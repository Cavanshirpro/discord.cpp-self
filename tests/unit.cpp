// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#include "internal.hpp"
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void check(bool condition, const char* expression) {
    ++checks;
    if (!condition) throw std::runtime_error(expression);
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__)
template<class Error = std::exception, class Function> void throws(Function function) {
    ++checks;
    try { function(); } catch (const Error&) { return; }
    throw std::runtime_error("Expected exception was not thrown");
}
using discord::Json;
using discord::Snowflake;
void models() {
    const Snowflake maximum("18446744073709551615");
    CHECK(maximum.value() == std::numeric_limits<std::uint64_t>::max());
    CHECK(Json(maximum) == "18446744073709551615");
    CHECK(Json("9007199254740993").get<Snowflake>().value() == 9007199254740993ULL);
    CHECK(Snowflake{0}.created_at().time_since_epoch().count() == 1420070400000LL);
    for (const auto* value : {"", "-1", "12x", "18446744073709551616", " 5"})
        throws<std::invalid_argument>([&] { (void)Snowflake(value); });
    throws<std::invalid_argument>([] { (void)Json(-1).get<Snowflake>(); });
    throws<std::invalid_argument>([] { (void)Json(1.5).get<Snowflake>(); });
    const auto user = Json{{"id", "42"}, {"username", "cavan"}, {"global_name", nullptr}, {"avatar", nullptr}}.get<discord::User>();
    CHECK(user.display_name == "cavan");
    CHECK(user.mention() == "<@42>");
    throws<std::invalid_argument>([&] { (void)user.avatar_url(17); });
    const auto guild = Json{{"id", "5"}, {"properties", {{"name", "test"}, {"icon", nullptr}}}}.get<discord::Guild>();
    CHECK(guild.name == "test");
    const auto profile = Json{{"user", {{"id", "42"}, {"username", "cavan"}, {"public_flags", 64}}},
        {"user_profile", {{"bio", "test bio"}, {"pronouns", "test"}}},
        {"mutual_guilds", Json::array({{{"id", "5"}, {"nick", nullptr}}})}, {"mutual_friends_count", 2}}.get<discord::UserProfile>();
    CHECK(profile.bio == "test bio" && profile.pronouns == "test");
    CHECK(profile.user.public_flags == 64);
    CHECK(profile.mutual_guild_ids == std::vector<Snowflake>{5});
    CHECK(profile.mutual_friends_count == 2);
    const auto empty_profile = Json{{"user", {{"id", "42"}, {"username", "cavan"}}},
        {"user_profile", nullptr}, {"mutual_guilds", nullptr}, {"mutual_friends_count", nullptr}}.get<discord::UserProfile>();
    CHECK(!empty_profile.bio && !empty_profile.pronouns && !empty_profile.mutual_friends_count && empty_profile.mutual_guild_ids.empty());
    const auto relation = Json{{"id", "42"}, {"type", 4}, {"user", nullptr}, {"nickname", nullptr}}.get<discord::Relationship>();
    CHECK(relation.type == discord::RelationshipType::outgoing_request && !relation.user);
    const auto member = Json{{"user", {{"id", "42"}, {"username", "cavan"}}}, {"nick", nullptr},
        {"roles", Json::array({"5"})}, {"joined_at", "2026-10-20T00:00:00Z"}}.get<discord::Member>();
    CHECK(member.roles == std::vector<Snowflake>{5} && !member.nickname && !member.pending);
    discord::MessageOptions options;
    options.content = "hello @everyone";
    options.reply_to = Snowflake{123};
    const auto data = options.json();
    CHECK(data["allowed_mentions"]["parse"].empty());
    CHECK(data["allowed_mentions"]["replied_user"] == false);
    CHECK(data["message_reference"]["message_id"] == "123");
    CHECK(!data.contains("embed") && !data.contains("embeds"));
    options.allow_mentions = true;
    CHECK(options.json()["allowed_mentions"]["parse"].size() == 3);
    throws<std::invalid_argument>([] { (void)discord::MessageOptions{}.json(); });
    discord::Channel detached;
    throws<discord::Error>([&] { (void)detached.send("hello"); });
    CHECK(discord::detail::encode_path("a:b /+") == "a%3Ab%20%2F%2B");
    CHECK(discord::detail::encode_path("\xF0\x9F\x91\x8D") == "%F0%9F%91%8D");
}
void commands_and_cache() {
    using discord::detail::split_arguments;
    CHECK(split_arguments("one 'two words' \"\" three\\ four") == std::vector<std::string>({"one", "two words", "", "three four"}));
    throws<std::invalid_argument>([] { (void)split_arguments("'unfinished"); });
    throws<std::invalid_argument>([] { (void)split_arguments("unfinished\\"); });
    discord::Client client;
    discord::Commands commands(client);
    commands.add("ping", [](const auto&) {});
    CHECK(commands.names() == std::vector<std::string>{"ping"});
    throws<std::invalid_argument>([&] { commands.add("ping", [](const auto&) {}); });
    throws<std::invalid_argument>([&] { discord::Commands invalid(client, ""); });
    discord::detail::MessageCache cache(2);
    cache.insert({{"id", "1"}, {"content", "a"}});
    cache.insert({{"id", "2"}});
    cache.insert({{"id", "1"}, {"content", "b"}});
    CHECK(cache.get(1)->at("content") == "b");
    cache.insert({{"id", "3"}});
    CHECK(!cache.get(1));
    CHECK(cache.get(2).has_value());
    cache.erase(2);
    cache.insert({{"id", "2"}});
    cache.insert({{"id", "4"}});
    CHECK(!cache.get(3));
    CHECK(cache.get(2).has_value());
    cache.clear();
    CHECK(!cache.get(2));
    discord::detail::MessageCache disabled(0);
    disabled.insert({{"id", "1"}});
    CHECK(!disabled.get(1));
}
void http() {
    using namespace discord::detail;
    CHECK(route_key({"GET", "/channels/123/messages/456?limit=1", nullptr}) == "GET/channels/123/messages/:id");
    CHECK(route_key({"GET", "/channels/789/messages/456", nullptr}) != route_key({"GET", "/channels/123/messages/456", nullptr}));
    int calls = 0;
    Rest rest({}, [&](const Request& request) {
        CHECK(request.method == "POST");
        if (++calls == 1) return Response{429, R"({"retry_after":0.001,"global":true})", {}};
        return Response{200, R"({"id":"4"})", {}};
    });
    throws<discord::Error>([&] { (void)rest.request("GET", "/users/@me"); });
    throws<std::invalid_argument>([&] { rest.login("bad\r\ntoken"); });
    throws<std::invalid_argument>([&] { rest.login(std::string("bad\0token", 9)); });
    rest.login("offline-token");
    CHECK(rest.request("POST", "/channels/1/messages", {{"content", "hi"}})["id"] == "4");
    CHECK(calls == 2);
    for (const auto* path : {"https://example.org", "//example.org", "/test\r\n", "/../test", "/test#x"})
        throws<std::invalid_argument>([&] { (void)rest.request("GET", path); });
    throws<std::invalid_argument>([&] { (void)rest.request("GET\r\n", "/test"); });
    throws<std::invalid_argument>([&] { (void)rest.request("GET", std::string("/test\0hidden", 12)); });
    rest.cancel();
    throws<discord::Cancelled>([&] { (void)rest.request("GET", "/test"); });
    Rest denied({}, [](const auto&) { return Response{403, R"({"code":50013,"message":"Missing permissions"})", {}}; });
    denied.login("test");
    try { (void)denied.request("GET", "/test"); throw std::runtime_error("Missing HTTP error"); }
    catch (const discord::HttpError& error) { CHECK(error.status == 403); CHECK(error.code == 50013); }
    Rest malformed({}, [](const auto&) { return Response{200, "not json", {}}; });
    malformed.login("test");
    throws<discord::ProtocolError>([&] { (void)malformed.request("GET", "/test"); });
    Rest empty({}, [](const auto&) { return Response{204, "", {}}; });
    empty.login("test");
    CHECK(empty.request("DELETE", "/test").is_null());
    discord::ClientOptions config;
    config.max_rate_limit_retries = 1;
    calls = 0;
    Rest limited(config, [&](const auto&) { ++calls; return Response{429, R"({"retry_after":0})", {}}; });
    limited.login("test");
    throws<discord::HttpError>([&] { (void)limited.request("GET", "/test"); });
    CHECK(calls == 2);
    Rest excessive({}, [](const auto&) { return Response{429, R"({"retry_after":99999})", {}}; });
    excessive.login("test");
    throws<discord::Error>([&] { (void)excessive.request("GET", "/test"); });
    Rest failure({}, [&](const auto&) { ++calls; return Response{500, "unavailable", {}}; });
    failure.login("test"); calls = 0;
    throws<discord::HttpError>([&] { (void)failure.request("POST", "/test", {{"content", "hi"}}); });
    CHECK(calls == 1); // Ambiguous mutations must never be replayed automatically.
    Rest challenge({}, [](const auto&) { return Response{400, R"({"captcha_key":["captcha-required"],"captcha_sitekey":"not-a-real-key"})", {}}; });
    challenge.login("test");
    throws<discord::VerificationRequired>([&] { (void)challenge.request("PUT", "/users/@me/relationships/42", Json::object()); });
    // Learn shared buckets from two different routes, then respect their common
    // reset while keeping a different channel independent.
    config.max_rate_limit_wait = std::chrono::milliseconds{20};
    int bucket_calls = 0;
    Rest shared(config, [&](const Request& request) {
        ++bucket_calls;
        const bool exhausted = request.method == "POST";
        return Response{200, "{}", {{"x-ratelimit-bucket", "same"},
            {"x-ratelimit-remaining", exhausted ? "0" : "1"},
            {"x-ratelimit-reset-after", "1"}}};
    });
    shared.login("test");
    (void)shared.request("GET", "/channels/1/messages");
    (void)shared.request("POST", "/channels/1/messages", {{"content", "x"}});
    CHECK(bucket_calls == 2); // Successful POST was returned despite long reset.
    throws<discord::Error>([&] { (void)shared.request("GET", "/channels/1/messages"); });
    CHECK(bucket_calls == 2); // Rejected BEFORE another network request.
    (void)shared.request("GET", "/channels/2/messages");
    CHECK(bucket_calls == 3);
    // Global 429 responses are retried within the serialized scheduler.
    int global_calls = 0;
    Rest global({}, [&](const Request&) {
        ++global_calls;
        if (global_calls == 1) return Response{429, R"({"retry_after":0.005,"global":true})", {}};
        return Response{200, "{}", {}};
    });
    global.login("test");
    (void)global.request("GET", "/users/@me");
    CHECK(global_calls == 2);
    // Retry exhaustion must retain a global deadline for the next invocation,
    // including when the server conveys global scope only through a header.
    config.max_rate_limit_retries = 0;
    config.max_rate_limit_wait = std::chrono::milliseconds{20};
    int exhausted_calls = 0;
    Rest exhausted(config, [&](const Request&) {
        ++exhausted_calls;
        return Response{429, "{}", {{"retry-after", "1"}, {"x-ratelimit-global", "true"}}};
    });
    exhausted.login("test");
    throws<discord::HttpError>([&] { (void)exhausted.request("GET", "/first"); });
    throws<discord::Error>([&] { (void)exhausted.request("GET", "/different"); });
    CHECK(exhausted_calls == 1);
    exhausted.login("test"); // Starting the Gateway must not forget REST deadlines.
    throws<discord::Error>([&] { (void)exhausted.request("GET", "/still-blocked"); });
    CHECK(exhausted_calls == 1);
    discord::ClientOptions invalid;
    invalid.event_queue_size = 0;
    throws<std::invalid_argument>([&] { discord::Client value(invalid); });
}
}
int main() {
    try { models(); commands_and_cache(); http(); std::cout << checks << " checks passed\n"; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
