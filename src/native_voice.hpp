// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include "native_media_common.hpp"
#include "native_rtp_order.hpp"
#include <curl/curl.h>
#include <dave/dave.h>
#include <openssl/evp.h>
namespace discord::detail::media {
class SocketWeb {
    CURL *curl_ = nullptr;
    Bytes pending_;
    bool binary_ = false;
    std::atomic_bool &stop_;

  public:
    SocketWeb(const std::string &url, std::atomic_bool &stop, long timeout) : stop_(stop) {
        curl_ = curl_easy_init();
        if (!curl_)
            throw Error("Cannot create voice websocket");
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);
        curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, timeout);
        curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(
            curl_, CURLOPT_XFERINFOFUNCTION,
            +[](void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return static_cast<std::atomic_bool *>(p)->load() ? 1 : 0;
            });
        curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, &stop_);
        auto code = curl_easy_perform(curl_);
        if (code != CURLE_OK) {
            curl_easy_cleanup(curl_);
            curl_ = nullptr;
            throw TransportError("Voice websocket connection failed");
        }
    }
    ~SocketWeb() {
        if (curl_)
            curl_easy_cleanup(curl_);
    }
    void send(View data, bool binary = false) {
        size_t sent = 0;
        auto end = Clock::now() + std::chrono::seconds{5};
        while (sent < data.size()) {
            size_t n = 0;
            auto code = curl_ws_send(curl_, data.data() + sent, data.size() - sent, &n, 0,
                                     binary ? CURLWS_BINARY : CURLWS_TEXT);
            sent += n;
            if (code != CURLE_OK && code != CURLE_AGAIN)
                throw TransportError("Voice websocket send failed");
            if (stop_ || Clock::now() > end)
                throw Error("Voice websocket send interrupted");
            if (sent < data.size())
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    void send(const Json &j) {
        const auto s = j.dump();
        send(View(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
    }
    std::optional<std::pair<Bytes, bool>> receive() {
        std::array<uint8_t, 16384> data;
        size_t n = 0;
        const curl_ws_frame *meta = nullptr;
        auto code = curl_ws_recv(curl_, data.data(), data.size(), &n, &meta);
        if (code == CURLE_AGAIN)
            return {};
        if (code != CURLE_OK)
            throw TransportError("Voice websocket receive failed");
        if (!meta)
            return {};
        if (meta->flags & CURLWS_CLOSE)
            throw GatewayError(n >= 2 ? u16(View(data).first(n)) : 1005);
        if (meta->flags & (CURLWS_PING | CURLWS_PONG))
            return {};
        if (pending_.empty())
            binary_ = (meta->flags & CURLWS_BINARY) != 0;
        if (pending_.size() + n > 4 * 1024 * 1024)
            throw Error("Voice websocket message limit");
        append(pending_, View(data).first(n));
        if (meta->bytesleft || (meta->flags & CURLWS_CONT))
            return {};
        auto value = std::move(pending_);
        pending_ = {};
        return std::pair{std::move(value), binary_};
    }
};
class TransportCrypto {
    std::array<uint8_t, 32> key_{};
    uint32_t counter_ = 1;
    bool ready_ = false;

  public:
    void key(const Json &j) {
        if (!j.is_array() || j.size() != 32)
            throw Error("Invalid UDP key size");
        for (size_t i = 0; i < 32; ++i)
            key_[i] = j[i].get<uint8_t>();
        ready_ = true;
    }
    bool ready() const { return ready_; }
    Bytes encrypt(View head, View data) {
        if (!ready_)
            return {};
        if (!counter_)
            throw Error("UDP nonce space exhausted; reconnect before sending more media");
        std::array<uint8_t, 12> nonce{};
        uint32_t v = counter_++;
        for (int i = 0; i < 4; ++i)
            nonce[i] = uint8_t(v >> (8 * i));
        auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(EVP_CIPHER_CTX_new(),
                                                                                   EVP_CIPHER_CTX_free);
        Bytes out(head.begin(), head.end());
        auto off = out.size();
        out.resize(off + data.size() + 16 + 4);
        int n = 0, len = 0;
        if (!ctx ||
            EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key_.data(), nonce.data()) != 1 ||
            EVP_EncryptUpdate(ctx.get(), nullptr, &n, head.data(), int(head.size())) != 1 ||
            EVP_EncryptUpdate(ctx.get(), out.data() + off, &n, data.data(), int(data.size())) != 1)
            throw Error("UDP encryption failed");
        len = n;
        if (EVP_EncryptFinal_ex(ctx.get(), out.data() + off + len, &n) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, out.data() + off + data.size()) != 1)
            throw Error("UDP authentication failed");
        std::copy_n(nonce.data(), 4, out.end() - 4);
        return out;
    }
    Bytes decrypt(View packet, size_t header) {
        if (!ready_ || packet.size() < header + 20)
            return {};
        std::array<uint8_t, 12> nonce{};
        std::copy_n(packet.end() - 4, 4, nonce.begin());
        size_t size = packet.size() - header - 20;
        auto ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(EVP_CIPHER_CTX_new(),
                                                                                   EVP_CIPHER_CTX_free);
        Bytes out(size + 16);
        int n = 0, len = 0;
        if (!ctx ||
            EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key_.data(), nonce.data()) != 1 ||
            EVP_DecryptUpdate(ctx.get(), nullptr, &n, packet.data(), int(header)) != 1 ||
            EVP_DecryptUpdate(ctx.get(), out.data(), &n, packet.data() + header, int(size)) != 1)
            return {};
        len = n;
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16,
                                const_cast<uint8_t *>(packet.data() + header + size)) != 1 ||
            EVP_DecryptFinal_ex(ctx.get(), out.data() + len, &n) != 1)
            return {};
        out.resize(len + n);
        return out;
    }
};
struct ReceivedFrame {
    std::string source, user, codec;
    uint32_t ssrc = 0, timestamp = 0;
    Bytes data;
    Clock::time_point arrival = Clock::now();
};
struct FrameAssembly {
    uint32_t timestamp = 0;
    uint16_t next = 0;
    bool initialized = false, broken = false;
    Bytes data;
};
struct RemoteSource {
    std::string user;
    uint32_t primary = 0, rtx = 0;
    std::string codec = "H264";
};
inline Json receiver_wants(const std::map<uint32_t, RemoteSource> &sources,
                           const std::map<std::string, uint32_t> &selected, std::string_view self, bool audio,
                           bool video, int pixel_count) {
    Json wants = {{"any", 0}}, pixels = Json::object();
    for (const auto &[ssrc, source] : sources) {
        if (source.user == self || ssrc != source.primary)
            continue;
        if (source.codec == "opus") {
            if (audio)
                wants[std::to_string(ssrc)] = 100;
            continue;
        }
        const auto layer = selected.find(source.user);
        if (video && layer != selected.end() && layer->second == ssrc) {
            wants[std::to_string(ssrc)] = 100;
            pixels[std::to_string(ssrc)] = pixel_count;
        }
    }
    wants["pixelCounts"] = std::move(pixels);
    return wants;
}
inline bool audio_output_enabled(const MediaOptions &options, bool screen) {
    return !options.muted && !options.audio_source.empty() &&
           (screen ? options.stream_audio : options.voice_audio);
}
class Voice {
    std::unique_ptr<SocketWeb> ws_;
    std::atomic_bool &stop_;
    MediaOptions options_;
#ifdef _WIN32
    SOCKET udp_ = INVALID_SOCKET;
#else
    int udp_ = -1;
#endif
    TransportCrypto crypto_;
    DAVESessionHandle session_ = nullptr;
    DAVEEncryptorHandle encryptor_ = nullptr;
    std::map<std::string, DAVEDecryptorHandle> decryptors_;
    std::map<int, int> transitions_;
    std::set<std::string> recognized_;
    std::map<uint32_t, RemoteSource> sources_;
    std::map<uint32_t, FrameAssembly> assembly_;
    std::map<uint32_t, RtpOrder> reorder_;
    std::map<uint32_t, Clock::time_point> last_frame_;
    std::map<std::string, uint32_t> selected_video_;
    struct Reception {
        uint64_t prior_total = 0, prior_lost = 0;
        uint32_t last_timestamp = 0, lsr = 0;
        double jitter = 0;
        Clock::time_point last_arrival{}, sr_arrival{};
    };
    std::map<uint32_t, Reception> reception_;
    uint64_t receiver_reports_sent_ = 0;
    struct Cached {
        uint16_t seq;
        uint32_t timestamp;
        uint8_t pt;
        bool marker;
        Bytes payload;
    };
    std::deque<Cached> retransmit_;
    std::deque<double> video_intervals_;
    Json receiver_reports_ = Json::array();
    uint32_t ssrc_ = 0, video_ssrc_ = 0, rtx_ssrc_ = 0, audio_ts_ = random32(), video_ts_ = random32();
    uint16_t aseq_ = uint16_t(random32()), vseq_ = uint16_t(random32()), rseq_ = uint16_t(random32()),
             transport_sequence_ = uint16_t(random32());
    std::string self_, server_, channel_, group_, source_;
    bool output_ = false, screen_ = false, ready_ = false;
    int version_ = 0, seq_ack_ = -1;
    uint64_t video_sent_ = 0, audio_sent_ = 0, received_ = 0, decrypt_failed_ = 0, udp_packets_ = 0,
             rtx_received_ = 0, rtx_sent_ = 0, broken_frames_ = 0;
    Clock::time_point heartbeat_ = Clock::time_point::max(), wants_ = Clock::time_point::max(),
                      started_ = Clock::now(), last_ack_ = Clock::now(), last_sent_{};
    std::chrono::milliseconds heartbeat_interval_{5000};
    uint32_t sr_audio_packets_ = 0, sr_audio_bytes_ = 0, sr_video_packets_ = 0, sr_video_bytes_ = 0;
    Clock::time_point sr_due_ = Clock::now() + std::chrono::seconds{1};
    std::function<void(ReceivedFrame)> frame_;
    MediaSession::Handler emit_;
    void udp_send(View p) {
        if (p.empty())
            return;
        auto n = ::send(udp_, reinterpret_cast<const char *>(p.data()), int(p.size()), 0);
        if (n < 0)
            ++udp_errors_;
    }
    uint64_t udp_errors_ = 0;
    std::array<uint64_t, 128> payload_packets_{};
    uint64_t unmapped_rtp_ = 0, transport_decrypt_failures_ = 0;
    bool discovering_ = false;
    Clock::time_point discovery_deadline_{}, discovery_retry_{};
    void send_binary(int op, View p) {
        Bytes b{uint8_t(op)};
        append(b, p);
        ws_->send(b, true);
    }
    void session_init(int version) {
        version_ = version;
        daveSessionInit(session_, uint16_t(version), std::stoull(group_), self_.c_str());
        uint8_t *data = nullptr;
        size_t size = 0;
        daveSessionGetMarshalledKeyPackage(session_, &data, &size);
        if (data && size)
            send_binary(26, View(data, size));
        daveFree(data);
        daveEncryptorSetKeyRatchet(encryptor_, nullptr);
        daveEncryptorSetPassthroughMode(encryptor_, false);
    }
    void ratchets() {
        for (const auto &id : recognized_) {
            auto key = daveSessionGetKeyRatchet(session_, id.c_str());
            if (!key)
                continue;
            if (id == self_)
                daveEncryptorSetKeyRatchet(encryptor_, key);
            else {
                auto &d = decryptors_[id];
                if (!d)
                    d = daveDecryptorCreate();
                daveDecryptorTransitionToKeyRatchet(d, key);
                daveDecryptorTransitionToPassthroughMode(d, false);
            }
            daveKeyRatchetDestroy(key);
        }
    }
    void execute(int id) {
        auto p = transitions_.find(id);
        if (p == transitions_.end())
            return;
        version_ = p->second;
        transitions_.erase(p);
        ratchets();
    }
    void transition_ready(int id) { ws_->send(Json{{"op", 23}, {"d", {{"transition_id", id}}}}); }
    void binary(View p) {
        if (p.size() < 3)
            return;
        seq_ack_ = u16(p);
        int op = p[2];
        if (op == 25)
            daveSessionSetExternalSender(session_, p.data() + 3, p.size() - 3);
        else if (op == 27) {
            std::vector<const char *> ids;
            for (auto &s : recognized_)
                ids.push_back(s.c_str());
            uint8_t *data = nullptr;
            size_t size = 0;
            daveSessionProcessProposals(session_, p.data() + 3, p.size() - 3, ids.data(), ids.size(), &data,
                                        &size);
            if (data && size)
                send_binary(28, View(data, size));
            daveFree(data);
        } else if ((op == 29 || op == 30) && p.size() >= 5) {
            int transition = u16(p, 3);
            bool success = false;
            uint64_t *roster = nullptr;
            size_t count = 0;
            if (op == 29) {
                auto r = daveSessionProcessCommit(session_, p.data() + 5, p.size() - 5);
                if (r) {
                    success = !daveCommitResultIsFailed(r);
                    if (success && !daveCommitResultIsIgnored(r))
                        daveCommitResultGetRosterMemberIds(r, &roster, &count);
                    daveCommitResultDestroy(r);
                }
            } else {
                std::vector<const char *> ids;
                for (auto &s : recognized_)
                    ids.push_back(s.c_str());
                auto r =
                    daveSessionProcessWelcome(session_, p.data() + 5, p.size() - 5, ids.data(), ids.size());
                if (r) {
                    success = true;
                    daveWelcomeResultGetRosterMemberIds(r, &roster, &count);
                    daveWelcomeResultDestroy(r);
                }
            }
            if (success) {
                for (size_t i = 0; i < count; ++i)
                    recognized_.insert(std::to_string(roster[i]));
                daveFree(roster);
                ratchets();
                if (transition) {
                    transitions_[transition] = version_;
                    transition_ready(transition);
                }
            } else {
                ws_->send(Json{{"op", 31}, {"d", {{"transition_id", transition}}}});
                session_init(version_);
            }
        }
    }
    bool sends_video() const {
        return output_ && (screen_ || options_.camera) && !options_.video_source.empty();
    }
    Json stream_description() const {
        return {
            {"type", screen_ ? "screen" : "video"},
            {"rid", "100"},
            {"ssrc", video_ssrc_},
            {"rtx_ssrc", rtx_ssrc_},
            {"active", sends_video()},
            {"quality", 100},
            {"max_bitrate", options_.bitrate_kbps * 1000},
            {"max_framerate", options_.fps},
            {"max_resolution", {{"type", "fixed"}, {"width", options_.width}, {"height", options_.height}}}};
    }
    void signal_output() {
        ws_->send(Json{{"op", 5},
                       {"d",
                        {{"speaking", output_ && audio_output_enabled(options_, screen_) ? 1 : 0},
                         {"delay", 0},
                         {"ssrc", ssrc_}}}});
        ws_->send(Json{{"op", 12},
                       {"d",
                        {{"audio_ssrc", ssrc_},
                         {"video_ssrc", sends_video() ? video_ssrc_ : 0},
                         {"rtx_ssrc", sends_video() ? rtx_ssrc_ : 0},
                         {"streams", sends_video() ? Json::array({stream_description()}) : Json::array()}}}});
    }
    void initial(const Json &d) {
        ssrc_ = d.at("ssrc").get<uint32_t>();
        video_ssrc_ = ssrc_ + 1;
        rtx_ssrc_ = ssrc_ + 2;
        if (d.contains("streams") && d["streams"].is_array() && !d["streams"].empty()) {
            auto s = d["streams"][0];
            video_ssrc_ = s.value("ssrc", video_ssrc_);
            rtx_ssrc_ = s.value("rtx_ssrc", rtx_ssrc_);
        }
        auto modes = d.at("modes");
        if (std::find(modes.begin(), modes.end(), "aead_aes256_gcm_rtpsize") == modes.end())
            throw Error("Voice server lacks AES256-GCM RTP transport");
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(d.at("port").get<uint16_t>());
        if (inet_pton(AF_INET, d.at("ip").get<std::string>().c_str(), &peer.sin_addr) != 1)
            throw Error("Invalid voice UDP endpoint");
        udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (::connect(udp_, reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) != 0)
            throw Error("Cannot connect voice UDP");
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(udp_, FIONBIO, &mode);
#else
        fcntl(udp_, F_SETFL, O_NONBLOCK);
#endif
        int receive_buffer = 4 * 1024 * 1024;
        setsockopt(udp_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&receive_buffer),
                   sizeof(receive_buffer));
        Bytes packet;
        put16(packet, 1);
        put16(packet, 70);
        put32(packet, ssrc_);
        packet.resize(74);
        udp_send(packet);
        discovering_ = true;
        discovery_deadline_ = Clock::now() + std::chrono::seconds{5};
        discovery_retry_ = Clock::now() + std::chrono::milliseconds{300};
    }
    void discover() {
        if (!discovering_)
            return;
        if (Clock::now() > discovery_deadline_)
            throw Error("Voice IP discovery timed out");
        std::array<uint8_t, 2048> data{};
        auto n = recv(udp_, reinterpret_cast<char *>(data.data()), int(data.size()), 0);
        if (n == 74 && u16(data) == 2 && u16(data, 2) == 70 && u32(data, 4) == ssrc_) {
            auto nul = std::find(data.begin() + 8, data.begin() + 72, 0);
            std::string address(data.begin() + 8, nul);
            auto port = u16(data, 72);
            select_protocol(address, port);
            discovering_ = false;
        } else if (Clock::now() >= discovery_retry_) {
            Bytes p;
            put16(p, 1);
            put16(p, 70);
            put32(p, ssrc_);
            p.resize(74);
            udp_send(p);
            discovery_retry_ = Clock::now() + std::chrono::milliseconds{300};
        }
    }
    void select_protocol(const std::string &address, int port) {
        Json codecs = Json::array({{{"name", "opus"},
                                    {"type", "audio"},
                                    {"priority", 1000},
                                    {"payload_type", 120},
                                    {"encode", true},
                                    {"decode", options_.receive_audio}},
                                   {{"name", "H264"},
                                    {"type", "video"},
                                    {"priority", 1000},
                                    {"payload_type", 101},
                                    {"rtx_payload_type", 102},
                                    {"encode", true},
                                    {"decode", true}},
                                   {{"name", "H265"},
                                    {"type", "video"},
                                    {"priority", 2000},
                                    {"payload_type", 103},
                                    {"rtx_payload_type", 104},
                                    {"encode", false},
                                    {"decode", true}}});
        ws_->send(
            Json{{"op", 1},
                 {"d",
                  {{"protocol", "udp"},
                   {"data", {{"address", address}, {"port", port}, {"mode", "aead_aes256_gcm_rtpsize"}}},
                   {"codecs", codecs}}}});
        daveEncryptorAssignSsrcToCodec(encryptor_, ssrc_, DAVE_CODEC_OPUS);
        daveEncryptorAssignSsrcToCodec(encryptor_, video_ssrc_, DAVE_CODEC_H264);
    }
    void json(const Json &p) {
        if (p.contains("seq"))
            seq_ack_ = p["seq"].get<int>();
        int op = p.at("op").get<int>();
        const auto &d = p.at("d");
        if (op == 8) {
            heartbeat_interval_ =
                std::chrono::milliseconds{std::min(5000, int(d.at("heartbeat_interval").get<double>()))};
            heartbeat_ = Clock::now();
        } else if (op == 6)
            last_ack_ = Clock::now();
        else if (op == 2)
            initial(d);
        else if (op == 4) {
            crypto_.key(d.at("secret_key"));
            version_ = d.value("dave_protocol_version", 0);
            if (!version_)
                throw Error("Voice did not negotiate DAVE encryption");
            session_init(version_);
            ready_ = true;
            signal_output();
            wants_ = Clock::now();
            emit_({{"event", "transport_connected"}, {"source", source_}, {"backend", "cpp"}});
        } else if (op == 5) {
            auto id = field_id(d, "user_id");
            recognized_.insert(id);
            auto s = d.value("ssrc", 0U);
            if (s)
                sources_[s] = {id, s, 0, "opus"};
        } else if (op == 11) {
            for (const auto &id : d.value("user_ids", Json::array()))
                recognized_.insert(id_text(id));
        } else if (op == 12) {
            auto id = field_id(d, "user_id");
            recognized_.insert(id);
            selected_video_.erase(id);
            uint64_t best_quality = 0;
            for (auto it = sources_.begin(); it != sources_.end();) {
                if (it->second.user == id && it->second.codec != "opus") {
                    assembly_.erase(it->first);
                    reorder_.erase(it->first);
                    last_frame_.erase(it->first);
                    it = sources_.erase(it);
                } else
                    ++it;
            }
            auto a = d.value("audio_ssrc", 0U);
            if (a)
                sources_[a] = {id, a, 0, "opus"};
            if (d.contains("streams"))
                for (const auto &s : d["streams"]) {
                    if (!s.value("active", true))
                        continue;
                    auto v = s.value("ssrc", 0U), r = s.value("rtx_ssrc", 0U);
                    if (v) {
                        RemoteSource source{id, v, r, "H264"};
                        sources_[v] = source;
                        if (r)
                            sources_[r] = source;
                        const auto res = s.value("max_resolution", Json::object());
                        const uint64_t score = uint64_t(std::max(0, s.value("quality", 0))) * 1000000000ULL +
                                               uint64_t(std::max(0, res.value("width", 0))) *
                                                   std::max(0, res.value("height", 0));
                        if (!selected_video_.contains(id) || score > best_quality) {
                            selected_video_[id] = v;
                            best_quality = score;
                        }
                    }
                }
            else {
                auto v = d.value("video_ssrc", 0U), r = d.value("rtx_ssrc", 0U);
                if (v) {
                    selected_video_[id] = v;
                    RemoteSource source{id, v, r, "H264"};
                    sources_[v] = source;
                    if (r)
                        sources_[r] = source;
                }
            }
            wants_ = Clock::now();
        } else if (op == 13) {
            auto id = field_id(d, "user_id");
            recognized_.erase(id);
            selected_video_.erase(id);
            for (auto i = sources_.begin(); i != sources_.end();)
                if (i->second.user == id) {
                    assembly_.erase(i->first);
                    reorder_.erase(i->first);
                    last_frame_.erase(i->first);
                    i = sources_.erase(i);
                } else
                    ++i;
        } else if (op == 21) {
            auto t = d.at("transition_id").get<int>();
            transitions_[t] = d.at("protocol_version").get<int>();
            if (t)
                transition_ready(t);
            else
                execute(t);
        } else if (op == 22)
            execute(d.at("transition_id").get<int>());
        else if (op == 24 && d.at("epoch").get<int>() == 1)
            session_init(d.at("protocol_version").get<int>());
    }
    Bytes dave_decrypt(const std::string &user, bool video, View in) {
        auto it = decryptors_.find(user);
        if (it == decryptors_.end()) {
            ++decrypt_failed_;
            return {};
        }
        Bytes out(in.size() + 64);
        size_t n = 0;
        auto code = daveDecryptorDecrypt(it->second, video ? DAVE_MEDIA_TYPE_VIDEO : DAVE_MEDIA_TYPE_AUDIO,
                                         in.data(), in.size(), out.data(), out.size(), &n);
        if (code != DAVE_DECRYPTOR_RESULT_CODE_SUCCESS) {
            ++decrypt_failed_;
            return {};
        }
        out.resize(n);
        return out;
    }
    Bytes dave_encrypt(bool video, View in) {
        if (!daveEncryptorHasKeyRatchet(encryptor_) || !version_)
            return {};
        Bytes out(daveEncryptorGetMaxCiphertextByteSize(
            encryptor_, video ? DAVE_MEDIA_TYPE_VIDEO : DAVE_MEDIA_TYPE_AUDIO, in.size()));
        size_t n = 0;
        auto code = daveEncryptorEncrypt(encryptor_, video ? DAVE_MEDIA_TYPE_VIDEO : DAVE_MEDIA_TYPE_AUDIO,
                                         video ? video_ssrc_ : ssrc_, in.data(), in.size(), out.data(),
                                         out.size(), &n);
        if (code != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS)
            throw Error("DAVE frame encryption failed");
        out.resize(n);
        return out;
    }
    Bytes video_extensions(bool repaired) {
        Bytes b;
        auto ext = [&](int id, View bytes) {
            b.push_back(uint8_t((id << 4) | (bytes.size() - 1)));
            append(b, bytes);
        };
        std::array<uint8_t, 3> zero{};
        ext(2, zero);
        auto ticks = uint32_t(
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() *
            262144.0);
        std::array<uint8_t, 3> abs{uint8_t(ticks >> 16), uint8_t(ticks >> 8), uint8_t(ticks)};
        ext(3, abs);
        std::array<uint8_t, 1> orientation{0};
        ext(4, orientation);
        std::array<uint8_t, 2> ts{uint8_t(transport_sequence_ >> 8), uint8_t(transport_sequence_)};
        ++transport_sequence_;
        ext(5, ts);
        ext(6, zero);
        std::array<uint8_t, 1> content{uint8_t(screen_ ? 1 : 0)};
        ext(7, content);
        auto type = screen_ ? std::string("screen") : std::string("video");
        ext(10, View(reinterpret_cast<const uint8_t *>(type.data()), type.size()));
        std::array<uint8_t, 3> rid{'1', '0', '0'};
        ext(repaired ? 12 : 11, rid);
        while (b.size() % 4)
            b.push_back(0);
        return b;
    }
    void rtp(uint32_t ssrc, uint16_t seq, uint32_t timestamp, uint8_t pt, bool marker, View payload,
             bool video, bool repaired = false) {
        Bytes h{uint8_t(video ? 0x90 : 0x80), uint8_t(pt | (marker ? 0x80 : 0))};
        put16(h, seq);
        put32(h, timestamp);
        put32(h, ssrc);
        Bytes body;
        if (video) {
            auto ext = video_extensions(repaired);
            put16(h, 0xBEDE);
            put16(h, uint16_t(ext.size() / 4));
            append(body, ext);
        }
        append(body, payload);
        udp_send(crypto_.encrypt(h, body));
        if (video && !repaired) {
            ++sr_video_packets_;
            sr_video_bytes_ += uint32_t(payload.size());
            retransmit_.push_back({seq, timestamp, pt, marker, Bytes(payload.begin(), payload.end())});
            if (retransmit_.size() > 2048)
                retransmit_.pop_front();
        } else if (!video) {
            ++sr_audio_packets_;
            sr_audio_bytes_ += uint32_t(payload.size());
        }
    }
    void rtcp(View packet) {
        if (packet.size() < 8)
            return;
        auto body = crypto_.decrypt(packet, 8);
        if (body.empty())
            return;
        Bytes p(packet.begin(), packet.begin() + 8);
        append(p, body);
        for (size_t off = 0; off + 4 <= p.size();) {
            auto size = (u16(p, off + 2) + 1) * 4U;
            if (size < 4 || off + size > p.size())
                break;
            int pt = p[off + 1], fmt = p[off] & 31;
            if (pt == 205 && fmt == 1 && size >= 16 && u32(p, off + 8) == video_ssrc_) {
                for (size_t i = off + 12; i + 4 <= off + size; i += 4) {
                    auto pid = u16(p, i), mask = u16(p, i + 2);
                    for (int bit = -1; bit < 16; ++bit) {
                        if (bit >= 0 && !(mask & (1 << bit)))
                            continue;
                        uint16_t seq = uint16_t(pid + (bit + 1));
                        auto found = std::find_if(retransmit_.rbegin(), retransmit_.rend(),
                                                  [&](auto &c) { return c.seq == seq; });
                        if (found == retransmit_.rend())
                            continue;
                        Bytes payload;
                        put16(payload, seq);
                        append(payload, found->payload);
                        rtp(rtx_ssrc_, rseq_++, found->timestamp, 102, found->marker, payload, true, true);
                        ++rtx_sent_;
                    }
                }
            } else if (pt == 201 || pt == 200) {
                if (pt == 200 && size >= 28) {
                    auto &report = reception_[u32(p, off + 4)];
                    report.lsr = (u32(p, off + 8) << 16) | (u32(p, off + 12) >> 16);
                    report.sr_arrival = Clock::now();
                }
                const size_t first = off + (pt == 200 ? 28 : 8);
                for (size_t i = first; i + 24 <= off + size; i += 24) {
                    const auto raw_loss = (uint32_t(p[i + 5]) << 16) | (uint32_t(p[i + 6]) << 8) | p[i + 7];
                    receiver_reports_.push_back({{"source_ssrc", u32(p, i)},
                                                 {"fraction_lost", p[i + 4]},
                                                 {"cumulative_lost", int32_t(raw_loss << 8) >> 8},
                                                 {"extended_high_sequence", u32(p, i + 8)},
                                                 {"jitter", u32(p, i + 12)},
                                                 {"last_sender_report", u32(p, i + 16)},
                                                 {"delay_since_last_sender_report", u32(p, i + 20)}});
                    if (receiver_reports_.size() > 16)
                        receiver_reports_.erase(receiver_reports_.begin());
                }
            } else if (pt == 206 && (fmt == 1 || fmt == 4))
                force_keyframe = true;
            off += size;
        }
    }
    void udp_receive() {
        std::array<uint8_t, 65536> buffer{};
        for (int burst = 0; burst < 256; ++burst) {
            int n = recv(udp_, reinterpret_cast<char *>(buffer.data()), int(buffer.size()), 0);
            if (n <= 0)
                break;
            ++udp_packets_;
            View p(buffer.data(), size_t(n));
            if (n < 16 || p[0] >> 6 != 2)
                continue;
            if (p[1] >= 192 && p[1] <= 223) {
                rtcp(p);
                continue;
            }
            size_t h = 12 + (p[0] & 15) * 4, ext = 0;
            ++payload_packets_[p[1] & 127];
            if (h > p.size())
                continue;
            if (p[0] & 16) {
                if (h + 4 > p.size())
                    continue;
                ext = u16(p, h + 2) * 4;
                h += 4;
            }
            auto body = crypto_.decrypt(p, h);
            if (body.empty()) {
                ++transport_decrypt_failures_;
                continue;
            }
            if (body.size() < ext)
                continue;
            if (p[0] & 32) {
                if (body.empty() || !body.back() || body.back() > body.size())
                    continue;
                body.resize(body.size() - body.back());
            }
            if (ext > body.size())
                continue;
            View payload(body.data() + ext, body.size() - ext);
            auto ssrc = u32(p, 8), timestamp = u32(p, 4);
            auto seq = u16(p, 2);
            auto pt = p[1] & 127;
            bool marker = p[1] & 128;
            auto mapped = sources_.find(ssrc);
            if (mapped == sources_.end()) {
                ++unmapped_rtp_;
                continue;
            }
            auto source = mapped->second;
            if (source.user == self_)
                continue;
            bool video = pt != 120;
            if (!video && !options_.receive_audio)
                continue;
            if (video && !options_.receive_video)
                continue;
            if (pt == 102 || pt == 104) {
                if (payload.size() < 2)
                    continue;
                seq = u16(payload);
                payload = payload.subspan(2);
                ssrc = source.primary;
                ++rtx_received_;
                pt -= 1;
            }
            if (pt != 120 && pt != 101 && pt != 103)
                continue;
            if (reorder_.size() >= 128 && !reorder_.contains(ssrc))
                continue;
            reorder_[ssrc].push({ssrc, timestamp, seq, pt, marker, source.user,
                                 Bytes(payload.begin(), payload.end()), Clock::now()});
        }
        for (auto &[ssrc, order] : reorder_)
            order.drain(
                Clock::now(), [&](OrderedRtp packet) { ordered(std::move(packet)); },
                [&](uint16_t seq) {
                    if (sources_[ssrc].codec == "opus")
                        return;
                    Bytes p{0x81, 205, 0, 3};
                    put32(p, ssrc_);
                    put32(p, ssrc);
                    put16(p, seq);
                    put16(p, 0);
                    udp_send(crypto_.encrypt(View(p).first(8), View(p).subspan(8)));
                });
    }
    void ordered(OrderedRtp packet) {
        auto ssrc = packet.ssrc, timestamp = packet.timestamp;
        auto seq = packet.sequence;
        int pt = packet.payload_type;
        bool marker = packet.marker;
        View payload(packet.payload);
        bool video = pt != 120;
        auto &timing = reception_[ssrc];
        if (timing.last_arrival != Clock::time_point{}) {
            const auto delta = std::chrono::duration<double>(packet.arrival - timing.last_arrival).count() *
                                   (video ? 90000 : 48000) -
                               int32_t(timestamp - timing.last_timestamp);
            timing.jitter += (std::abs(delta) - timing.jitter) / 16;
        }
        timing.last_arrival = packet.arrival;
        timing.last_timestamp = timestamp;
        if (!video) {
            auto clear = dave_decrypt(packet.user, false, payload);
            if (!clear.empty()) {
                ++received_;
                last_frame_[ssrc] = packet.arrival;
                frame_({source_, packet.user, "opus", ssrc, timestamp, std::move(clear), packet.arrival});
            }
            return;
        }
        if (pt != 101 && pt != 103)
            return;
        bool h265 = pt == 103;
        sources_[ssrc].codec = h265 ? "H265" : "H264";
        auto &a = assembly_[ssrc];
        if (!a.initialized || a.timestamp != timestamp) {
            a.data.clear();
            a.broken = false;
            a.timestamp = timestamp;
            a.next = seq;
            a.initialized = true;
        }
        if (int16_t(seq - a.next) < 0)
            return;
        if (seq != a.next) {
            a.broken = true;
            ++broken_frames_;
        }
        a.next = uint16_t(seq + 1);
        if (a.broken || payload.empty())
            return;
        auto type = h265 ? (payload[0] >> 1) & 63 : payload[0] & 31;
        if ((!h265 && type >= 1 && type <= 23) || (h265 && type <= 47))
            nal_append(a.data, payload);
        else if ((!h265 && type == 24) || (h265 && type == 48)) {
            size_t pos = h265 ? 2 : 1;
            while (pos + 2 <= payload.size()) {
                size_t len = u16(payload, pos);
                pos += 2;
                if (!len || pos + len > payload.size()) {
                    a.broken = true;
                    break;
                }
                nal_append(a.data, payload.subspan(pos, len));
                pos += len;
            }
        } else if ((!h265 && type == 28) || (h265 && type == 49)) {
            size_t hdr = h265 ? 3 : 2;
            if (payload.size() < hdr) {
                a.broken = true;
                return;
            }
            uint8_t fu = payload[hdr - 1];
            if (fu & 0x80) {
                a.data.insert(a.data.end(), {0, 0, 0, 1});
                if (h265) {
                    a.data.push_back(uint8_t((payload[0] & 0x81) | ((fu & 63) << 1)));
                    a.data.push_back(payload[1]);
                } else
                    a.data.push_back(uint8_t((payload[0] & 0xE0) | (fu & 31)));
            } else if (a.data.empty()) {
                a.broken = true;
                return;
            }
            append(a.data, payload.subspan(hdr));
        } else
            a.broken = true;
        if (a.data.size() > 8 * 1024 * 1024) {
            a.data.clear();
            a.broken = true;
        }
        if (marker && !a.broken && !a.data.empty()) {
            auto clear = dave_decrypt(packet.user, true, a.data);
            if (!clear.empty()) {
                ++received_;
                last_frame_[ssrc] = packet.arrival;
                frame_({source_, packet.user, h265 ? "H265" : "H264", ssrc, timestamp, std::move(clear),
                        packet.arrival});
            }
            a.data.clear();
        }
    }

    void sender_report(bool video) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(now);
        auto frac = std::chrono::duration<double>(now - sec).count();
        Bytes p{0x80, 200, 0, 6};
        put32(p, video ? video_ssrc_ : ssrc_);
        put32(p, uint32_t(sec.count() + 2208988800ULL));
        put32(p, uint32_t(frac * 4294967296.0));
        put32(p, video ? video_ts_ : audio_ts_);
        put32(p, video ? sr_video_packets_ : sr_audio_packets_);
        put32(p, video ? sr_video_bytes_ : sr_audio_bytes_);
        udp_send(crypto_.encrypt(View(p).first(8), View(p).subspan(8)));
    }
    void receiver_report() {
        Bytes blocks;
        int count = 0;
        auto flush = [&] {
            if (!count)
                return;
            Bytes p{uint8_t(0x80 | count), 201};
            put16(p, uint16_t(1 + 6 * count));
            put32(p, ssrc_);
            append(p, blocks);
            udp_send(crypto_.encrypt(View(p).first(8), View(p).subspan(8)));
            ++receiver_reports_sent_;
            blocks.clear();
            count = 0;
        };
        for (const auto &[ssrc, order] : reorder_) {
            if (!order.delivered || !sources_.contains(ssrc))
                continue;
            auto &state = reception_[ssrc];
            const auto total = order.delivered + order.skipped;
            const auto expected = total - state.prior_total, lost = order.skipped - state.prior_lost;
            put32(blocks, ssrc);
            blocks.push_back(uint8_t(expected ? std::min<uint64_t>(255, lost * 256 / expected) : 0));
            const auto cumulative = uint32_t(std::min<uint64_t>(order.skipped, 0x7FFFFF));
            blocks.push_back(uint8_t(cumulative >> 16));
            blocks.push_back(uint8_t(cumulative >> 8));
            blocks.push_back(uint8_t(cumulative));
            put32(blocks, order.extended_high_sequence());
            put32(blocks, uint32_t(std::clamp(state.jitter, 0.0, 4294967295.0)));
            put32(blocks, state.lsr);
            const auto delay =
                state.sr_arrival == Clock::time_point{}
                    ? 0.0
                    : std::chrono::duration<double>(Clock::now() - state.sr_arrival).count() * 65536;
            put32(blocks, uint32_t(std::clamp(delay, 0.0, 4294967295.0)));
            state.prior_total = total;
            state.prior_lost = order.skipped;
            if (++count == 31)
                flush();
        }
        flush();
    }
    void request_video() {
        auto wants = receiver_wants(sources_, selected_video_, self_, options_.receive_audio,
                                    options_.receive_video, options_.width * options_.height);
        for (const auto &[ssrc, source] : sources_)
            if (source.codec != "opus" && wants.contains(std::to_string(ssrc)) &&
                (!last_frame_.contains(ssrc) ||
                 Clock::now() - last_frame_.at(ssrc) > std::chrono::milliseconds{500}))
                pli(ssrc);
        ws_->send(Json{{"op", 15}, {"d", wants}});
    }

  public:
    bool force_keyframe = false;
    void set_audio_state(bool receive_audio, bool muted) {
        if (options_.receive_audio != receive_audio) {
            options_.receive_audio = receive_audio;
            wants_ = Clock::time_point{};
        }
        if (options_.muted != muted) {
            options_.muted = muted;
            if (ready_)
                signal_output();
        }
    }
    void set_sources(const MediaOptions &options) {
        options_ = options;
        last_sent_ = {};
        video_intervals_.clear();
        if (ready_)
            signal_output();
        force_keyframe = true;
    }
    Voice(std::string endpoint, std::string token, std::string server, std::string channel,
          std::string session, std::string self, std::string source, std::string group, bool output,
          bool screen, const MediaOptions &options, std::atomic_bool &stop, std::set<std::string> recognized,
          std::function<void(ReceivedFrame)> frame, MediaSession::Handler emit)
        : stop_(stop), options_(options), recognized_(std::move(recognized)), self_(std::move(self)),
          server_(std::move(server)), channel_(std::move(channel)), group_(std::move(group)),
          source_(std::move(source)), output_(output), screen_(screen), frame_(std::move(frame)),
          emit_(std::move(emit)) {
        recognized_.insert(self_);
        session_ = daveSessionCreate(nullptr, nullptr, nullptr, nullptr);
        encryptor_ = daveEncryptorCreate();
        if (!session_ || !encryptor_)
            throw Error("Cannot create native DAVE session");
        if (endpoint.starts_with("wss://"))
            endpoint.erase(0, 6);
        if (endpoint.ends_with(":443"))
            endpoint.resize(endpoint.size() - 4);
        if (endpoint.empty() || endpoint.find_first_of("/\\?#@\r\n") != std::string::npos)
            throw Error("Invalid voice endpoint");
        ws_ = std::make_unique<SocketWeb>("wss://" + endpoint + "/?v=9", stop_,
                                          long(options.connect_timeout.count()));
        Json streams = Json::array();
        if (output_)
            streams.push_back(stream_description());
        ws_->send(Json{{"op", 0},
                       {"d",
                        {{"server_id", server_},
                         {"channel_id", channel_},
                         {"user_id", self_},
                         {"session_id", session},
                         {"token", token},
                         {"video", true},
                         {"streams", streams},
                         {"max_dave_protocol_version", daveMaxSupportedProtocolVersion()}}}});
    }
    ~Voice() {
#ifdef _WIN32
        if (udp_ != INVALID_SOCKET)
            closesocket(udp_);
#else
        if (udp_ >= 0)
            close(udp_);
#endif
        for (auto &[_, d] : decryptors_)
            daveDecryptorDestroy(d);
        if (encryptor_)
            daveEncryptorDestroy(encryptor_);
        if (session_)
            daveSessionDestroy(session_);
    }
    void recognize(const std::set<std::string> &users) { recognized_.insert(users.begin(), users.end()); }
    void pli(uint32_t target) {
        if (!crypto_.ready())
            return;
        Bytes p{0x81, 206, 0, 2};
        put32(p, ssrc_);
        put32(p, target);
        udp_send(crypto_.encrypt(View(p).first(8), View(p).subspan(8)));
    }
    void keyframes() {
        for (auto &[s, r] : sources_)
            if (s == r.primary && r.codec != "opus" && selected_video_[r.user] == s)
                pli(s);
    }
    void tick() {
        for (int i = 0; i < 64; ++i) {
            auto p = ws_->receive();
            if (!p)
                break;
            if (p->second)
                binary(p->first);
            else
                json(Json::parse(p->first));
        }
        discover();
        auto now = Clock::now();
        if (now >= heartbeat_) {
            ws_->send(Json{{"op", 3},
                           {"d",
                            {{"t", std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count()},
                             {"seq_ack", seq_ack_}}}});
            heartbeat_ = now + heartbeat_interval_;
        }
        if (now - last_ack_ > std::chrono::seconds{30} && ready_)
            throw Error("Voice heartbeat acknowledgement timed out");
        if (!ready_ && now - started_ > options_.connect_timeout)
            throw Error("Voice session startup timed out");
        if (crypto_.ready()) {
            udp_receive();
            if (now >= wants_) {
                request_video();
                wants_ = now + std::chrono::seconds{3};
            }
            if (now >= sr_due_) {
                receiver_report();
                if (sr_audio_packets_)
                    sender_report(false);
                if (sr_video_packets_)
                    sender_report(true);
                sr_due_ = now + std::chrono::seconds{1};
            }
        }
    }
    bool can_send() const { return output_ && ready_ && version_ && daveEncryptorHasKeyRatchet(encryptor_); }
    bool connected() const { return ready_; }
    void audio(View clear) {
        if (!can_send() || !audio_output_enabled(options_, screen_))
            return;
        auto data = dave_encrypt(false, clear);
        if (data.empty())
            return;
        rtp(ssrc_, aseq_++, audio_ts_, 120, true, data, false);
        audio_ts_ += 960;
        ++audio_sent_;
    }
    void video(View clear) {
        if (!can_send()) {
            last_sent_ = {};
            video_intervals_.clear();
            return;
        }
        auto data = dave_encrypt(true, clear);
        if (data.empty())
            return;
        const auto sent = Clock::now();
        if (last_sent_ != Clock::time_point{}) {
            video_intervals_.push_back(std::chrono::duration<double, std::milli>(sent - last_sent_).count());
            if (video_intervals_.size() > 300)
                video_intervals_.pop_front();
        }
        last_sent_ = sent;
        auto units = nals(data);
        size_t max = 1100;
        for (size_t i = 0; i < units.size(); ++i) {
            const auto &nal = units[i];
            if (nal.empty())
                continue;
            if (nal.size() <= max)
                rtp(video_ssrc_, vseq_++, video_ts_, 101, i + 1 == units.size(), nal, true);
            else {
                size_t pos = 1;
                while (pos < nal.size()) {
                    size_t count = std::min(max - 2, nal.size() - pos);
                    bool end = pos + count == nal.size();
                    Bytes p;
                    p.reserve(count + 2);
                    p.push_back(uint8_t((nal[0] & 0xE0) | 28));
                    p.push_back(uint8_t((nal[0] & 31) | (pos == 1 ? 0x80 : 0) | (end ? 0x40 : 0)));
                    append(p, View(nal).subspan(pos, count));
                    rtp(video_ssrc_, vseq_++, video_ts_, 101, end && i + 1 == units.size(), p, true);
                    pos += count;
                }
            }
        }
        video_ts_ += uint32_t(90000 / options_.fps);
        ++video_sent_;
    }
    Json snapshot() const {
        Json payload_types = Json::object();
        for (size_t i = 0; i < payload_packets_.size(); ++i)
            if (payload_packets_[i])
                payload_types[std::to_string(i)] = payload_packets_[i];
        std::vector<double> intervals(video_intervals_.begin(), video_intervals_.end());
        double interval_mean = 0;
        for (auto v : intervals)
            interval_mean += v;
        if (!intervals.empty())
            interval_mean /= intervals.size();
        std::sort(intervals.begin(), intervals.end());
        Json sources = Json::array();
        for (auto &[s, r] : sources_)
            if (s == r.primary)
                sources.push_back(
                    {{"ssrc", s}, {"user_id", r.user}, {"codec", r.codec}, {"rtx_ssrc", r.rtx}});
        return {
            {"connected", ready_},
            {"encrypted", can_send() || (!output_ && !decryptors_.empty())},
            {"dave_protocol_version", version_},
            {"video_frames_sent", video_sent_},
            {"video_interval_mean_ms", interval_mean},
            {"video_interval_p95_ms", intervals.empty() ? 0.0 : intervals[(intervals.size() - 1) * 95 / 100]},
            {"video_interval_max_ms", intervals.empty() ? 0.0 : intervals.back()},
            {"receiver_reports", receiver_reports_},
            {"receiver_reports_sent", receiver_reports_sent_},
            {"audio_frames_sent", audio_sent_},
            {"received_frames", received_},
            {"decrypt_failures", decrypt_failed_},
            {"udp_packets_received", udp_packets_},
            {"udp_send_errors", udp_errors_},
            {"rtp_payload_packets", payload_types},
            {"unmapped_rtp_packets", unmapped_rtp_},
            {"transport_decrypt_failures", transport_decrypt_failures_},
            {"rtx_received", rtx_received_},
            {"rtx_sent", rtx_sent_},
            {"broken_frames", broken_frames_},
            {"sources", sources}};
    }
};
} // namespace discord::detail::media
