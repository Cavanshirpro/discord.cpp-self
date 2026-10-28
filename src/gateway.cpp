// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#include <cerrno>
#endif
#include "internal.hpp"
#include <curl/curl.h>
#include <array>
#include <cmath>
#include <random>

namespace discord::detail {
namespace {
constexpr std::size_t payload_limit = 16 * 1024 * 1024;
struct Reconnect {};
struct CurlDeleter { void operator()(CURL* value) const { curl_easy_cleanup(value); } };
int progress(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::atomic_bool*>(userdata)->load() ? 1 : 0;
}
void wait_socket(CURL* curl, bool writing = false) {
    curl_socket_t socket = CURL_SOCKET_BAD;
    if (curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket) != CURLE_OK || socket == CURL_SOCKET_BAD)
        throw TransportError("Gateway socket unavailable");
#ifdef _WIN32
    fd_set sockets;
    FD_ZERO(&sockets);
    FD_SET(socket, &sockets);
    timeval timeout{0, 50'000};
    if (select(static_cast<int>(socket + 1), writing ? nullptr : &sockets,
        writing ? &sockets : nullptr, nullptr, &timeout) < 0)
        throw TransportError("Could not poll Gateway socket");
#else
    pollfd descriptor{socket, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    if (::poll(&descriptor, 1, 50) < 0 && errno != EINTR)
        throw TransportError("Could not poll Gateway socket");
#endif
}
void send(CURL* curl, const Json& data, const std::atomic_bool& stop) {
    const auto text = data.dump();
    const auto deadline = Clock::now() + std::chrono::seconds{10};
    std::size_t offset = 0;
    do {
        if (stop) throw Cancelled("Client closed");
        if (Clock::now() >= deadline) throw TransportError("Gateway send timed out");
        std::size_t sent = 0;
        const auto result = curl_ws_send(curl, text.data() + offset, text.size() - offset, &sent, 0, CURLWS_TEXT);
        offset += sent;
        if (result != CURLE_OK && result != CURLE_AGAIN)
            throw TransportError(std::string("Gateway send: ") + curl_easy_strerror(result));
        if (offset < text.size()) wait_socket(curl, true);
    } while (offset < text.size());
}
bool fatal(int code) {
    return code == 4004 || code == 4010 || code == 4011 || code == 4012 || code == 4013 || code == 4014;
}
double jitter() {
    thread_local std::mt19937 random(std::random_device{}());
    return std::uniform_real_distribution<double>{0.0, 1.0}(random);
}
std::string gateway_address(const std::string& base) {
    if ((!base.starts_with("wss://") && !base.starts_with("ws://")) || base.find_first_of("\r\n#") != std::string::npos)
        throw ProtocolError("Invalid Gateway URL");
    return base.substr(0, base.find('?')) + "?v=9&encoding=json&compress=zlib-stream";
}
}
Inflater::Inflater() {
    if (inflateInit(&stream_) != Z_OK) throw ProtocolError("Could not initialize Gateway decompression");
}
Inflater::~Inflater() { inflateEnd(&stream_); }
std::vector<std::string> Inflater::push(std::string_view data) {
    if (data.size() > payload_limit - pending_.size()) throw ProtocolError("Compressed Gateway payload exceeds 16 MiB");
    pending_.append(data);
    std::vector<std::string> messages;
    const std::string marker("\0\0\xff\xff", 4);
    for (;;) {
        const auto end = pending_.find(marker);
        if (end == std::string::npos) break;
        const auto length = end + marker.size();
        stream_.next_in = reinterpret_cast<Bytef*>(pending_.data());
        stream_.avail_in = static_cast<uInt>(length);
        std::string output;
        std::array<char, 16'384> buffer{};
        do {
            stream_.next_out = reinterpret_cast<Bytef*>(buffer.data());
            stream_.avail_out = static_cast<uInt>(buffer.size());
            const auto result = inflate(&stream_, Z_SYNC_FLUSH);
            if (result != Z_OK && result != Z_BUF_ERROR) throw ProtocolError("Invalid zlib-stream Gateway payload");
            const auto produced = buffer.size() - stream_.avail_out;
            if (produced > payload_limit - output.size()) throw ProtocolError("Gateway payload exceeds 16 MiB");
            output.append(buffer.data(), produced);
            if (result == Z_BUF_ERROR && stream_.avail_in != 0) throw ProtocolError("Stalled Gateway decompression");
        } while (stream_.avail_in != 0 || stream_.avail_out == 0);
        pending_.erase(0, length);
        messages.push_back(std::move(output));
    }
    return messages;
}
Gateway::Gateway(ClientOptions options, std::string token, std::atomic_bool& stop,
                 std::atomic<std::int64_t>& latency, std::function<void(RawEvent)> emit,
                 std::function<std::vector<Json>()> outbound)
    : options_(std::move(options)), token_(std::move(token)), stop_(stop), latency_(latency), emit_(std::move(emit)), outbound_(std::move(outbound)) {}

void Gateway::run() {
    init_curl();
    const auto* info = curl_version_info(CURLVERSION_NOW);
    bool supported = false;
    for (auto protocol = info->protocols; protocol && *protocol; ++protocol)
        if (std::string_view(*protocol) == "wss") supported = true;
    if (!supported) throw ProtocolError("libcurl was built without WebSocket/TLS support; install a build with ws and wss protocols");
    int failures = 0;
    while (!stop_) {
        established_ = false;
        std::exception_ptr failure;
        try { connect(); }
        catch (const Cancelled&) { return; }
        catch (const GatewayError& error) {
            if (fatal(error.code)) throw;
            failure = std::current_exception();
            if (error.code == 4007 || error.code == 4009) { session_.clear(); sequence_.reset(); resume_url_.clear(); }
        }
        catch (const Reconnect&) {}
        catch (const TransportError&) { failure = std::current_exception(); }
        if (stop_) return;
        if (established_) failures = 0;
        if (++failures > options_.max_reconnect_attempts) {
            if (failure) std::rethrow_exception(failure);
            throw TransportError("Gateway reconnect budget exhausted");
        }
        latency_ = -1;
        const auto delay = std::chrono::milliseconds{static_cast<std::int64_t>(
            std::min(30'000.0, 500.0 * (1 << std::min(failures - 1, 6))) + jitter() * 250.0)};
        try { interruptible_wait(delay, stop_); } catch (const Cancelled&) { return; }
    }
}
void Gateway::connect() {
    std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
    if (!curl) throw TransportError("Could not create Gateway connection");
    const auto url = gateway_address(resume_url_.empty() ? options_.gateway_url : resume_url_);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "ws,wss");
    curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "discord.cpp-self/0.1.0");
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options_.request_timeout.count()));
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(options_.request_timeout.count()));
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &stop_);
    const auto result = curl_easy_perform(curl.get());
    if (stop_) throw Cancelled("Client closed");
    if (result != CURLE_OK) throw TransportError(std::string("Gateway connect: ") + curl_easy_strerror(result));

    Inflater inflater;
    std::string frame, close_frame;
    std::array<char, 16'384> buffer{};
    auto hello_deadline = Clock::now() + options_.request_timeout;
    auto ready_deadline = hello_deadline;
    auto heartbeat_due = Clock::time_point::max();
    auto sent_at = Clock::time_point{};
    std::chrono::milliseconds interval{0};
    bool hello = false, waiting_ack = false;
    auto heartbeat = [&] {
        send(curl.get(), {{"op", 1}, {"d", sequence_ ? Json(*sequence_) : Json(nullptr)}}, stop_);
        if (!waiting_ack) sent_at = Clock::now();
        waiting_ack = true;
    };
    auto receive = [&](const std::string& text) {
        const auto payload = Json::parse(text, nullptr, false);
        if (!payload.is_object() || !payload.contains("op") || !payload["op"].is_number_integer())
            throw ProtocolError("Invalid Gateway payload");
        if (payload.contains("s") && !payload["s"].is_null()) sequence_ = payload["s"].get<std::int64_t>();
        const int opcode = payload["op"].get<int>();
        const auto data = payload.value("d", Json(nullptr));
        switch (opcode) {
        case 10: {
            if (hello) throw ProtocolError("Duplicate Gateway HELLO");
            const auto milliseconds = data.at("heartbeat_interval").get<double>();
            if (!std::isfinite(milliseconds) || milliseconds < 1 || milliseconds > 300'000)
                throw ProtocolError("Invalid Gateway heartbeat interval");
            interval = std::chrono::milliseconds{static_cast<std::int64_t>(milliseconds)};
            heartbeat_due = Clock::now() + std::chrono::milliseconds{static_cast<std::int64_t>(milliseconds * jitter())};
            hello = true;
            ready_deadline = Clock::now() + options_.request_timeout;
            if (!session_.empty() && sequence_) {
                send(curl.get(), {{"op", 6}, {"d", {{"token", token_}, {"session_id", session_}, {"seq", *sequence_}}}}, stop_);
            } else {
#ifdef _WIN32
                const auto os = "Windows";
#elif defined(__APPLE__)
                const auto os = "Mac OS X";
#else
                const auto os = "Linux";
#endif
                send(curl.get(), {{"op", 2}, {"d", {
                    {"token", token_}, {"compress", false}, {"capabilities", 0},
                    {"properties", {{"os", os}, {"browser", "discord.cpp-self"}, {"device", "discord.cpp-self"}}},
                    // The user Gateway accepts unknown for initial presence;
                    // connecting must not force the account's status to online.
                    {"presence", {{"status", "unknown"}, {"since", 0}, {"activities", Json::array()}, {"afk", false}}},
                    {"client_state", {{"guild_versions", Json::object()}}}}}}, stop_);
            }
            break;
        }
        case 11:
            if (waiting_ack) latency_ = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - sent_at).count();
            waiting_ack = false;
            break;
        case 1: heartbeat(); break;
        case 7: throw Reconnect{};
        case 9:
            if (!data.is_boolean() || !data.get<bool>()) { session_.clear(); sequence_.reset(); resume_url_.clear(); }
            interruptible_wait(std::chrono::milliseconds{1'000 + static_cast<int>(jitter() * 4'000)}, stop_);
            throw Reconnect{};
        case 0: {
            const auto name = payload.at("t").get<std::string>();
            if (name == "READY") {
                session_ = data.at("session_id").get<std::string>();
                resume_url_ = data.value("resume_gateway_url", options_.gateway_url);
                // A server-provided resume address must not downgrade a TLS connection.
                if (options_.gateway_url.starts_with("wss://") && !resume_url_.starts_with("wss://"))
                    throw ProtocolError("Gateway supplied an insecure resume URL");
                established_ = true;
            } else if (name == "RESUMED") established_ = true;
            emit_({name, data});
            break;
        }
        default: break; // Unknown opcodes remain forward compatible.
        }
    };
    while (!stop_) {
        if (!hello && Clock::now() > hello_deadline) throw TransportError("Gateway HELLO timed out");
        if (established_ && outbound_) for (const auto& packet : outbound_()) send(curl.get(), packet, stop_);
        if (hello && !established_ && Clock::now() > ready_deadline) throw TransportError("Gateway READY/RESUMED timed out");
        if (Clock::now() >= heartbeat_due) {
            if (waiting_ack) throw TransportError("Gateway heartbeat acknowledgement timed out");
            heartbeat();
            heartbeat_due = Clock::now() + interval;
        }
        std::size_t received = 0;
        const curl_ws_frame* meta = nullptr;
        const auto code = curl_ws_recv(curl.get(), buffer.data(), buffer.size(), &received, &meta);
        if (code == CURLE_AGAIN) { wait_socket(curl.get()); continue; }
        if (code != CURLE_OK) throw TransportError(std::string("Gateway receive: ") + curl_easy_strerror(code));
        if (!meta) throw ProtocolError("Missing WebSocket metadata");
        if (meta->flags & CURLWS_CLOSE) {
            close_frame.append(buffer.data(), received);
            if (meta->bytesleft == 0) {
                int close_code = 1005;
                if (close_frame.size() >= 2)
                    close_code = (static_cast<unsigned char>(close_frame[0]) << 8) | static_cast<unsigned char>(close_frame[1]);
                throw GatewayError(close_code);
            }
            continue;
        }
        if (meta->flags & (CURLWS_PING | CURLWS_PONG)) continue; // libcurl automatically answers PING.
        if (received > payload_limit - frame.size()) throw ProtocolError("WebSocket message exceeds 16 MiB");
        frame.append(buffer.data(), received);
        if (meta->bytesleft != 0 || (meta->flags & CURLWS_CONT)) continue;
        if (meta->flags & CURLWS_BINARY) {
            for (const auto& text : inflater.push(frame)) receive(text);
        } else receive(frame);
        frame.clear();
    }
    // Best effort, bounded by nonblocking libcurl; cleanup releases the socket.
    const unsigned char normal_close[]{0x03, 0xE8};
    std::size_t sent = 0;
    (void)curl_ws_send(curl.get(), normal_close, 2, &sent, 0, CURLWS_CLOSE);
}
}
