// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#pragma once
#include "client.hpp"
#include <map>

namespace discord {
struct Context {
    Client& client;
    const Message& message;
    std::string name;
    std::string arguments;
    std::vector<std::string> args; // Quoted strings and backslash escapes supported.
    [[nodiscard]] Message reply(std::string text) const { return message.reply(std::move(text)); }
    [[nodiscard]] Message send(std::string text) const { return message.channel().send(std::move(text)); }
};

// Only the logged-in user's own messages can invoke commands.
class Commands {
public:
    explicit Commands(Client& client, std::string prefix = "!");
    Commands& add(std::string name, std::function<void(const Context&)> handler);
    [[nodiscard]] bool process(const Message& message) const;
    [[nodiscard]] std::vector<std::string> names() const;
private:
    Client& client_;
    std::string prefix_;
    std::map<std::string, std::function<void(const Context&)>, std::less<>> handlers_;
};
}
