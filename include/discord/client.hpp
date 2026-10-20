// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#pragma once
#include "errors.hpp"
#include "models.hpp"
#include <functional>

namespace discord {
struct ClientOptions {
    std::string api_url = "https://discord.com/api/v9";
    std::string gateway_url = "wss://gateway.discord.gg/";
    std::chrono::milliseconds request_timeout{30'000};
    std::chrono::milliseconds max_rate_limit_wait{60'000};
    std::size_t message_cache_size = 1'000;
    std::size_t event_queue_size = 4'096;
    int max_reconnect_attempts = 8;
    int max_rate_limit_retries = 5;
};

struct Ready { User user; std::vector<Guild> guilds; };
struct MessageUpdate {
    Snowflake id;
    Snowflake channel_id;
    std::optional<Message> before;
    std::optional<Message> after; // Available if cached, otherwise use patch.
    Json patch;
};
struct MessageDelete {
    Snowflake id;
    Snowflake channel_id;
    std::optional<Message> cached;
};
struct RawEvent { std::string name; Json data; };

class Client {
public:
    explicit Client(ClientOptions options = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    Client& on_ready(std::function<void(const Ready&)> handler);
    Client& on_message(std::function<void(const Message&)> handler);
    Client& on_message_edit(std::function<void(const MessageUpdate&)> handler);
    Client& on_message_delete(std::function<void(const MessageDelete&)> handler);
    Client& on_raw_event(std::function<void(const RawEvent&)> handler);
    Client& on_resumed(std::function<void()> handler);
    Client& on_error(std::function<void(const std::exception&)> handler);

    // Configure handlers before run(). Callbacks execute serially on its thread.
    // login() enables REST without starting the Gateway. run() also calls login().
    void login(std::string token);
    void run(std::string token);
    void close() noexcept;
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] std::optional<User> user() const;
    [[nodiscard]] std::optional<Message> cached_message(Snowflake id) const;
    [[nodiscard]] std::chrono::milliseconds latency() const noexcept;

    [[nodiscard]] Channel channel(Snowflake id) const;
    [[nodiscard]] Channel fetch_channel(Snowflake id) const;
    [[nodiscard]] User fetch_user(Snowflake id) const;
    [[nodiscard]] User fetch_me() const;
    [[nodiscard]] std::vector<Guild> fetch_guilds() const;
    [[nodiscard]] Guild fetch_guild(Snowflake id) const;
    [[nodiscard]] std::vector<Channel> fetch_channels(Snowflake guild_id) const;
    [[nodiscard]] Member fetch_member(Snowflake guild_id, Snowflake user_id) const;
    [[nodiscard]] UserProfile fetch_profile(Snowflake user_id, std::optional<Snowflake> guild_id = {}) const;
    [[nodiscard]] std::vector<Relationship> fetch_relationships() const;
    void send_friend_request(Snowflake user_id) const;
    [[nodiscard]] Channel create_dm(Snowflake recipient) const;
    [[nodiscard]] Message send(Snowflake channel_id, std::string content) const;
    [[nodiscard]] Message send(Snowflake channel_id, const MessageOptions& options) const;

    // Relative API paths only; same authentication, errors and rate limits.
    [[nodiscard]] Json request(std::string method, std::string path, Json body = nullptr) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Throws a useful error if the variable is missing; never prints the token.
[[nodiscard]] std::string token_from_env(const char* name = "DISCORD_TOKEN");
}
