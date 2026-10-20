// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>
#include <iostream>

int main() {
    try {
        // The client owns the HTTP transport, Gateway connection and message cache.
        discord::Client client;
        // Register callbacks before run(); READY supplies the logged-in account.
        client.on_ready([](const discord::Ready& ready) {
            std::cout << "Connected as " << ready.user.name << '\n';
        });
        client.on_message([&](const discord::Message& message) {
            const auto self = client.user();
            // Only your own !ping invokes this command. Other accounts are ignored.
            if (self && message.author.id == self->id && message.content == "!ping") {
                // reply() preserves the reference and disables mention notifications.
                const auto reply = message.reply("pong");
                std::cout << "Sent " << reply.id.str() << '\n';
            }
        });
        // This blocks and keeps receiving events; Gateway heartbeats run separately.
        // Set DISCORD_TOKEN in the environment instead of putting it in this file.
        client.run(discord::token_from_env());
    } catch (const std::exception& error) {
        // Unhandled connection/callback errors reach here and produce a failing exit.
        std::cerr << error.what() << '\n';
        return 1;
    }
}
