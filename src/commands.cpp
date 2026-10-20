// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#include "internal.hpp"
#include <cctype>

namespace discord::detail {
std::vector<std::string> split_arguments(std::string_view input) {
    std::vector<std::string> result;
    std::string word;
    char quote = 0;
    bool escaped = false, started = false;
    for (const char c : input) {
        if (escaped) { word += c; escaped = false; started = true; }
        else if (c == '\\') { escaped = true; started = true; }
        else if (quote) { if (c == quote) quote = 0; else word += c; }
        else if (c == '\'' || c == '"') { quote = c; started = true; }
        else if (std::isspace(static_cast<unsigned char>(c))) {
            if (started) { result.push_back(std::move(word)); word.clear(); started = false; }
        } else { word += c; started = true; }
    }
    if (escaped || quote) throw std::invalid_argument("Unfinished quote or escape in command arguments");
    if (started) result.push_back(std::move(word));
    return result;
}
}

namespace discord {
Commands::Commands(Client& client, std::string prefix) : client_(client), prefix_(std::move(prefix)) {
    if (prefix_.empty()) throw std::invalid_argument("Command prefix cannot be empty");
}
Commands& Commands::add(std::string name, std::function<void(const Context&)> handler) {
    if (client_.is_running()) throw Error("Register commands before run()");
    if (name.empty() || name.find_first_of(" \t\r\n\\\"'") != std::string::npos || !handler)
        throw std::invalid_argument("Invalid command name or handler");
    if (!handlers_.emplace(std::move(name), std::move(handler)).second)
        throw std::invalid_argument("Command is already registered");
    return *this;
}
bool Commands::process(const Message& message) const {
    const auto self = client_.user();
    if (!self || message.author.id != self->id || !message.content.starts_with(prefix_)) return false;
    auto input = std::string_view(message.content).substr(prefix_.size());
    const auto end = input.find_first_of(" \t\r\n");
    const auto name = input.substr(0, end);
    const auto found = handlers_.find(name);
    if (found == handlers_.end()) return false;
    auto arguments = end == std::string_view::npos ? std::string_view{} : input.substr(end);
    const auto first = arguments.find_first_not_of(" \t\r\n");
    arguments = first == std::string_view::npos ? std::string_view{} : arguments.substr(first);
    Context context{client_, message, std::string(name), std::string(arguments), detail::split_arguments(arguments)};
    found->second(context);
    return true;
}
std::vector<std::string> Commands::names() const {
    std::vector<std::string> result;
    for (const auto& [name, handler] : handlers_) { (void)handler; result.push_back(name); }
    return result;
}
}
