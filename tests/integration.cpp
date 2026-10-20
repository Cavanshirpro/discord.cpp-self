// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#include <discord/discord.hpp>
#include <atomic>
#include <iostream>
#include <thread>

void check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Expected API URL, Gateway URL and mode");
        const std::string mode = argv[3];
        discord::ClientOptions options;
        options.api_url = argv[1]; options.gateway_url = argv[2];
        options.request_timeout = std::chrono::seconds{3};
        const bool timeout_mode = mode == "missing_ack" || mode == "no_ready" || mode == "no_hello";
        options.max_reconnect_attempts = timeout_mode ? 0 : 2;
        if (mode == "no_ready" || mode == "no_hello") options.request_timeout = std::chrono::milliseconds{250};
        if (mode == "overflow") options.event_queue_size = 2;
        discord::Client client(options);
        if (mode == "cancel") {
            client.login("offline-token");
            std::jthread closer([&] { std::this_thread::sleep_for(std::chrono::milliseconds{150}); client.close(); });
            const auto start = std::chrono::steady_clock::now();
            try { (void)client.request("GET", "/cancel"); throw std::runtime_error("Expected cancellation"); }
            catch (const discord::Cancelled&) {}
            check(std::chrono::steady_clock::now() - start < std::chrono::seconds{2}, "Cancellation was not prompt");
            std::cout << "cancel passed\n";
            return 0;
        }
        int ready = 0, messages = 0, edits = 0, deletes = 0, resumed = 0, commands = 0, errors = 0;
        discord::Commands router(client);
        router.add("echo", [&](const discord::Context& ctx) {
            check(ctx.args == std::vector<std::string>{"two words", "three"}, "Quoted arguments failed"); ++commands;
        });
        client.on_ready([&](const discord::Ready& value) {
            ++ready; check(value.user.id == discord::Snowflake{100}, "Incorrect account");
            if (mode == "overflow") std::this_thread::sleep_for(std::chrono::milliseconds{300});
            if (mode == "callback") throw std::runtime_error("callback failure");
            if (mode != "basic") return;
            check(client.fetch_me().name == "cavan", "REST failed");
            const auto sent = client.send(200, "hello");
            check(sent.id == discord::Snowflake{900}, "Send failed");
            check(sent.reply("reply").id == discord::Snowflake{900}, "Reply failed");
            check(sent.edit("edited").content == "edited", "Edit failed");
            sent.react("a:b"); sent.unreact("a:b");
            check(client.channel(200).history(1).size() == 1, "History failed");
            check(client.channel(200).fetch_message(900).id == discord::Snowflake{900}, "User message lookup failed");
            for (const auto missing : {901, 902}) {
                bool not_found = false;
                try { (void)client.channel(200).fetch_message(missing); }
                catch (const discord::HttpError& error) { not_found = error.status == 404 && error.code == 10008; }
                check(not_found, "A nearby or missing message must not be returned");
            }
            check(client.fetch_guilds().size() == 1, "User guild lookup failed");
            check(client.fetch_guild(400).approximate_member_count == 2, "Guild details failed");
            check(client.fetch_member(400, 100).user.id == discord::Snowflake{100}, "Member lookup failed");
            check(client.fetch_profile(100, 400).bio == "fixture", "Profile lookup failed");
            check(client.fetch_user(101).id == discord::Snowflake{101}, "Profile fallback for a denied bare user route failed");
            check(client.fetch_relationships().front().type == discord::RelationshipType::implicit, "Relationship lookup failed");
            client.send_friend_request(100);
            check(client.fetch_channel(200).id == discord::Snowflake{200}, "Fetch channel failed");
            check(client.create_dm(100).id == discord::Snowflake{200}, "DM failed");
            client.channel(200).typing(); sent.remove();
        });
        client.on_message([&](const discord::Message& message) {
            ++messages; (void)router.process(message);
            if (message.id == discord::Snowflake{301}) throw std::runtime_error("handled callback error");
        });
        client.on_error([&](const std::exception& error) {
            if (std::string(error.what()) != "handled callback error") throw;
            ++errors;
        });
        client.on_message_edit([&](const discord::MessageUpdate& event) {
            ++edits;
            check(event.before && event.before->content == "!echo \"two words\" three", "Missing before message");
            check(event.after && event.after->content == "changed", "Partial update failed");
            check(event.after->author.id == discord::Snowflake{100}, "Partial update lost author");
        });
        client.on_message_delete([&](const discord::MessageDelete& event) {
            ++deletes; check(event.cached.has_value(), "Delete lost cached message");
        });
        client.on_resumed([&] { ++resumed; });
        client.on_raw_event([&](const discord::RawEvent& event) { if (event.name == "FINISH") client.close(); });
        client.on_raw_event([&](const discord::RawEvent& event) {
            if (event.name == "USER_UPDATE") {
                const auto user = client.user();
                check(user && user->name == "cavan" && user->display_name == "updated", "Partial user update lost fields");
            }
        });
        bool failed = false;
        try { client.run("offline-token"); }
        catch (const discord::GatewayError& error) { check(mode == "fatal" && error.code == 4004, "Unexpected Gateway error"); failed = true; }
        catch (const discord::TransportError&) { check(timeout_mode, "Unexpected transport error"); failed = true; }
        catch (const discord::Error& error) { check(mode == "overflow" && std::string(error.what()).find("queue") != std::string::npos, "Unexpected library error"); failed = true; }
        catch (const std::runtime_error& error) { check(mode == "callback" && std::string(error.what()) == "callback failure", "Unexpected callback error"); failed = true; }
        check(!client.is_running(), "run() did not reset state");
        if (mode == "rerun") {
            client.run("offline-token");
            check(ready == 2 && !client.is_running(), "Client could not run twice");
        }
        if (mode == "basic") {
            check(ready == 1 && resumed == 1, "Resume flow failed");
            check(messages == 2 && commands == 1 && edits == 1 && deletes == 2 && errors == 1, "Event dispatch failed");
            check(!client.cached_message(300) && !client.cached_message(301), "Deleted messages remained cached");
        } else if (mode == "invalid") check(ready == 2, "Invalid session did not identify again");
        else if (mode == "rerun") check(!failed, "Unexpected rerun error");
        else check(failed, "Expected terminal error");
        std::cout << mode << " passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
