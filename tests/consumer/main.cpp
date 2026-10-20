// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#include <discord/discord.hpp>
#include <iostream>

int main() {
    discord::Client client;
    discord::Commands commands(client);
    commands.add("ping", [](const auto&) {});
    std::cout << discord::version << '\n';
    return commands.names().size() == 1 ? 0 : 1;
}
