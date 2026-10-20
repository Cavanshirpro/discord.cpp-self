// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <chrono>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace discord {
/// @brief JSON value type used for raw API data and extension fields.
using Json = nlohmann::json;
namespace detail { class Rest; }

/// @brief Exact 64-bit Discord ID, serialized to JSON as a decimal string.
/// Avoid floating-point conversions, which lose precision for large IDs.
class Snowflake {
public:
    /// @brief Create an unset/zero ID.
    constexpr Snowflake() = default;
    /// @brief Construct from an exact unsigned integer.
    constexpr Snowflake(std::uint64_t value) : value_(value) {}
    /// @brief Parse an unsigned decimal ID without whitespace or trailing characters.
    /// @throws std::invalid_argument For empty, malformed or overflowing input.
    explicit Snowflake(std::string_view value);
    /// @brief Return the exact integer representation.
    [[nodiscard]] constexpr std::uint64_t value() const { return value_; }
    /// @brief Return the decimal representation used by Discord's JSON API.
    [[nodiscard]] std::string str() const;
    /// @brief Decode the ID's creation time as a UTC system-clock time point.
    [[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds> created_at() const;
    /// @brief Return true for a nonzero ID; this does not verify remote existence.
    explicit constexpr operator bool() const { return value_ != 0; }
    /// @brief Compare the underlying unsigned ID values.
    auto operator<=>(const Snowflake&) const = default;
private:
    std::uint64_t value_ = 0;
};
/// @brief Serialize an ID as a decimal JSON string.
void to_json(Json& out, const Snowflake& value);
/// @brief Read a decimal string or nonnegative integer JSON ID; reject floating point.
void from_json(const Json& in, Snowflake& value);

/// @brief User snapshot; optional profile fields depend on the response and visibility.
struct User {
    Snowflake id; ///< Account ID.
    std::string name; ///< Discord username.
    std::string display_name; ///< Global display name, falling back to username.
    std::optional<std::string> avatar; ///< Avatar asset hash, if returned.
    bool bot = false; ///< Whether Discord marks this account as a bot.
    std::string discriminator = "0"; ///< Legacy discriminator; usually 0 for migrated names.
    std::uint64_t public_flags = 0; ///< Public user-flag bitmask returned by Discord.
    std::optional<std::string> banner; ///< Banner asset hash, if visible and returned.
    std::optional<std::string> bio; ///< Biography when included in the response.
    std::optional<std::uint32_t> accent_color; ///< Profile accent RGB integer, if returned.
    /// @brief Format the user's mention text; message options still control whether it pings.
    [[nodiscard]] std::string mention() const { return "<@" + id.str() + ">"; }
    /// @brief Build a CDN avatar URL, with a default-avatar fallback; makes no request.
    /// @param size Power of two from 16 to 4096; default 256.
    /// @throws std::invalid_argument If size is outside the supported power-of-two range.
    [[nodiscard]] std::string avatar_url(int size = 256) const;
};
/// @brief Deserialize a user snapshot from Discord JSON.
void from_json(const Json& in, User& value);

/// @brief Guild snapshot. READY guilds may be partial; fetch_guild() retrieves details.
struct Guild {
    Snowflake id; ///< Guild/server ID.
    std::string name; ///< Guild name when returned.
    std::optional<std::string> icon; ///< Guild icon asset hash.
    bool unavailable = false; ///< Guild availability flag from the payload.
    std::optional<Snowflake> owner_id; ///< Owner ID when visible in this response.
    std::optional<std::string> description; ///< Public guild description, if returned.
    std::optional<int> approximate_member_count; ///< Approximate count, not a complete member list.
    std::optional<int> approximate_presence_count; ///< Approximate online count, if returned.
    std::vector<std::string> features; ///< Discord guild feature names.
    int premium_tier = 0; ///< Guild boost tier from the response.
};
/// @brief Deserialize a guild snapshot, including READY's nested properties shape.
void from_json(const Json& in, Guild& value);

/// @brief A user's guild membership; this is separate from its global User profile.
struct Member {
    User user; ///< Global user snapshot embedded in the membership.
    std::optional<std::string> nickname; ///< Guild-specific nickname.
    std::optional<std::string> avatar; ///< Guild-specific avatar hash.
    std::vector<Snowflake> roles; ///< Assigned role IDs returned by Discord.
    std::string joined_at; ///< Join timestamp string from Discord.
    std::optional<std::string> premium_since; ///< Guild-boost start timestamp, if returned.
    bool pending = false; ///< Whether membership screening is pending.
    std::uint64_t flags = 0; ///< Guild member-flag bitmask.
};
/// @brief Deserialize a guild membership snapshot.
void from_json(const Json& in, Member& value);

/// @brief Profile information visible to the requesting account, plus the raw response.
/// Missing values are not proof that information does not exist. Discord's
/// visibility rules determine which fields are returned.
struct UserProfile {
    User user; ///< Profile owner's user snapshot.
    std::optional<std::string> bio; ///< Visible profile biography, falling back to user.bio.
    std::optional<std::string> pronouns; ///< Visible pronoun text.
    std::optional<std::string> premium_since; ///< Premium start timestamp, if returned.
    std::optional<int> mutual_friends_count; ///< Mutual friend count when visible.
    std::vector<Snowflake> mutual_guild_ids; ///< IDs in the returned mutual guild list.
    Json data; ///< Full response, including version-dependent fields; treat as private account data.
};
/// @brief Deserialize visible profile fields and retain the original response.
void from_json(const Json& in, UserProfile& value);

/// @brief Discord relationship state as seen by the logged-in account.
enum class RelationshipType {
    none = 0, ///< No relationship.
    friend_ = 1, ///< Existing friendship.
    blocked = 2, ///< Blocked account.
    incoming_request = 3, ///< A request received from the other account.
    outgoing_request = 4, ///< A request sent by this account.
    implicit = 5 ///< Implicit relationship reported by Discord.
};
/// @brief A relationship entry; listing it does not change its state.
struct Relationship {
    Snowflake id; ///< Other account's ID.
    RelationshipType type = RelationshipType::none; ///< Friendship/block/request state.
    std::optional<User> user; ///< Embedded user snapshot, when returned.
    std::optional<std::string> nickname; ///< Relationship nickname, when returned.
};
/// @brief Deserialize one relationship entry.
void from_json(const Json& in, Relationship& value);

/// @brief Metadata for an existing message attachment; no upload/download is performed.
struct Attachment {
    Snowflake id; ///< Attachment ID.
    std::string filename; ///< File name reported by Discord.
    std::string url; ///< Attachment URL; availability may expire or change.
    std::uint64_t size = 0; ///< Size in bytes.
};
/// @brief Deserialize attachment metadata.
void from_json(const Json& in, Attachment& value);

/// @brief Outgoing text/reply settings. Mention notifications are disabled by default.
struct MessageOptions {
    std::string content; ///< Nonempty text to send.
    bool allow_mentions = false; ///< Enable user/role/everyone mention parsing explicitly.
    bool mention_author = false; ///< Whether a reply should notify its original author.
    std::optional<Snowflake> reply_to; ///< Optional message ID to reference as a reply.
    /// @brief Serialize these options to the API payload; empty content is rejected.
    [[nodiscard]] Json json() const;
};

class Message;
/// @brief Channel snapshot or ID-only handle with helpers bound to a live client.
/// Network helpers are synchronous and fail with Error if the client no longer exists.
class Channel {
public:
    Snowflake id; ///< Channel ID.
    std::optional<Snowflake> guild_id; ///< Owning guild ID; absent for DMs.
    std::string name; ///< Channel name when metadata was fetched.
    int type = 0; ///< Numeric Discord channel type.
    std::vector<User> recipients; ///< DM recipients included in the response.
    std::optional<Snowflake> parent_id; ///< Parent category/channel ID, if returned.
    std::optional<std::string> topic; ///< Channel topic when available.
    int position = 0; ///< Guild ordering position from Discord.
    bool nsfw = false; ///< Age-restricted channel flag.

    /// @brief Send text and return the sent snapshot; mentions default to disabled.
    [[nodiscard]] Message send(std::string content) const;
    /// @brief Send with explicit reply/mention options and return the sent snapshot.
    [[nodiscard]] Message send(const MessageOptions& options) const;
    /// @brief Fetch exactly one message through the user-account history route.
    /// @throws HttpError With status 404/code 10008 if no exact matching ID is returned.
    [[nodiscard]] Message fetch_message(Snowflake message_id) const;
    /// @brief Fetch one page of history in the API's returned order; no auto-pagination.
    /// @param limit Number of messages, from 1 to 100 inclusive; default 50.
    /// @param before Optional exclusive upper message ID for fetching older messages.
    /// @throws std::invalid_argument If limit is outside 1..100.
    [[nodiscard]] std::vector<Message> history(int limit = 50, std::optional<Snowflake> before = {}) const;
    /// @brief Send one typing indicator; does not start a background repeating task.
    void typing() const;

    /// @brief Internal binding hook used by Client; normal callers use its model factories.
    void bind(const std::shared_ptr<detail::Rest>& rest) { rest_ = rest; }
private:
    std::weak_ptr<detail::Rest> rest_;
};
/// @brief Deserialize metadata without binding a client; use Client factories for network helpers.
void from_json(const Json& in, Channel& value);

/// @brief Message snapshot with synchronous operations bound to its originating client.
/// Editing/deleting does not mutate this snapshot. Keep the client alive for helpers.
class Message {
public:
    Snowflake id; ///< Message ID.
    Snowflake channel_id; ///< Channel containing the message.
    std::optional<Snowflake> guild_id; ///< Guild ID, absent for direct messages.
    User author; ///< Message author's user snapshot.
    std::string content; ///< Message text returned by Discord.
    std::string timestamp; ///< Creation timestamp string from Discord.
    std::optional<std::string> edited_timestamp; ///< Last edit timestamp when present.
    std::vector<Attachment> attachments; ///< Existing attachment metadata.
    bool pinned = false; ///< Whether the message is pinned.
    bool tts = false; ///< Whether the message is marked text-to-speech.

    /// @brief Return a bound channel handle without an HTTP metadata request.
    [[nodiscard]] Channel channel() const;
    /// @brief Reply to this message; returns the sent snapshot without mention pings.
    [[nodiscard]] Message reply(std::string content) const;
    /// @brief Reply using explicit options; overrides options.reply_to with this message's ID.
    [[nodiscard]] Message reply(MessageOptions options) const;
    /// @brief Edit this message's content and return a new snapshot; does not change this object.
    [[nodiscard]] Message edit(std::string content) const;
    /// @brief Delete this message, subject to the account's permissions.
    void remove() const;
    /// @brief Add the current account's reaction using Unicode emoji or a custom name:id.
    void react(std::string emoji) const;
    /// @brief Remove only the current account's reaction for the specified emoji.
    void unreact(std::string emoji) const;
    /// @brief Build this message's browser URL; makes no request.
    [[nodiscard]] std::string jump_url() const;
    /// @brief Internal binding hook used by Client and model factories.
    void bind(const std::shared_ptr<detail::Rest>& rest) { rest_ = rest; }
private:
    std::weak_ptr<detail::Rest> rest_;
};
/// @brief Deserialize a snapshot without client binding; network helpers need a live client.
void from_json(const Json& in, Message& value);
}
