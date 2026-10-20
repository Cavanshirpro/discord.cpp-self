// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>
#include <iostream>

int main() {
    try {
        discord::Client client;
        client.login(discord::token_from_env());
        std::cout << "Account: " << client.fetch_me().display_name << '\n';
        for (const auto& guild : client.fetch_guilds())
            std::cout << guild.id.str() << " " << guild.name << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
