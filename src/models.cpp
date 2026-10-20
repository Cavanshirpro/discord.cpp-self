// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include "internal.hpp"
#include <charconv>

namespace discord {
namespace {
template<class T> std::optional<T> optional_field(const Json& in, const char* key) {
    const auto it = in.find(key);
    if (it == in.end() || it->is_null()) return {};
    return it->get<T>();
}
std::shared_ptr<detail::Rest> lock_rest(const std::weak_ptr<detail::Rest>& rest) {
    auto result = rest.lock();
    if (!result) throw Error("This model has no live client");
    return result;
}
Message bound_message(const Json& data, const std::shared_ptr<detail::Rest>& rest) {
    auto value = data.get<Message>();
    value.bind(rest);
    return value;
}
std::string message_path(Snowflake channel, Snowflake message) {
    return "/channels/" + channel.str() + "/messages/" + message.str();
}
}

Snowflake::Snowflake(std::string_view value) {
    if (value.empty()) throw std::invalid_argument("Invalid Discord snowflake");
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), value_);
    if (error != std::errc{} || end != value.data() + value.size() || value.empty())
        throw std::invalid_argument("Invalid Discord snowflake");
}
std::string Snowflake::str() const { return std::to_string(value_); }
std::chrono::sys_time<std::chrono::milliseconds> Snowflake::created_at() const {
    return std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{(value_ >> 22) + 1420070400000ULL}};
}
void to_json(Json& out, const Snowflake& value) { out = value.str(); }
void from_json(const Json& in, Snowflake& value) {
    if (in.is_string()) value = Snowflake(in.get_ref<const std::string&>());
    else if (in.is_number_unsigned()) value = Snowflake(in.get<std::uint64_t>());
    else if (in.is_number_integer() && in.get<std::int64_t>() >= 0)
        value = Snowflake(static_cast<std::uint64_t>(in.get<std::int64_t>()));
    else throw std::invalid_argument("Snowflake must be an unsigned integer or decimal string");
}
void from_json(const Json& in, User& value) {
    value.id = in.at("id").get<Snowflake>();
    value.name = in.value("username", "");
    value.display_name = optional_field<std::string>(in, "global_name").value_or(value.name);
    value.avatar = optional_field<std::string>(in, "avatar");
    value.bot = in.value("bot", false);
    value.discriminator = in.value("discriminator", "0");
    value.public_flags = in.value("public_flags", std::uint64_t{});
    value.banner = optional_field<std::string>(in, "banner");
    value.bio = optional_field<std::string>(in, "bio");
    value.accent_color = optional_field<std::uint32_t>(in, "accent_color");
}
std::string User::avatar_url(int size) const {
    if (size < 16 || size > 4096 || (size & (size - 1)))
        throw std::invalid_argument("Avatar size must be a power of two between 16 and 4096");
    if (!avatar) return "https://cdn.discordapp.com/embed/avatars/" + std::to_string((id.value() >> 22) % 6) + ".png";
    return "https://cdn.discordapp.com/avatars/" + id.str() + "/" + *avatar +
        (avatar->starts_with("a_") ? ".gif" : ".png") + "?size=" + std::to_string(size);
}
void from_json(const Json& in, Guild& value) {
    value.id = in.at("id").get<Snowflake>();
    const auto& properties = in.contains("properties") && in["properties"].is_object() ? in["properties"] : in;
    value.name = properties.value("name", "");
    value.icon = optional_field<std::string>(properties, "icon");
    value.unavailable = in.value("unavailable", false);
    value.owner_id = optional_field<Snowflake>(properties, "owner_id");
    value.description = optional_field<std::string>(properties, "description");
    value.approximate_member_count = optional_field<int>(in, "approximate_member_count");
    value.approximate_presence_count = optional_field<int>(in, "approximate_presence_count");
    value.features = properties.value("features", std::vector<std::string>{});
    value.premium_tier = properties.value("premium_tier", 0);
}
void from_json(const Json& in, Member& value) {
    value.user = in.at("user").get<User>();
    value.nickname = optional_field<std::string>(in, "nick");
    value.avatar = optional_field<std::string>(in, "avatar");
    value.roles = in.value("roles", std::vector<Snowflake>{});
    value.joined_at = in.value("joined_at", "");
    value.premium_since = optional_field<std::string>(in, "premium_since");
    value.pending = in.value("pending", false);
    value.flags = in.value("flags", std::uint64_t{});
}
void from_json(const Json& in, UserProfile& value) {
    value.user = in.at("user").get<User>();
    const auto details = in.value("user_profile", Json::object());
    value.bio = details.is_object() ? optional_field<std::string>(details, "bio") : std::nullopt;
    if (!value.bio) value.bio = value.user.bio;
    value.pronouns = details.is_object() ? optional_field<std::string>(details, "pronouns") : std::nullopt;
    value.premium_since = optional_field<std::string>(in, "premium_since");
    value.mutual_friends_count = optional_field<int>(in, "mutual_friends_count");
    value.mutual_guild_ids.clear();
    if (const auto guilds = in.find("mutual_guilds"); guilds != in.end() && guilds->is_array())
        for (const auto& guild : *guilds) value.mutual_guild_ids.push_back(guild.at("id").get<Snowflake>());
    value.data = in;
}
void from_json(const Json& in, Relationship& value) {
    value.id = in.at("id").get<Snowflake>();
    value.type = static_cast<RelationshipType>(in.value("type", 0));
    value.user = optional_field<User>(in, "user");
    value.nickname = optional_field<std::string>(in, "nickname");
}
void from_json(const Json& in, Attachment& value) {
    value.id = in.at("id").get<Snowflake>();
    value.filename = in.value("filename", "");
    value.url = in.value("url", "");
    value.size = in.value("size", std::uint64_t{});
}
Json MessageOptions::json() const {
    if (content.empty()) throw std::invalid_argument("A message needs text content");
    Json out{{"content", content}, {"allowed_mentions", {
        {"parse", allow_mentions ? Json::array({"users", "roles", "everyone"}) : Json::array()},
        {"replied_user", mention_author}}}};
    if (reply_to) out["message_reference"] = {{"message_id", *reply_to}, {"fail_if_not_exists", false}};
    return out;
}
void from_json(const Json& in, Channel& value) {
    value.id = in.at("id").get<Snowflake>();
    value.guild_id = optional_field<Snowflake>(in, "guild_id");
    value.name = optional_field<std::string>(in, "name").value_or("");
    value.type = in.value("type", 0);
    value.recipients = in.value("recipients", std::vector<User>{});
    value.parent_id = optional_field<Snowflake>(in, "parent_id");
    value.topic = optional_field<std::string>(in, "topic");
    value.position = in.value("position", 0);
    value.nsfw = in.value("nsfw", false);
}
Message Channel::send(std::string content) const {
    MessageOptions options;
    options.content = std::move(content);
    return send(options);
}
Message Channel::send(const MessageOptions& options) const {
    auto rest = lock_rest(rest_);
    return bound_message(rest->request("POST", "/channels/" + id.str() + "/messages", options.json()), rest);
}
Message Channel::fetch_message(Snowflake message_id) const {
    auto rest = lock_rest(rest_);
    // User accounts fetch a message through history, not the bot-only single
    // message route. A neighbouring message does not count as a successful fetch.
    const auto data = rest->request("GET", "/channels/" + id.str() + "/messages?limit=1&around=" + message_id.str());
    if (!data.is_array()) throw ProtocolError("Message history response must be an array");
    if (data.empty() || data.front().at("id").get<Snowflake>() != message_id)
        throw HttpError(404, 10008, "Unknown Message");
    return bound_message(data.front(), rest);
}
std::vector<Message> Channel::history(int limit, std::optional<Snowflake> before) const {
    if (limit < 1 || limit > 100) throw std::invalid_argument("History limit must be between 1 and 100");
    auto rest = lock_rest(rest_);
    auto path = "/channels/" + id.str() + "/messages?limit=" + std::to_string(limit);
    if (before) path += "&before=" + before->str();
    auto data = rest->request("GET", path);
    if (!data.is_array()) throw ProtocolError("Message history response must be an array");
    std::vector<Message> result;
    for (const auto& item : data) result.push_back(bound_message(item, rest));
    return result;
}
void Channel::typing() const {
    lock_rest(rest_)->request("POST", "/channels/" + id.str() + "/typing");
}
void from_json(const Json& in, Message& value) {
    value.id = in.at("id").get<Snowflake>();
    value.channel_id = in.at("channel_id").get<Snowflake>();
    value.guild_id = optional_field<Snowflake>(in, "guild_id");
    value.author = in.at("author").get<User>();
    value.content = in.value("content", "");
    value.timestamp = in.value("timestamp", "");
    value.edited_timestamp = optional_field<std::string>(in, "edited_timestamp");
    value.attachments = in.value("attachments", std::vector<Attachment>{});
    value.pinned = in.value("pinned", false);
    value.tts = in.value("tts", false);
}
Channel Message::channel() const {
    Channel result;
    result.id = channel_id;
    result.guild_id = guild_id;
    result.bind(lock_rest(rest_));
    return result;
}
Message Message::reply(std::string content) const {
    MessageOptions options;
    options.content = std::move(content);
    return reply(std::move(options));
}
Message Message::reply(MessageOptions options) const {
    options.reply_to = id;
    return channel().send(options);
}
Message Message::edit(std::string content) const {
    auto rest = lock_rest(rest_);
    return bound_message(rest->request("PATCH", message_path(channel_id, id), {
        {"content", std::move(content)}, {"allowed_mentions", {{"parse", Json::array()}}}}), rest);
}
void Message::remove() const { lock_rest(rest_)->request("DELETE", message_path(channel_id, id)); }
void Message::react(std::string emoji) const {
    lock_rest(rest_)->request("PUT", message_path(channel_id, id) + "/reactions/" + detail::encode_path(emoji) + "/@me");
}
void Message::unreact(std::string emoji) const {
    lock_rest(rest_)->request("DELETE", message_path(channel_id, id) + "/reactions/" + detail::encode_path(emoji) + "/@me");
}
std::string Message::jump_url() const {
    return "https://discord.com/channels/" + (guild_id ? guild_id->str() : "@me") + "/" + channel_id.str() + "/" + id.str();
}
}
