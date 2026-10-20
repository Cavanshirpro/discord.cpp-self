// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace {
// Advanced manual harness: discovering a target is read-only. --run performs
// real sends/DMs and may request friendship; inspect the target and docs first.
using discord::Json;
using discord::Snowflake;
std::string test_guild_name() {
    const auto* value = std::getenv("DISCORD_TEST_GUILD");
    return value && *value ? value : "library-tests";
}
std::string test_category_name() {
    const auto* value = std::getenv("DISCORD_TEST_CATEGORY");
    return value && *value ? value : "integration";
}
struct Accounts {
    std::array<std::string, 2> tokens;
    std::array<discord::Client, 2> clients;
    std::array<discord::User, 2> users;
    std::array<bool, 2> authenticated{false, false};
};
void load(Accounts& accounts, const char* path, bool reporting_only = false) {
    // Keep both credentials in an ignored local file. Never print their values.
    std::ifstream file(path, std::ios::binary);
    if (!file) throw discord::Error("Cannot read credential file");
    std::string text((std::istreambuf_iterator<char>(file)), {});
    if (text.size() > 16'384) throw discord::Error("Credential file is too large");
    const std::regex pattern(R"((mfa\.[A-Za-z0-9_-]{20,}|[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{5,}\.[A-Za-z0-9_-]{20,}))");
    std::vector<std::string> tokens;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it)
        if (std::find(tokens.begin(), tokens.end(), it->str()) == tokens.end()) tokens.push_back(it->str());
    if (tokens.size() != 2) throw discord::Error("Expected two distinct credentials");
    for (std::size_t i = 0; i < 2; ++i) {
        accounts.tokens[i] = std::move(tokens[i]);
        accounts.clients[i].login(accounts.tokens[i]);
        try { accounts.users[i] = accounts.clients[i].fetch_me(); }
        catch (const discord::HttpError& error) {
            if (!reporting_only || i != 0 || (error.status != 401 && error.status != 403)) throw;
            std::cout << "A_AUTH_UNAVAILABLE status=" << error.status << '\n';
            continue;
        }
        accounts.authenticated[i] = true;
        if (accounts.users[i].bot) throw discord::Error("Expected two normal accounts");
    }
    if (accounts.users[0].id == accounts.users[1].id) throw discord::Error("Both credentials belong to one account");
}
Json discover(Accounts& accounts) {
    // Only inspect a guild matching the configured name and shared by both accounts.
    const auto first = accounts.clients[0].fetch_guilds();
    const auto second = accounts.clients[1].fetch_guilds();
    Json result{{"accounts", Json::array()}, {"common_guilds", Json::array()}};
    for (std::size_t i = 0; i < 2; ++i)
        result["accounts"].push_back({{"label", i == 0 ? "A" : "B"}, {"id", accounts.users[i].id}, {"username", accounts.users[i].name}});
    for (const auto& guild : first) {
        if (std::none_of(second.begin(), second.end(), [&](const auto& other) { return other.id == guild.id; })) continue;
        Json item{{"id", guild.id}, {"name", guild.name}, {"channels", Json::array()}};
        if (guild.name == test_guild_name()) {
            const auto channels = accounts.clients[0].request("GET", "/guilds/" + guild.id.str() + "/channels");
            for (const auto& channel : channels) {
                item["channels"].push_back({{"id", channel.at("id")}, {"name", channel.value("name", "")},
                    {"type", channel.value("type", 0)}, {"parent_id", channel.value("parent_id", Json(nullptr))}});
            }
        }
        if (guild.name == test_guild_name()) result["common_guilds"].push_back(std::move(item));
    }
    return result;
}
struct Target {
    Snowflake guild, category, channel;
    std::string name;
    std::string label() const { return test_guild_name() + " / " + test_category_name() + " / #" + name; }
};
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
Target target(const Json& discovery) {
    // Fail on ambiguity rather than sending to a guessed category or channel.
    if (discovery["common_guilds"].size() != 1) throw discord::Error("Expected one matching shared test guild");
    const auto& guild = discovery["common_guilds"][0];
    std::vector<Json> categories, channels;
    for (const auto& channel : guild["channels"])
        if (channel["type"] == 4 && lower(channel["name"].get<std::string>()) == lower(test_category_name())) categories.push_back(channel);
    if (categories.size() != 1) throw discord::Error("Expected one matching test category");
    for (const auto& channel : guild["channels"])
        if (channel["type"] == 0 && channel["parent_id"] == categories[0]["id"]) channels.push_back(channel);
    if (channels.size() != 1) throw discord::Error("Expected one text channel under the test category");
    return {guild["id"].get<Snowflake>(), categories[0]["id"].get<Snowflake>(),
        channels[0]["id"].get<Snowflake>(), channels[0]["name"].get<std::string>()};
}
Json pick(const Json& input, std::initializer_list<const char*> keys) {
    Json result = Json::object();
    for (const auto* key : keys) if (input.contains(key)) result[key] = input[key];
    return result;
}
Json error_result(const discord::HttpError& error) {
    return {{"status", "failed"}, {"http_status", error.status}, {"discord_code", error.code}};
}
template<class Function> Json attempt(Function function) {
    // Turn failures into journal entries; verification challenges are not retried.
    try { auto result = function(); result["status"] = "ok"; return result; }
    catch (const discord::VerificationRequired& error) {
        auto result = error_result(error); result["verification_required"] = true;
        result["reason"] = "Discord verification/CAPTCHA required; no automatic retry"; return result;
    } catch (const discord::HttpError& error) { return error_result(error); }
    catch (const discord::TransportError&) { return {{"status", "failed"}, {"reason", "transport_error; mutation not replayed"}}; }
    catch (const std::exception&) { return {{"status", "failed"}, {"reason", "response_validation_failed"}}; }
}
int peer_relationship(discord::Client& client, Snowflake peer) {
    for (const auto& relationship : client.fetch_relationships()) if (relationship.id == peer) return static_cast<int>(relationship.type);
    return 0;
}
Json safe_profile(const discord::UserProfile& profile) {
    const auto& data = profile.data;
    Json result{{"user", pick(data.at("user"), {"id", "username", "global_name", "discriminator", "avatar", "banner", "accent_color", "public_flags", "bio", "bot", "system", "primary_guild", "avatar_decoration_data", "collectibles", "display_name_styles"})},
        {"account_created_at_utc_ms", profile.user.id.created_at().time_since_epoch().count()},
        {"mutual_guild_count", profile.mutual_guild_ids.size()}, {"avatar_url", profile.user.avatar_url()}};
    for (const auto* key : {"user_profile", "guild_member_profile", "premium_since", "premium_guild_since", "premium_type", "badges", "guild_badges", "connected_accounts", "mutual_friends_count"})
        if (data.contains(key)) result[key] = data[key];
    // Record schema visibility as well as values; null is not a positive claim
    // that the underlying information exists or is visible to this account.
    result["returned_fields"] = Json::array();
    for (auto it = data.begin(); it != data.end(); ++it) result["returned_fields"].push_back(it.key());
    result["user_fields"] = Json::array();
    for (auto it = data["user"].begin(); it != data["user"].end(); ++it) result["user_fields"].push_back(it.key());
    result["note"] = "null/empty means returned without a value; absent means not returned. Only information visible to the requesting account is available.";
    return result;
}
void check(bool value) { if (!value) throw discord::Error("Test response did not match expected data"); }
struct Runner {
    Accounts& accounts;
    Target destination;
    std::filesystem::path output;
    Json report;
    std::string marker;
    Runner(Accounts& value, Target target, const char* path, bool resume = false) : accounts(value), destination(std::move(target)), output(path) {
        if (resume) {
            std::ifstream file(output);
            if (!file) throw discord::Error("Cannot read previous report");
            file >> report;
            check(report.at("guild_id").get<Snowflake>() == destination.guild && report.at("channel_id").get<Snowflake>() == destination.channel);
            for (std::size_t i = 0; i < 2; ++i) {
                const auto& previous = report.at("accounts").at(i);
                if (accounts.authenticated[i]) check(previous.at("id").get<Snowflake>() == accounts.users[i].id);
                else { accounts.users[i].id = previous.at("id").get<Snowflake>(); accounts.users[i].name = previous.at("username").get<std::string>(); }
            }
            marker = report.at("run").get<std::string>();
            return;
        }
        if (std::filesystem::exists(output)) throw discord::Error("A journal already exists; inspect it before repeating live mutations");
        marker = "discord.cpp-self " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        report = {{"run", marker}, {"guild_id", destination.guild}, {"channel_id", destination.channel}, {"channel_name", destination.name},
            {"accounts", Json::array()}, {"steps", Json::object()}, {"report_messages", Json::array()}};
        for (std::size_t i = 0; i < 2; ++i) report["accounts"].push_back({{"label", i == 0 ? "A" : "B"}, {"username", accounts.users[i].name}, {"id", accounts.users[i].id}});
    }
    void safe(const std::string& text) const {
        for (const auto& token : accounts.tokens) if (text.find(token) != std::string::npos) throw discord::Error("Credential found in output");
    }
    void save() const {
        // Journals contain account/profile data and belong in an ignored build path.
        const auto text = report.dump(2); safe(text);
        if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file || !(file << text << '\n')) throw discord::Error("Cannot write test report");
    }
    template<class Function> void step(const std::string& name, Function function) {
        // Journal before a potentially mutating operation; an interrupted run
        // must be inspected instead of automatically repeating its requests.
        report["steps"][name] = {{"status", "started"}}; save();
        report["steps"][name].update(attempt(std::move(function))); save();
        std::cout << name << ": " << report["steps"][name]["status"].get<std::string>() << '\n' << std::flush;
    }
    void publish(std::size_t actor, std::string title, const Json& data) {
        auto body = data.dump(2, ' ', true);
        for (auto backtick = body.find('`'); backtick != std::string::npos; backtick = body.find('`', backtick + 6)) body.replace(backtick, 1, "\\u0060");
        // JSON ASCII encoding gives safe chunk boundaries for Unicode values.
        std::size_t position = 0, part = 0;
        while (position < body.size()) {
            auto end = std::min(position + 1550, body.size());
            if (end < body.size()) {
                const auto newline = body.rfind('\n', end);
                if (newline != std::string::npos && newline > position) end = newline + 1;
            }
            auto chunk = body.substr(position, end - position);
            const auto text = "**" + title + " / " + std::to_string(++part) + "**\n```json\n" + chunk + "\n```";
            safe(text);
            discord::Message message;
            try { message = accounts.clients[actor].send(destination.channel, text); }
            catch (const discord::HttpError& error) {
                report["report_delivery_error"] = error_result(error); save(); throw;
            }
            report["report_messages"].push_back({{"id", message.id}, {"url", "https://discord.com/channels/" + destination.guild.str() + "/" + destination.channel.str() + "/" + message.id.str()}});
            save();
            position = end;
        }
    }
    void run() {
        // Each mutation is journaled before execution, then verified with a read.
        std::array<std::optional<discord::Message>, 2> messages;
        for (std::size_t i = 0; i < 2; ++i) {
            const auto label = i == 0 ? "A" : "B";
            step(std::string(label) + "_server_channel", [&] {
                const auto guild = accounts.clients[i].fetch_guild(destination.guild);
                const auto channels = accounts.clients[i].fetch_channels(destination.guild);
                const auto channel = accounts.clients[i].fetch_channel(destination.channel);
                check(channel.parent_id == destination.category);
                return Json{{"guild", {{"id", guild.id}, {"name", guild.name}, {"owner_id", guild.owner_id ? Json(*guild.owner_id) : Json(nullptr)},
                    {"description", guild.description ? Json(*guild.description) : Json(nullptr)}, {"member_count", guild.approximate_member_count ? Json(*guild.approximate_member_count) : Json(nullptr)},
                    {"online_count", guild.approximate_presence_count ? Json(*guild.approximate_presence_count) : Json(nullptr)}, {"features", guild.features}, {"premium_tier", guild.premium_tier}}},
                    {"channel_count", channels.size()}, {"channel", {{"id", channel.id}, {"name", channel.name}, {"parent_id", *channel.parent_id}, {"type", channel.type},
                    {"position", channel.position}, {"nsfw", channel.nsfw}, {"topic", channel.topic ? Json(*channel.topic) : Json(nullptr)}}}};
            });
            step(std::string(label) + "_peer_user", [&] {
                const auto user = accounts.clients[i].fetch_user(accounts.users[1 - i].id);
                check(user.id == accounts.users[1 - i].id);
                return Json{{"id", user.id}, {"username", user.name}, {"display_name", user.display_name}, {"public_flags", user.public_flags}, {"bot", user.bot}};
            });
            step(std::string(label) + "_peer_member", [&] {
                const auto member = accounts.clients[i].fetch_member(destination.guild, accounts.users[1 - i].id);
                return Json{{"id", member.user.id}, {"nickname", member.nickname ? Json(*member.nickname) : Json(nullptr)}, {"roles", member.roles},
                    {"joined_at", member.joined_at}, {"premium_since", member.premium_since ? Json(*member.premium_since) : Json(nullptr)}, {"pending", member.pending}, {"flags", member.flags}};
            });
            step(std::string(label) + "_peer_profile", [&] { return safe_profile(accounts.clients[i].fetch_profile(accounts.users[1 - i].id, destination.guild)); });
            step(std::string(label) + "_send", [&] {
                const auto text = "[" + marker + "] Account " + label + ": user-account message test in " + destination.label() + ".";
                messages[i] = accounts.clients[i].send(destination.channel, text);
                return Json{{"message_id", messages[i]->id}, {"content", messages[i]->content}};
            });
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const auto label = i == 0 ? "A" : "B";
            step(std::string(label) + "_history", [&] {
                const auto history = accounts.clients[i].channel(destination.channel).history(20);
                Json result{{"messages_read", history.size()}, {"test_messages_verified", Json::array()}};
                for (const auto& message : messages) if (message) {
                    const auto fetched = accounts.clients[i].channel(destination.channel).fetch_message(message->id);
                    check(fetched.author.id == message->author.id && fetched.content == message->content);
                    result["test_messages_verified"].push_back(fetched.id);
                }
                check(!result["test_messages_verified"].empty()); return result;
            });
            step(std::string(label) + "_dm", [&] {
                const auto dm = accounts.clients[i].create_dm(accounts.users[1 - i].id);
                const auto sent = dm.send("[" + marker + "] DM send/read test from account " + label + " to the other test account.");
                report["steps"][std::string(label) + "_dm"]["send_accepted"] = true;
                report["steps"][std::string(label) + "_dm"]["message_id"] = sent.id;
                save();
                // Verify from the receiving account, not just the sender's echo.
                const auto received = accounts.clients[1 - i].channel(dm.id).fetch_message(sent.id);
                check(received.author.id == accounts.users[i].id && received.content == sent.content);
                return Json{{"channel_id", dm.id}, {"message_id", sent.id}, {"recipient_read_verified", true}};
            });
        }
        step("friend_request", [&] {
            const int a = peer_relationship(accounts.clients[0], accounts.users[1].id);
            const int b = peer_relationship(accounts.clients[1], accounts.users[0].id);
            Json result{{"before_A", a}, {"before_B", b}};
            report["steps"]["friend_request"].update(result); save();
            if (a == 1 && b == 1) {
                result["action"] = "skipped: already friends; existing friendship preserved";
                return result;
            }
            if ((a != 0 && a != 5) || (b != 0 && b != 5)) {
                result["action"] = "existing pending/blocked relationship preserved"; return result;
            }
            accounts.clients[0].send_friend_request(accounts.users[1].id);
            report["steps"]["friend_request"]["request_accepted"] = true; save();
            int after_a = 0, after_b = 0;
            for (int poll = 0; poll < 3; ++poll) {
                after_a = peer_relationship(accounts.clients[0], accounts.users[1].id);
                after_b = peer_relationship(accounts.clients[1], accounts.users[0].id);
                if (after_a == 4 && after_b == 3) break;
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
            }
            result["action"] = "A sent B a friend request"; result["after_A"] = after_a; result["after_B"] = after_b;
            result["pending_request_verified"] = after_a == 4 && after_b == 3;
            check(result["pending_request_verified"].get<bool>()); return result;
        });
        Json summary{{"run", marker}, {"accounts", report["accounts"]}, {"target", destination.label()},
            {"results", Json::object()}, {"relationship_types", {{"0", "none"}, {"1", "friends"}, {"2", "blocked"}, {"3", "incoming request"}, {"4", "outgoing request"}}}};
        for (auto it = report["steps"].begin(); it != report["steps"].end(); ++it)
            if (!it.key().ends_with("profile") && !it.key().ends_with("server_channel") && !it.key().ends_with("member")) summary["results"][it.key()] = it.value();
        const std::size_t reporter = report["steps"]["friend_request"].value("verification_required", false) ? 1 : 0;
        publish(reporter, "discord.cpp-self | Live test results", summary);
        publish(reporter, "Guild and channel details | Account A", report["steps"]["A_server_channel"]);
        publish(1, "Guild and channel details | Account B", report["steps"]["B_server_channel"]);
        publish(reporter, "Account B profile and membership | Viewed by A", {{"profile", report["steps"]["A_peer_profile"]}, {"member", report["steps"]["A_peer_member"]}});
        publish(1, "Account A profile and membership | Viewed by B", {{"profile", report["steps"]["B_peer_profile"]}, {"member", report["steps"]["B_peer_member"]}});
        report["report_delivery"] = attempt([&] {
            for (const auto& entry : report["report_messages"]) {
                const auto fetched = accounts.clients[1].channel(destination.channel).fetch_message(entry["id"].get<Snowflake>());
                check(fetched.channel_id == destination.channel);
            }
            return Json{{"messages_verified", report["report_messages"].size()}};
        });
        save();
        std::cout << "REPORT_SAVED " << output.string() << '\n' << "REPORT_MESSAGES " << report["report_messages"].size() << '\n';
        for (const auto& entry : report["report_messages"]) std::cout << entry["url"].get<std::string>() << '\n';
    }
    void complete_report() {
        // Resume reporting only. Never replay a friend request, DM or test send.
        for (std::size_t i = 0; i < 2; ++i) {
            const auto label = i == 0 ? "A" : "B";
            if (!accounts.authenticated[i]) {
                report["steps"][std::string(label) + "_authentication_after_test"] = {{"status", "failed"}, {"reason", "credential is no longer accepted; no further requests with this account"}};
                save(); continue;
            }
            step(std::string(label) + "_peer_user_after_fix", [&] {
                const auto user = accounts.clients[i].fetch_user(accounts.users[1 - i].id);
                check(user.id == accounts.users[1 - i].id);
                return Json{{"id", user.id}, {"username", user.name}, {"display_name", user.display_name}, {"public_flags", user.public_flags}, {"fallback", "profile route on 403/40001"}};
            });
        }
        step("relationship_after_test", [&] {
            Json states;
            for (std::size_t i = 0; i < 2; ++i)
                states[i == 0 ? "A" : "B"] = accounts.authenticated[i] ? Json(peer_relationship(accounts.clients[i], accounts.users[1 - i].id)) : Json(nullptr);
            return states;
        });
        Json summary{{"run", marker}, {"accounts", report["accounts"]}, {"target", destination.label()},
            {"results", Json::object()}, {"note", "Report delivery resumed by B. No friend request, DM, or test mutation was replayed."}};
        for (auto it = report["steps"].begin(); it != report["steps"].end(); ++it) {
            if (it.key().ends_with("profile") || it.key().ends_with("server_channel") || it.key().ends_with("member")) continue;
            summary["results"][it.key()] = pick(it.value(), {"status", "http_status", "discord_code", "reason", "messages_read", "test_messages_verified", "recipient_read_verified", "before_A", "before_B", "request_accepted", "A", "B"});
        }
        publish(1, "discord.cpp-self | Complete test report (B)", summary);
        const auto& server = report["steps"]["B_server_channel"];
        publish(1, "Guild and channel details | Retrieved by both accounts", server);
        publish(1, "Account B profile and membership | Visible to A", {{"profile", report["steps"]["A_peer_profile"]}, {"member", report["steps"]["A_peer_member"]}});
        publish(1, "Account A profile and membership | Visible to B", {{"profile", report["steps"]["B_peer_profile"]}, {"member", report["steps"]["B_peer_member"]}});
        report["report_delivery"] = attempt([&] {
            for (const auto& entry : report["report_messages"]) {
                const auto fetched = accounts.clients[1].channel(destination.channel).fetch_message(entry["id"].get<Snowflake>());
                check(fetched.channel_id == destination.channel);
            }
            return Json{{"messages_verified", report["report_messages"].size()}};
        });
        save();
        std::cout << "REPORT_DELIVERY " << report["report_delivery"].dump() << '\n';
        for (const auto& entry : report["report_messages"]) std::cout << entry["url"].get<std::string>() << '\n';
    }
};
}
int main(int argc, char** argv) {
    try {
        // No action flag means discovery only. Resuming reports never repeats tests.
        if (argc != 2 && !(argc == 4 && (std::string_view(argv[2]) == "--run" || std::string_view(argv[2]) == "--complete-report")))
            throw discord::Error("Usage: discord_self_account_tests <credential-file> [--run|--complete-report <report-file>]");
        Accounts accounts;
        const bool reporting_only = argc == 4 && std::string_view(argv[2]) == "--complete-report";
        load(accounts, argv[1], reporting_only);
        Json discovery;
        if (reporting_only && !accounts.authenticated[0]) {
            std::ifstream previous_file(argv[3]); Json previous; previous_file >> previous;
            const auto guild = accounts.clients[1].fetch_guild(previous.at("guild_id").get<Snowflake>());
            check(guild.name == test_guild_name());
            const auto channels = accounts.clients[1].request("GET", "/guilds/" + guild.id.str() + "/channels");
            discovery = {{"common_guilds", Json::array({{{"id", guild.id}, {"name", guild.name}, {"channels", channels}}})}};
        } else discovery = discover(accounts);
        if (argc == 2) std::cout << discovery.dump(2) << '\n';
        else if (std::string_view(argv[2]) == "--complete-report") Runner(accounts, target(discovery), argv[3], true).complete_report();
        else Runner(accounts, target(discovery), argv[3]).run();
    } catch (const discord::HttpError& error) {
        std::cerr << "HTTP_ERROR status=" << error.status << " code=" << error.code << '\n'; return 1;
    } catch (const discord::TransportError&) { std::cerr << "TRANSPORT_ERROR\n"; return 1; }
    catch (const std::exception&) { std::cerr << "ACCOUNT_TEST_ERROR\n"; return 1; }
}
