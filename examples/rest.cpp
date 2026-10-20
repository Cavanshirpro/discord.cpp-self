// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>
#include <iostream>

int main() {
    try {
        discord::Client client;
        // login() configures REST authentication without opening a Gateway session.
        client.login(discord::token_from_env());
        // REST calls are synchronous. fetch_me() is the first credential check here;
        // client.user() would require Gateway READY and is not used in this example.
        std::cout << "Account: " << client.fetch_me().display_name << '\n';
        // Models expose typed fields; keep Snowflake IDs as strings when displaying.
        for (const auto& guild : client.fetch_guilds())
            std::cout << guild.id.str() << " " << guild.name << '\n';
    } catch (const std::exception& error) {
        // A failed HTTP request throws; there is no event loop or background retry.
        std::cerr << error.what() << '\n';
        return 1;
    }
}
