// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <discord/discord.hpp>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <zlib.h>

namespace discord::detail {
using Clock = std::chrono::steady_clock;
struct Request { std::string method; std::string path; Json body; };
struct Response {
    int status = 0;
    std::string body;
    std::map<std::string, std::string, std::less<>> headers;
};
using Transport = std::function<Response(const Request&)>;
void init_curl();
[[nodiscard]] std::string encode_path(std::string_view value);
[[nodiscard]] std::string route_key(const Request& request);
void interruptible_wait(std::chrono::milliseconds delay, const std::atomic_bool& stop);

class Rest {
public:
    explicit Rest(ClientOptions options, Transport transport = {});
    void login(std::string token);
    Json request(std::string method, std::string path, Json body = nullptr);
    void cancel() noexcept { cancelled_ = true; }
private:
    Response perform(const Request& request);
    ClientOptions options_;
    Transport transport_;
    std::string token_;
    std::mutex mutex_;
    std::atomic_bool cancelled_{false};
    std::map<std::string, Clock::time_point> blocked_;
    std::map<std::string, std::string> buckets_;
    Clock::time_point global_until_{};
};

class Inflater {
public:
    Inflater();
    ~Inflater();
    Inflater(const Inflater&) = delete;
    Inflater& operator=(const Inflater&) = delete;
    std::vector<std::string> push(std::string_view data);
private:
    z_stream stream_{};
    std::string pending_;
};

class Gateway {
public:
    Gateway(ClientOptions options, std::string token, std::atomic_bool& stop,
            std::atomic<std::int64_t>& latency, std::function<void(RawEvent)> emit,
            std::function<std::vector<Json>()> outbound = {});
    void run();
private:
    void connect();
    ClientOptions options_;
    std::string token_;
    std::atomic_bool& stop_;
    std::atomic<std::int64_t>& latency_;
    std::function<void(RawEvent)> emit_;
    std::function<std::vector<Json>()> outbound_;
    std::string session_;
    std::string resume_url_;
    std::optional<std::int64_t> sequence_;
    bool established_ = false;
};

// FIFO insertion order: updates never insert duplicate eviction entries.
class MessageCache {
public:
    explicit MessageCache(std::size_t capacity) : capacity_(capacity) {}
    void insert(Json data);
    std::optional<Json> get(Snowflake id) const;
    void erase(Snowflake id);
    void clear();
private:
    std::size_t capacity_;
    std::unordered_map<std::uint64_t, Json> values_;
    std::deque<std::uint64_t> order_;
};
std::vector<std::string> split_arguments(std::string_view input);
}
