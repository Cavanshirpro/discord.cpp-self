// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include "native_media_common.hpp"
#include "native_media.hpp"
#include "internal.hpp"
#include "native_recording.hpp"
#include <future>
namespace discord::detail {
using namespace media;
void run_native_media(MediaOptions options, std::string token, std::atomic_bool &stop,
                      const MediaSession::Handler &handler,
                      const std::function<std::deque<Json>()> &commands) {
    init_curl();
    daveSetLogSinkCallback(+[](DAVELoggingSeverity, const char *, int, const char *) {});
    std::exception_ptr callback_failure;
    MediaSession::Handler emit = [&](const Json &event) {
        try {
            handler(event);
        } catch (...) {
            callback_failure = std::current_exception();
            throw;
        }
    };
    Recorder recorder(options, emit);
    MediaPollTimer poll_timer;
#ifdef _WIN32
    struct TimerResolution {
        bool enabled = timeBeginPeriod(1) == TIMERR_NOERROR;
        ~TimerResolution() {
            if (enabled)
                timeEndPeriod(1);
        }
    } timer_resolution;
#endif
    std::mutex mutex;
    std::deque<RawEvent> incoming;
    std::vector<Json> outgoing;
    std::exception_ptr gateway_failure;
    std::atomic_bool gateway_stop = false;
    std::atomic_int64_t latency = -1;
    Gateway gateway(
        ClientOptions{}, std::move(token), gateway_stop, latency,
        [&](RawEvent event) {
            std::lock_guard lock(mutex);
            if (incoming.size() >= 4096)
                throw Error("Media Gateway queue full");
            incoming.push_back(std::move(event));
        },
        [&] {
            std::lock_guard lock(mutex);
            std::vector<Json> result;
            result.swap(outgoing);
            return result;
        });
    std::jthread network([&] {
        try {
            gateway.run();
        } catch (...) {
            std::lock_guard lock(mutex);
            gateway_failure = std::current_exception();
        }
    });
    struct Close {
        std::atomic_bool &flag;
        ~Close() { flag = true; }
    } close{gateway_stop};
    auto send = [&](int op, Json data) {
        std::lock_guard lock(mutex);
        if (outgoing.size() >= 128)
            throw Error("Media Gateway send queue full");
        outgoing.push_back({{"op", op}, {"d", std::move(data)}});
    };
    std::string self, session;
    bool joined = false, stream_requested = false, deaf = false;
    auto pending_join = Clock::time_point::max();
    Json server_event = nullptr;
    std::map<std::string, Json> participants, streams, stream_servers, profiles;
    std::map<std::pair<std::string, std::string>, Json> all_voice_states, members;
    auto remember_user = [&](const Json &user) {
        auto id = field_id(user, "id");
        if (!id.empty() && profiles.size() < 16384)
            profiles[id] = user;
    };
    auto remember_guild = [&](const Json &g) {
        auto gid = field_id(g, "id");
        for (auto state : g.value("voice_states", Json::array())) {
            state["guild_id"] = gid;
            const auto uid = field_id(state, "user_id");
            if (!uid.empty())
                all_voice_states[{gid, uid}] = std::move(state);
        }
        for (const auto &member : g.value("members", Json::array())) {
            auto user = member.value("user", Json::object());
            remember_user(user);
            auto id = field_id(user, "id");
            if (!id.empty())
                members[{gid, id}] = member;
        }
    };
    std::map<std::string, std::unique_ptr<Voice>> connections;
    std::set<std::string> watching;
    std::set<std::string> ignored_streams;
    std::map<std::string, Clock::time_point> retry_due;
    std::map<std::string, int> failures;
    auto guild = [&] { return options.guild_id.str(); };
    auto channel = [&] { return options.channel_id.str(); };
    auto own_key = [&] { return "guild:" + guild() + ":" + channel() + ":" + self; };
    auto users = [&] {
        std::set<std::string> ids{self};
        for (auto &[id, _] : participants)
            ids.insert(id);
        return ids;
    };
    auto voice_state = [&](bool leave = false) {
        send(4, {{"guild_id", guild()},
                 {"channel_id", leave ? Json(nullptr) : Json(channel())},
                 {"self_mute", false},
                 {"self_deaf", deaf},
                 {"self_video", !leave && options.camera && !options.video_source.empty()}});
    };
    auto stop_connections = [&] {
        for (auto &key : watching)
            send(19, {{"stream_key", key}});
        if (stream_requested)
            send(19, {{"stream_key", own_key()}});
        watching.clear();
        ignored_streams.clear();
        retry_due.clear();
        failures.clear();
        connections.clear();
        stream_servers.clear();
        streams.clear();
        server_event = nullptr;
        session.clear();
        joined = false;
        stream_requested = false;
    };
    std::shared_ptr<Sources> sources;
    auto preparation_options = options;
    auto preparation = std::async(std::launch::async, [preparation_options, &stop] {
        return Sources::prepare(preparation_options, stop, [](const Json &) {});
    });
    std::future<std::shared_ptr<Sources>> replacement;
    MediaOptions requested_options = options;
    size_t audio_index = 0, video_index = 0;
    auto audio_due = Clock::now(), video_due = Clock::now();
    auto start = Clock::now(), snapshot_due = start, stream_ping = start;
    bool playing = false, configured_record = false;
    uint64_t received_audio = 0, received_video = 0;
    auto receive = [&](ReceivedFrame f) {
        if (f.codec == "opus")
            ++received_audio;
        else
            ++received_video;
        recorder.frame(std::move(f));
    };
    auto refresh_participants = [&] {
        participants.clear();
        for (auto &[key, value] : all_voice_states)
            if (key.first == guild() && field_id(value, "channel_id") == channel())
                participants[key.second] = value;
    };
    auto watch = [&](const std::string &key) {
        const auto prefix = "guild:" + guild() + ":" + channel() + ":";
        if (!key.starts_with(prefix) || key == own_key())
            throw Error("Stream must belong to another user in the current channel");
        const auto id = key.substr(prefix.size());
        auto p = participants.find(id);
        if (p == participants.end() || !p->second.value("self_stream", false))
            throw Error("Stream is not announced in this channel");
        if (watching.size() >= 8 && !watching.contains(key))
            throw Error("At most eight watched streams");
        if (watching.insert(key).second)
            send(20, {{"stream_key", key}});
    };
    int generation = 0;
    struct ConnectJob {
        std::future<std::unique_ptr<Voice>> future;
        bool output;
        std::string owner;
    };
    std::map<std::pair<int, std::string>, ConnectJob> connect_jobs;
    auto create_connections = [&] {
        for (auto it = connect_jobs.begin(); it != connect_jobs.end();) {
            if (it->second.future.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
                ++it;
                continue;
            }
            const auto key = it->first.second;
            try {
                auto connection = it->second.future.get();
                if (it->first.first == generation &&
                    (key == "voice" ||
                     (streams.contains(key) && (key == own_key() || watching.contains(key))))) {
                    connections[key] = std::move(connection);
                    if (key == own_key())
                        send(22, {{"stream_key", key}, {"paused", false}});
                    else if (key != "voice")
                        emit({{"event", "stream_watched"},
                              {"stream_key", key},
                              {"owner_id", it->second.owner}});
                }
            } catch (const std::exception &error) {
                if (callback_failure)
                    std::rethrow_exception(callback_failure);
                if (it->first.first == generation) {
                    auto attempt = ++failures[key];
                    retry_due[key] =
                        Clock::now() + std::chrono::seconds{std::min(30, 1 << std::min(attempt, 4))};
                    emit({{"event", "transport_error"},
                          {"source", key},
                          {"message", error.what()},
                          {"retry", attempt}});
                    if (key == "voice" && attempt >= 3)
                        throw;
                    if (key != "voice" && key != own_key() && attempt >= 3) {
                        ignored_streams.insert(key);
                        watching.erase(key);
                        send(19, {{"stream_key", key}});
                    }
                }
            }
            it = connect_jobs.erase(it);
        }
        if (self.empty() || session.empty())
            return;
        if (!server_event.is_null() && !connections.contains("voice") &&
            !connect_jobs.contains({generation, "voice"}) && Clock::now() >= retry_due["voice"]) {
            auto endpoint = server_event.value("endpoint", Json(nullptr));
            if (endpoint.is_string()) {
                auto server = guild(), chan = channel(), voice_session = session, me = self;
                auto secret = server_event.at("token").get<std::string>();
                auto opts = options;
                auto recognized = users();
                connect_jobs.emplace(std::pair{generation, std::string("voice")},
                                     ConnectJob{std::async(std::launch::async,
                                                           [=, &stop] {
                                                               return std::make_unique<Voice>(
                                                                   endpoint.get<std::string>(), secret,
                                                                   server, chan, voice_session, me, "camera",
                                                                   chan, true, false, opts, stop, recognized,
                                                                   receive, emit);
                                                           }),
                                                true,
                                                {}});
            }
        }
        if (options.go_live && !options.video_source.empty() && connections.contains("voice") &&
            connections.at("voice")->connected() && !stream_requested &&
            Clock::now() >= retry_due["create_stream"]) {
            send(18, {{"type", "guild"}, {"guild_id", guild()}, {"channel_id", channel()}});
            stream_requested = true;
        }
        for (auto &[key, secret] : stream_servers) {
            if (connections.contains(key) || connect_jobs.contains({generation, key}) ||
                !streams.contains(key) || Clock::now() < retry_due[key])
                continue;
            auto &s = streams.at(key);
            auto server = field_id(s, "rtc_server_id"), chan = field_id(s, "rtc_channel_id");
            if (server.empty() || chan.empty() || secret.value("endpoint", Json(nullptr)).is_null())
                continue;
            bool output = key == own_key();
            if (!output && !watching.contains(key))
                continue;
            auto pos = key.find_last_of(':');
            auto owner = key.substr(pos + 1);
            if (connect_jobs.size() >= 32)
                continue;
            auto endpoint = secret.at("endpoint").get<std::string>(),
                 auth = secret.at("token").get<std::string>();
            auto voice_session = session, me = self;
            auto opts = options;
            auto recognized = users();
            auto group = std::to_string(std::stoull(server) - 1);
            connect_jobs.emplace(
                std::pair{generation, key},
                ConnectJob{std::async(std::launch::async,
                                      [=, &stop] {
                                          return std::make_unique<Voice>(
                                              endpoint, auth, server, chan, voice_session, me,
                                              output ? "outgoing_stream" : "stream-" + owner, group, output,
                                              true, opts, stop, recognized, receive, emit);
                                      }),
                           output, owner});
        }
    };
    try {
        emit({{"event", "starting"},
              {"backend", "cpp"},
              {"runtime", "C++20 / libdave / OpenSSL / Opus / FFmpeg"}});
        while (!stop) {
            std::deque<RawEvent> events;
            {
                std::lock_guard lock(mutex);
                if (gateway_failure)
                    std::rethrow_exception(gateway_failure);
                events.swap(incoming);
            }
            for (auto &e : events) {
                auto &d = e.data;
                if (e.name == "VOICE_STATE_UPDATE") {
                    auto gid = field_id(d, "guild_id"), id = field_id(d, "user_id");
                    if (all_voice_states.size() < 16384 || all_voice_states.contains({gid, id}))
                        all_voice_states[{gid, id}] = d;
                    if (d.contains("member")) {
                        members[{gid, id}] = d["member"];
                        remember_user(d["member"].value("user", Json::object()));
                    }
                }
                if (e.name == "READY") {
                    self = field_id(d.at("user"), "id");
                    remember_user(d.at("user"));
                    for (const auto &user : d.value("users", Json::array()))
                        remember_user(user);
                    for (const auto &g : d.value("guilds", Json::array()))
                        remember_guild(g);
                    refresh_participants();
                    voice_state(true);
                    pending_join = Clock::now() + std::chrono::milliseconds{200};
                } else if (e.name == "GUILD_CREATE") {
                    remember_guild(d);
                    refresh_participants();
                } else if (e.name == "GUILD_MEMBER_UPDATE" || e.name == "GUILD_MEMBER_ADD") {
                    auto user = d.value("user", Json::object());
                    remember_user(user);
                    members[{field_id(d, "guild_id"), field_id(user, "id")}] = d;
                } else if (e.name == "VOICE_STATE_UPDATE" && field_id(d, "guild_id") == guild()) {
                    auto id = field_id(d, "user_id");
                    if (field_id(d, "channel_id") == channel()) {
                        participants[id] = d;
                        if (id == self) {
                            session = d.at("session_id").get<std::string>();
                            joined = true;
                            deaf = d.value("self_deaf", deaf);
                        }
                    } else {
                        participants.erase(id);
                        if (id == self && joined)
                            throw Error("Account left the selected voice channel");
                    }
                    emit({{"event", "participant_changed"},
                          {"user_id", id},
                          {"camera", d.value("self_video", false)},
                          {"streaming", d.value("self_stream", false)}});
                } else if (e.name == "VOICE_SERVER_UPDATE" && field_id(d, "guild_id") == guild()) {
                    server_event = d;
                    emit({{"event", "voice_server_ready"},
                          {"guild_id", guild()},
                          {"has_endpoint", d.value("endpoint", Json(nullptr)).is_string()}});
                } else if (e.name == "STREAM_CREATE" || e.name == "STREAM_UPDATE") {
                    auto key = d.value("stream_key", std::string{});
                    if (key.starts_with("guild:" + guild() + ":" + channel() + ":")) {
                        auto &s = streams[key];
                        if (!s.is_object())
                            s = Json::object();
                        s.merge_patch(d);
                        emit({{"event", e.name == "STREAM_CREATE" ? "stream_created" : "stream_changed"},
                              {"stream_key", key}});
                    }
                } else if (e.name == "STREAM_SERVER_UPDATE")
                    stream_servers[d.at("stream_key").get<std::string>()] = d;
                else if (e.name == "STREAM_DELETE") {
                    auto key = d.value("stream_key", std::string{});
                    if (key == own_key()) {
                        stream_requested = false;
                        retry_due["create_stream"] = Clock::now() + std::chrono::seconds{2};
                    }
                    connections.erase(key);
                    stream_servers.erase(key);
                    streams.erase(key);
                    watching.erase(key);
                }
            }
            for (auto &command : commands())
                try {
                    auto op = command.value("op", std::string{});
                    if (op == "set_deaf") {
                        deaf = command.at("deaf").get<bool>();
                        voice_state();
                    } else if (op == "watch_stream") {
                        auto key = command.at("stream_key").get<std::string>();
                        ignored_streams.erase(key);
                        watch(key);
                    } else if (op == "unwatch_stream") {
                        auto key = command.at("stream_key").get<std::string>();
                        ignored_streams.insert(key);
                        if (watching.erase(key)) {
                            send(19, {{"stream_key", key}});
                            connections.erase(key);
                        }
                    } else if (op == "record") {
                        recorder.start(path_from(command.at("directory").get<std::string>()),
                                       command.at("audio_seconds").get<int>(),
                                       command.at("video_seconds").get<int>());
                        for (auto &[_, v] : connections)
                            v->keyframes();
                    } else if (op == "stop_recording")
                        recorder.finish(true);
                    else if (op == "set_sources") {
                        if (replacement.valid())
                            throw Error("A source replacement is still being prepared");
                        requested_options = options;
                        requested_options.video_format.clear();
                        requested_options.audio_format.clear();
                        requested_options.video_source = command.at("video_source").get<std::string>();
                        requested_options.audio_source = command.at("audio_source").get<std::string>();
                        auto prepare_options = requested_options;
                        replacement = std::async(std::launch::async, [prepare_options, &stop] {
                            return Sources::prepare(prepare_options, stop, [](const Json &) {});
                        });
                        emit({{"event", "preparing_source"},
                              {"video_source", requested_options.video_source},
                              {"audio_source", requested_options.audio_source}});
                    } else if (op == "join") {
                        recorder.finish(true);
                        voice_state(true);
                        stop_connections();
                        ++generation;
                        participants.clear();
                        options.guild_id = command.at("guild_id").get<Snowflake>();
                        options.channel_id = command.at("channel_id").get<Snowflake>();
                        refresh_participants();
                        playing = false;
                        audio_due = video_due = Clock::now();
                        voice_state();
                        start = Clock::now();
                    }
                    emit({{"event", "command_accepted"}, {"op", op}});
                } catch (const std::exception &e) {
                    emit(
                        {{"event", "command_error"}, {"message", e.what()}, {"op", command.value("op", "")}});
                }
            if (!sources && preparation.valid() &&
                preparation.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
                sources = preparation.get();
                audio_due = video_due = Clock::now();
            }
            if (replacement.valid() &&
                replacement.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
                try {
                    sources = replacement.get();
                    options.video_source = requested_options.video_source;
                    options.audio_source = requested_options.audio_source;
                    audio_index = video_index = 0;
                    audio_due = video_due = Clock::now();
                    for (auto &[_, v] : connections)
                        v->set_sources(options);
                    if (options.video_source.empty() && stream_requested) {
                        send(19, {{"stream_key", own_key()}});
                        connections.erase(own_key());
                        streams.erase(own_key());
                        stream_servers.erase(own_key());
                        stream_requested = false;
                    }
                    voice_state();
                    emit({{"event", "sources_changed"},
                          {"video_source", options.video_source},
                          {"audio_source", options.audio_source}});
                } catch (const std::exception &e) {
                    emit({{"event", "command_error"}, {"op", "set_sources"}, {"message", e.what()}});
                }
            if (Clock::now() >= pending_join) {
                pending_join = Clock::time_point::max();
                voice_state();
            }
            create_connections();
            if (options.watch_streams)
                for (auto &[id, p] : participants)
                    if (id != self && p.value("self_stream", false)) {
                        auto key = "guild:" + guild() + ":" + channel() + ":" + id;
                        if (!ignored_streams.contains(key))
                            watch(key);
                    }
            for (auto it = connections.begin(); it != connections.end();) {
                try {
                    it->second->recognize(users());
                    it->second->tick();
                    ++it;
                } catch (const std::exception &error) {
                    if (callback_failure)
                        std::rethrow_exception(callback_failure);
                    auto key = it->first;
                    auto attempt = ++failures[key];
                    emit({{"event", "transport_error"},
                          {"source", key == own_key() ? "outgoing_stream" : key},
                          {"message", error.what()},
                          {"retry", attempt}});
                    if (key == "voice" && attempt >= 3)
                        throw;
                    retry_due[key] =
                        Clock::now() + std::chrono::seconds{std::min(30, 1 << std::min(attempt, 4))};
                    if (attempt >= 3 && key != "voice" && key != own_key()) {
                        ignored_streams.insert(key);
                        watching.erase(key);
                        send(19, {{"stream_key", key}});
                        stream_servers.erase(key);
                    }
                    it = connections.erase(it);
                }
            }
            auto now = Clock::now();
            bool ready = connections.contains("voice") && connections.at("voice")->connected();
            if (sources && ready && !playing) {
                playing = true;
                audio_due = video_due = now;
                emit({{"event", "playing"},
                      {"backend", "cpp"},
                      {"guild_id", guild()},
                      {"channel_id", channel()}});
                start = now;
            }
            if (sources && ready) {
                if (now >= audio_due && sources->has_audio()) {
                    auto packet = sources->take_audio(audio_index, options.loop);
                    if (!packet.empty()) {
                        for (auto &[key, v] : connections)
                            if (key == "voice" || key == own_key())
                                v->audio(packet);
                        audio_due += std::chrono::milliseconds{20};
                    } else
                        audio_due = now + std::chrono::milliseconds{2};
                    if (now - audio_due > std::chrono::milliseconds{100})
                        audio_due = now;
                }
                if (now >= video_due && sources->has_video()) {
                    for (auto &[key, v] : connections)
                        if ((key == "voice" || key == own_key()) && v->force_keyframe) {
                            for (size_t i = 0; i < sources->video.size(); ++i) {
                                size_t idx = (video_index + i) % sources->video.size();
                                bool keyframe = false;
                                for (auto &n : nals(sources->video[idx]))
                                    if ((n[0] & 31) == 5)
                                        keyframe = true;
                                if (keyframe) {
                                    video_index = idx;
                                    break;
                                }
                            }
                            v->force_keyframe = false;
                        }
                    auto packet = sources->take_video(video_index, options.loop);
                    if (!packet.empty()) {
                        for (auto &[key, v] : connections)
                            if (key == own_key() || (key == "voice" && options.camera))
                                v->video(packet);
                        video_due += std::chrono::nanoseconds{1000000000 / options.fps};
                    } else
                        video_due = now + std::chrono::milliseconds{2};
                    if (now - video_due > std::chrono::milliseconds{100})
                        video_due = now;
                }
            }

            if (!configured_record && !options.recording_directory.empty() &&
                (!options.record_wait_for_media || ((!options.receive_audio || received_audio) &&
                                                    (!options.receive_video || received_video)))) {
                configured_record = true;
                recorder.start(options.recording_directory, options.record_audio_seconds,
                               options.record_video_seconds);
                for (auto &[_, v] : connections)
                    v->keyframes();
            }
            recorder.tick();
            if (now >= snapshot_due) {
                Json state{{"event", "snapshot"},
                           {"backend", "cpp"},
                           {"guild_id", guild()},
                           {"channel_id", channel()},
                           {"self_user_id", self},
                           {"self_deaf", deaf},
                           {"recording", recorder.active()},
                           {"participants", Json::array()},
                           {"streams", Json::array()},
                           {"transports", Json::object()},
                           {"receive", {{"audio_frames", received_audio}, {"video_frames", received_video}}},
                           {"video_source", options.video_source},
                           {"audio_source", options.audio_source}};
                for (auto &[id, p] : participants) {
                    Json member = members.contains({guild(), id}) ? members.at({guild(), id})
                                                                  : p.value("member", Json::object());
                    if (!member.is_object())
                        member = Json::object();
                    Json user =
                        profiles.contains(id) ? profiles.at(id) : member.value("user", Json::object());
                    auto visible_state = p;
                    visible_state.erase("session_id");
                    visible_state.erase("member");
                    state["participants"].push_back(
                        {{"user_id", id},
                         {"username", user.value("username", id)},
                         {"display_name", member.value("nick", Json(nullptr))},
                         {"global_name", user.value("global_name", Json(nullptr))},
                         {"avatar", user.value("avatar", Json(nullptr))},
                         {"public_flags", user.value("public_flags", 0)},
                         {"bot", user.value("bot", false)},
                         {"roles", member.value("roles", Json::array())},
                         {"camera", p.value("self_video", false)},
                         {"streaming", p.value("self_stream", false)},
                         {"self_deaf", p.value("self_deaf", false)},
                         {"self_mute", p.value("self_mute", false)},
                         {"mute", p.value("mute", false)},
                         {"deaf", p.value("deaf", false)},
                         {"suppress", p.value("suppress", false)},
                         {"voice_state", visible_state}});
                }
                for (auto &[key, s] : streams) {
                    auto v = s;
                    v.erase("rtc_server_id");
                    v.erase("rtc_channel_id");
                    v["key"] = key;
                    v["owner_id"] = key.substr(key.find_last_of(':') + 1);
                    v["watching"] = watching.contains(key);
                    auto viewer = v.find("viewer_ids");
                    v["viewer_count"] =
                        viewer != v.end() && viewer->is_array() ? Json(viewer->size()) : Json(nullptr);
                    state["streams"].push_back(std::move(v));
                }
                for (const auto &[id, p] : participants)
                    if (p.value("self_stream", false)) {
                        auto key = "guild:" + guild() + ":" + channel() + ":" + id;
                        if (!streams.contains(key))
                            state["streams"].push_back({{"key", key},
                                                        {"owner_id", id},
                                                        {"watching", watching.contains(key)},
                                                        {"metadata_received", false},
                                                        {"viewer_ids", nullptr},
                                                        {"viewer_count", nullptr}});
                    }
                for (auto &[key, v] : connections)
                    state["transports"][key == own_key() ? "outgoing_stream" : key] = v->snapshot();
                emit(state);
                snapshot_due = now + std::chrono::seconds{1};
            }
            if (now >= stream_ping) {
                for (auto &key : watching)
                    send(21, {{"stream_key", key}});
                if (stream_requested)
                    send(21, {{"stream_key", own_key()}});
                stream_ping = now + std::chrono::seconds{30};
            }
            if (options.duration.count() && playing && now - start >= options.duration)
                break;
            if (!joined && now - start > options.connect_timeout)
                throw Error("Voice channel join timed out");
            auto wake = Clock::now() + std::chrono::milliseconds{2};
            if (sources && ready) {
                if (sources->has_video())
                    wake = std::min(wake, video_due);
                if (sources->has_audio())
                    wake = std::min(wake, audio_due);
            }
            poll_timer.wait(std::chrono::duration_cast<std::chrono::nanoseconds>(wake - Clock::now()));
        }
        stop = true;
        recorder.flush();
        stop_connections();
        if (!self.empty())
            voice_state(true);
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        emit({{"event", "stopped"}});
    } catch (...) {
        stop = true;
        connections.clear();
        try {
            recorder.flush();
            if (!self.empty())
                voice_state(true);
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        } catch (...) {
        }
        throw;
    }
}
} // namespace discord::detail
