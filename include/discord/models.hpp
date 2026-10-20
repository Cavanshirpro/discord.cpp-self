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
using Json = nlohmann::json;
namespace detail { class Rest; }

// IDs are integers internally, strings on the wire. No floating-point rounding.
class Snowflake {
public:
    constexpr Snowflake() = default;
    constexpr Snowflake(std::uint64_t value) : value_(value) {}
    explicit Snowflake(std::string_view value);
    [[nodiscard]] constexpr std::uint64_t value() const { return value_; }
    [[nodiscard]] std::string str() const;
    [[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds> created_at() const;
    explicit constexpr operator bool() const { return value_ != 0; }
    auto operator<=>(const Snowflake&) const = default;
private:
    std::uint64_t value_ = 0;
};
void to_json(Json& out, const Snowflake& value);
void from_json(const Json& in, Snowflake& value);

struct User {
    Snowflake id;
    std::string name;
    std::string display_name;
    std::optional<std::string> avatar;
    bool bot = false;
    std::string discriminator = "0";
    std::uint64_t public_flags = 0;
    std::optional<std::string> banner;
    std::optional<std::string> bio;
    std::optional<std::uint32_t> accent_color;
    [[nodiscard]] std::string mention() const { return "<@" + id.str() + ">"; }
    [[nodiscard]] std::string avatar_url(int size = 256) const;
};
void from_json(const Json& in, User& value);

struct Guild {
    Snowflake id;
    std::string name;
    std::optional<std::string> icon;
    bool unavailable = false;
    std::optional<Snowflake> owner_id;
    std::optional<std::string> description;
    std::optional<int> approximate_member_count;
    std::optional<int> approximate_presence_count;
    std::vector<std::string> features;
    int premium_tier = 0;
};
void from_json(const Json& in, Guild& value);

struct Member {
    User user;
    std::optional<std::string> nickname;
    std::optional<std::string> avatar;
    std::vector<Snowflake> roles;
    std::string joined_at;
    std::optional<std::string> premium_since;
    bool pending = false;
    std::uint64_t flags = 0;
};
void from_json(const Json& in, Member& value);

struct UserProfile {
    User user;
    std::optional<std::string> bio;
    std::optional<std::string> pronouns;
    std::optional<std::string> premium_since;
    std::optional<int> mutual_friends_count;
    std::vector<Snowflake> mutual_guild_ids;
    // Extended profile fields vary by account visibility and Discord version.
    Json data;
};
void from_json(const Json& in, UserProfile& value);

enum class RelationshipType { none = 0, friend_ = 1, blocked = 2, incoming_request = 3, outgoing_request = 4, implicit = 5 };
struct Relationship {
    Snowflake id;
    RelationshipType type = RelationshipType::none;
    std::optional<User> user;
    std::optional<std::string> nickname;
};
void from_json(const Json& in, Relationship& value);

struct Attachment {
    Snowflake id;
    std::string filename;
    std::string url;
    std::uint64_t size = 0;
};
void from_json(const Json& in, Attachment& value);

// Mentions are opt-in. A reply also defaults to not pinging its author.
struct MessageOptions {
    std::string content;
    bool allow_mentions = false;
    bool mention_author = false;
    std::optional<Snowflake> reply_to;
    [[nodiscard]] Json json() const;
};

class Message;
class Channel {
public:
    Snowflake id;
    std::optional<Snowflake> guild_id;
    std::string name;
    int type = 0;
    std::vector<User> recipients;
    std::optional<Snowflake> parent_id;
    std::optional<std::string> topic;
    int position = 0;
    bool nsfw = false;

    [[nodiscard]] Message send(std::string content) const;
    [[nodiscard]] Message send(const MessageOptions& options) const;
    [[nodiscard]] Message fetch_message(Snowflake message_id) const;
    [[nodiscard]] std::vector<Message> history(int limit = 50, std::optional<Snowflake> before = {}) const;
    void typing() const;

    // Used by the client to attach network operations to model values.
    void bind(const std::shared_ptr<detail::Rest>& rest) { rest_ = rest; }
private:
    std::weak_ptr<detail::Rest> rest_;
};
void from_json(const Json& in, Channel& value);

class Message {
public:
    Snowflake id;
    Snowflake channel_id;
    std::optional<Snowflake> guild_id;
    User author;
    std::string content;
    std::string timestamp;
    std::optional<std::string> edited_timestamp;
    std::vector<Attachment> attachments;
    bool pinned = false;
    bool tts = false;

    [[nodiscard]] Channel channel() const;
    [[nodiscard]] Message reply(std::string content) const;
    [[nodiscard]] Message reply(MessageOptions options) const;
    [[nodiscard]] Message edit(std::string content) const;
    void remove() const;
    void react(std::string emoji) const;
    void unreact(std::string emoji) const;
    [[nodiscard]] std::string jump_url() const;
    void bind(const std::shared_ptr<detail::Rest>& rest) { rest_ = rest; }
private:
    std::weak_ptr<detail::Rest> rest_;
};
void from_json(const Json& in, Message& value);
}
