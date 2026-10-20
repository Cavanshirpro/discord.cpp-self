// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include "client.hpp"
#include <map>

namespace discord {
/// @brief Command invocation context. References are valid only during the callback.
struct Context {
    Client& client; ///< Client running the command.
    const Message& message; ///< Original command message; do not retain this reference.
    std::string name; ///< Matched command name without the prefix.
    std::string arguments; ///< Argument text with leading separating whitespace removed.
    std::vector<std::string> args; ///< Parsed words; single/double quotes and backslash escapes are supported.
    /// @brief Reply to the command message with mention notifications disabled.
    [[nodiscard]] Message reply(std::string text) const { return message.reply(std::move(text)); }
    /// @brief Send a new message to the command's channel without a reply reference.
    [[nodiscard]] Message send(std::string text) const { return message.channel().send(std::move(text)); }
};

/// @brief Case-sensitive prefix router accepting only the logged-in user's messages.
/// Forward MESSAGE_CREATE events to process(). Keep the router alive while its
/// callback can run, and register commands before Client::run().
class Commands {
public:
    /// @brief Create a router bound to a client and a nonempty prefix.
    /// @param client The client; must outlive this router.
    /// @param prefix Text introducing commands, such as !.
    explicit Commands(Client& client, std::string prefix = "!");
    /// @brief Register a unique command name and callback; returns this router.
    /// @throws std::invalid_argument For invalid/duplicate names or an empty callback.
    Commands& add(std::string name, std::function<void(const Context&)> handler);
    /// @brief Parse and execute a matching own-message command synchronously.
    /// @return True if a command ran; false for other users, missing prefixes or unknown names.
    /// @throws std::invalid_argument For unfinished argument quotes/escapes; callback errors propagate.
    [[nodiscard]] bool process(const Message& message) const;
    /// @brief Return registered command names in lexicographical order.
    [[nodiscard]] std::vector<std::string> names() const;
private:
    Client& client_;
    std::string prefix_;
    std::map<std::string, std::function<void(const Context&)>, std::less<>> handlers_;
};
}
