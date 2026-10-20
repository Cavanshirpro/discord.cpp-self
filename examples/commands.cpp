// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>
#include <iostream>

int main() {
    try {
        discord::Client client;
        discord::Commands commands(client, "!");
        commands.add("ping", [](const discord::Context& ctx) { (void)ctx.reply("pong"); });
        commands.add("echo", [](const discord::Context& ctx) {
            if (!ctx.arguments.empty()) (void)ctx.send(ctx.arguments);
        });
        commands.add("quit", [&](const discord::Context&) { client.close(); });
        client.on_message([&](const discord::Message& message) { (void)commands.process(message); });
        client.on_ready([](const discord::Ready& ready) {
            std::cout << ready.user.name << ": !ping, !echo, !quit\n";
        });
        client.on_error([](const std::exception& error) { std::cerr << error.what() << '\n'; });
        client.run(discord::token_from_env());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
