// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include "errors.hpp"
#include "models.hpp"
#include <functional>

namespace discord {
/// @brief Connection, retry and memory limits for a Client.
/// Set options before constructing the client. Durations are milliseconds.
struct ClientOptions {
    std::string api_url = "https://discord.com/api/v9"; ///< Trusted REST base URL; receives the token.
    std::string gateway_url = "wss://gateway.discord.gg/"; ///< Trusted Gateway URL; receives the token.
    std::chrono::milliseconds request_timeout{30'000}; ///< HTTP and initial HELLO/READY timeout; must be positive.
    std::chrono::milliseconds max_rate_limit_wait{60'000}; ///< Maximum individual rate-limit wait before failing.
    std::size_t message_cache_size = 1'000; ///< Cached messages; zero disables the cache.
    std::size_t event_queue_size = 4'096; ///< Pending dispatch capacity; overflow raises an error.
    int max_reconnect_attempts = 8; ///< Consecutive reconnect retry budget; zero disables retries.
    int max_rate_limit_retries = 5; ///< Maximum HTTP 429 retries for one request.
};

/// @brief Initial authenticated Gateway state; guild entries may be partial.
struct Ready {
    User user; ///< Logged-in user account.
    std::vector<Guild> guilds; ///< Guild snapshots included in READY.
};
/// @brief A partial message edit, with cached before/after snapshots when available.
struct MessageUpdate {
    Snowflake id; ///< Edited message ID.
    Snowflake channel_id; ///< Channel containing the message.
    std::optional<Message> before; ///< Previous cached snapshot, or nullopt on a cache miss.
    std::optional<Message> after; ///< Merged cached snapshot, or nullopt on a cache miss.
    Json patch; ///< Raw partial update; missing fields must not be treated as empty values.
};
/// @brief A message deletion; a cache miss leaves cached empty.
struct MessageDelete {
    Snowflake id; ///< Deleted message ID.
    Snowflake channel_id; ///< Channel that contained the message.
    std::optional<Message> cached; ///< Last cached snapshot, if available.
};
/// @brief A raw Gateway dispatch, including events without a typed model.
struct RawEvent {
    std::string name; ///< Discord dispatch name, such as MESSAGE_CREATE.
    Json data; ///< Dispatch data object; not the full Gateway envelope.
};

/// @brief A Discord user-account client with synchronous REST and Gateway events.
/// Register callbacks before run(). Callbacks run serially on the run() caller's
/// thread while Gateway I/O and heartbeats run on a worker. Keep the client alive
/// while using its model network helpers; models hold weak transport references.
class Client {
public:
    /// @brief Create a disconnected client; construction does not log in.
    /// @param options Transport limits and trusted endpoints.
    /// @throws std::invalid_argument If options contain invalid limits or URLs.
    explicit Client(ClientOptions options = {});
    /// @brief Request shutdown. A caller running run() on another thread must join it first.
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    /// @brief Add a listener for initial READY or a fresh identification after reconnect.
    /// @return This client for chaining. Register before run().
    Client& on_ready(std::function<void(const Ready&)> handler);
    /// @brief Add a listener for each created message, including your own messages.
    /// @note Check message.author.id against user() to avoid replying to yourself.
    Client& on_message(std::function<void(const Message&)> handler);
    /// @brief Add a partial-edit listener; before/after are empty for uncached messages.
    Client& on_message_edit(std::function<void(const MessageUpdate&)> handler);
    /// @brief Add a deletion listener; bulk deletions emit one callback per message.
    Client& on_message_delete(std::function<void(const MessageDelete&)> handler);
    /// @brief Add a raw dispatch listener, called after any corresponding typed listeners.
    Client& on_raw_event(std::function<void(const RawEvent&)> handler);
    /// @brief Add a listener for successful Gateway session resumption.
    Client& on_resumed(std::function<void()> handler);
    /// @brief Set the callback-exception handler; replacing any previous handler.
    /// Handled exceptions let dispatch continue. Rethrow to stop run(). This does
    /// not intercept terminal Gateway errors, which propagate directly from run().
    Client& on_error(std::function<void(const std::exception&)> handler);

    /// @brief Configure REST authentication without opening a Gateway connection.
    /// @param token Raw user token, without a Bot prefix. Do not hard-code it.
    /// @note Does not validate the token remotely; fetch_me() can do that. Clears user/cache state.
    void login(std::string token);
    /// @brief Log in and block while dispatching Gateway events until close() or failure.
    /// @param token Raw user-account token, usually read with token_from_env().
    /// @throws GatewayError On fatal Gateway closes; transport/protocol failures also propagate.
    /// @note Maintains heartbeats and bounded reconnect/resume; sequential reuse is supported.
    void run(std::string token);
    /// @brief Stop event processing and interrupt transport/rate-limit waits.
    /// Safe from callbacks or another normal thread; not an OS signal handler.
    void close() noexcept;
    /// @brief Return whether a run() call is active, including connection setup.
    [[nodiscard]] bool is_running() const noexcept;
    /// @brief Return the cached logged-in user after READY, or nullopt before it.
    /// @note For REST-only applications use fetch_me(); this method makes no request.
    [[nodiscard]] std::optional<User> user() const;
    /// @brief Return a cached message snapshot, or nullopt; no HTTP request is made.
    [[nodiscard]] std::optional<Message> cached_message(Snowflake id) const;
    /// @brief Latest heartbeat round-trip time, or -1 ms before an acknowledgement.
    [[nodiscard]] std::chrono::milliseconds latency() const noexcept;

    /// @brief Create a bound channel handle by ID without fetching its metadata.
    /// Only the ID is populated; use fetch_channel() when you need name/type/details.
    [[nodiscard]] Channel channel(Snowflake id) const;
    /// @brief Fetch a channel snapshot with metadata and bound network helpers.
    [[nodiscard]] Channel fetch_channel(Snowflake id) const;
    /// @brief Fetch a user; fall back to its visible profile only on HTTP 403/code 40001.
    [[nodiscard]] User fetch_user(Snowflake id) const;
    /// @brief Fetch the current account over REST; works after login() without run().
    [[nodiscard]] User fetch_me() const;
    /// @brief Fetch the account's guild list, requesting approximate counts where available.
    [[nodiscard]] std::vector<Guild> fetch_guilds() const;
    /// @brief Fetch one accessible guild with approximate member/presence counts.
    [[nodiscard]] Guild fetch_guild(Snowflake id) const;
    /// @brief Fetch accessible channels in a guild and bind their network helpers.
    [[nodiscard]] std::vector<Channel> fetch_channels(Snowflake guild_id) const;
    /// @brief Fetch a user's membership, roles and visible guild-specific fields.
    [[nodiscard]] Member fetch_member(Snowflake guild_id, Snowflake user_id) const;
    /// @brief Fetch profile information visible to this account; unavailable fields remain optional.
    /// @param user_id User whose profile to retrieve.
    /// @param guild_id Optional mutual guild context for guild-specific profile fields.
    [[nodiscard]] UserProfile fetch_profile(Snowflake user_id, std::optional<Snowflake> guild_id = {}) const;
    /// @brief Fetch this account's visible relationships, including pending requests.
    [[nodiscard]] std::vector<Relationship> fetch_relationships() const;
    /// @brief Send a friend request to a user; this is a real account mutation.
    /// @throws VerificationRequired If Discord requests verification/CAPTCHA; not automatically retried.
    void send_friend_request(Snowflake user_id) const;
    /// @brief Open or retrieve a DM channel with the recipient; does not send a message.
    [[nodiscard]] Channel create_dm(Snowflake recipient) const;
    /// @brief Send a text message to a channel; mentions default to disabled.
    /// @return The newly sent message snapshot. HTTP calls are synchronous.
    [[nodiscard]] Message send(Snowflake channel_id, std::string content) const;
    /// @brief Send using explicit reply and mention options; returns the sent snapshot.
    [[nodiscard]] Message send(Snowflake channel_id, const MessageOptions& options) const;

    /// @brief Make a synchronous raw REST request with this client's authentication and rate limits.
    /// @param method HTTP method, such as GET, POST, PATCH, PUT or DELETE.
    /// @param path Relative API path beginning with /; absolute URLs are rejected.
    /// @param body JSON request body; null means no JSON body.
    /// @return Parsed JSON response, or null for an empty response.
    /// @throws HttpError For failed HTTP responses; transport/JSON errors also propagate.
    [[nodiscard]] Json request(std::string method, std::string path, Json body = nullptr) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// @brief Read a user token from a process environment variable without printing it.
/// @param name Variable name; defaults to DISCORD_TOKEN. Does not load .env files.
/// @throws Error If the variable is missing/empty; invalid names raise std::invalid_argument.
[[nodiscard]] std::string token_from_env(const char* name = "DISCORD_TOKEN");
}
