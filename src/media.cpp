// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/media.hpp>
#include <discord/errors.hpp>
#ifdef DISCORD_SELF_NATIVE_MEDIA
#include "native_media.hpp"
#endif
namespace discord {
namespace {
std::string utf8(const std::filesystem::path &p) {
    const auto s = p.u8string();
    return {s.begin(), s.end()};
}
} // namespace
static_assert(std::atomic_bool::is_always_lock_free, "Media stop flag must be signal-safe");
MediaSession::MediaSession(MediaOptions options) : options_(std::move(options)) {
    const auto &o = options_;
    if (!o.guild_id || !o.channel_id || o.ffmpeg.empty())
        throw std::invalid_argument("Media requires guild/channel IDs and an FFmpeg executable");
    if (o.width < 16 || o.width > 3840 || o.width % 2 || o.height < 16 || o.height > 2160 || o.height % 2 ||
        o.fps < 1 || o.fps > 60 || o.bitrate_kbps < 100 || o.bitrate_kbps > 20000 ||
        o.connect_timeout.count() < 1000 || o.connect_timeout.count() > 300000 || o.duration.count() < 0 ||
        o.duration.count() > 31536000)
        throw std::invalid_argument("Invalid media dimensions, frame rate, bitrate or timeout");
    if (((o.camera || o.go_live) && o.video_source.empty()) ||
        (!(o.camera || o.go_live) && o.audio_source.empty() && !o.receive_audio && !o.receive_video))
        throw std::invalid_argument(
            "Video outputs require video; receive-only mode requires receiving enabled");
    if (o.record_audio_seconds < 0 || o.record_audio_seconds > 300 || o.record_video_seconds < 0 ||
        o.record_video_seconds > 300)
        throw std::invalid_argument("Invalid recording duration");
    for (const auto *value : {&o.ffmpeg, &o.video_source, &o.audio_source, &o.video_format, &o.audio_format})
        if (value->find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in media option");
}
bool MediaSession::available() noexcept {
#ifdef DISCORD_SELF_NATIVE_MEDIA
    return true;
#else
    return false;
#endif
}
void MediaSession::run(std::string token, Handler handler) {
    if (token.empty() || token.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        throw std::invalid_argument("Invalid media token");
    if (used_.exchange(true))
        throw Error("MediaSession is single-use; create a new session");
    running_ = true;
    struct Reset {
        std::atomic_bool &flag;
        ~Reset() { flag = false; }
    } reset{running_};
    if (stop_)
        return;
#ifdef DISCORD_SELF_NATIVE_MEDIA
    detail::run_native_media(
        options_, std::move(token), stop_,
        [&](const Json &event) {
            if (event.value("event", "") == "snapshot") {
                std::lock_guard lock(state_mutex_);
                snapshot_ = event;
            }
            if (handler)
                handler(event);
        },
        [&] {
            std::lock_guard lock(state_mutex_);
            std::deque<Json> values;
            values.swap(commands_);
            return values;
        });
#else
    (void)handler;
    throw Error("Native C++ media was not built; configure DISCORD_SELF_NATIVE_MEDIA=ON");
#endif
}
Json MediaSession::snapshot() const {
    std::lock_guard lock(state_mutex_);
    return snapshot_;
}
void MediaSession::command(Json value) {
    if (!running_)
        throw Error("Media session is not running");
    std::lock_guard lock(state_mutex_);
    if (commands_.size() >= 64)
        throw Error("Media command queue is full");
    commands_.push_back(std::move(value));
}
void MediaSession::watch_stream(std::string key) {
    command({{"op", "watch_stream"}, {"stream_key", std::move(key)}});
}
void MediaSession::unwatch_stream(std::string key) {
    command({{"op", "unwatch_stream"}, {"stream_key", std::move(key)}});
}
void MediaSession::set_deaf(bool value) { command({{"op", "set_deaf"}, {"deaf", value}}); }
void MediaSession::set_muted(bool value) { command({{"op", "set_muted"}, {"muted", value}}); }
void MediaSession::set_audio_playback(bool value) { command({{"op", "set_audio_playback"}, {"enabled", value}}); }
void MediaSession::stop_recording() { command({{"op", "stop_recording"}}); }
void MediaSession::set_sources(std::string video, std::string audio) {
    if (video.find('\0') != std::string::npos || audio.find('\0') != std::string::npos)
        throw std::invalid_argument("NUL in media source");
    command({{"op", "set_sources"}, {"video_source", std::move(video)}, {"audio_source", std::move(audio)}});
}
void MediaSession::join(Snowflake guild, Snowflake channel) {
    if (!guild || !channel)
        throw std::invalid_argument("Guild and voice channel IDs are required");
    command({{"op", "join"}, {"guild_id", guild}, {"channel_id", channel}});
}
void MediaSession::record(std::filesystem::path directory, int audio, int video) {
    if (directory.empty() || audio < 0 || audio > 300 || video < 0 || video > 300 || (!audio && !video))
        throw std::invalid_argument("Invalid recording directory/duration");
    command({{"op", "record"},
             {"directory", utf8(directory)},
             {"audio_seconds", audio},
             {"video_seconds", video}});
}
} // namespace discord
