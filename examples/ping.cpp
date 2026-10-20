// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#include <discord/discord.hpp>
#include <iostream>

int main() {
    try {
        discord::Client client;
        client.on_ready([](const discord::Ready& ready) {
            std::cout << "Connected as " << ready.user.name << '\n';
        });
        client.on_message([&](const discord::Message& message) {
            const auto self = client.user();
            if (self && message.author.id == self->id && message.content == "!ping") {
                const auto reply = message.reply("pong");
                std::cout << "Sent " << reply.id.str() << '\n';
            }
        });
        client.run(discord::token_from_env());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
