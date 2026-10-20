// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>
#include <iostream>

int main() {
    try {
        discord::Client client;
        // Keep the router alive for as long as its message callback can run.
        // Commands only accepts messages written by the logged-in account.
        discord::Commands commands(client, "!");
        commands.add("ping", [](const discord::Context& ctx) { (void)ctx.reply("pong"); });
        commands.add("echo", [](const discord::Context& ctx) {
            // arguments keeps the raw text; ctx.args contains parsed quoted words.
            if (!ctx.arguments.empty()) (void)ctx.send(ctx.arguments);
        });
        // close() stops the Gateway and makes the blocking run() call return.
        commands.add("quit", [&](const discord::Context&) { client.close(); });
        // Forward each message to the router; no manual event loop is needed.
        client.on_message([&](const discord::Message& message) { (void)commands.process(message); });
        client.on_ready([](const discord::Ready& ready) {
            std::cout << ready.user.name << ": !ping, !echo, !quit\n";
        });
        // A handled command error is logged without ending the connection.
        client.on_error([](const std::exception& error) {
            std::cerr << error.what() << '\n';
            // Stop when Discord requires account verification instead of retrying it.
            if (dynamic_cast<const discord::VerificationRequired*>(&error)) throw;
        });
        // The token is read from the environment and never printed by this example.
        client.run(discord::token_from_env());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
