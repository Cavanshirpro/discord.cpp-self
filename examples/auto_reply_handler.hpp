// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <discord/discord.hpp>
#include <algorithm>
#include <cctype>
#include <deque>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace examples {
// This helper belongs to the example, not the library's public API. Keeping the
// message policy here lets the offline fixture exercise the actual responder.
class AutoReplyHandler {
public:
    AutoReplyHandler(discord::Client& client, discord::Snowflake channel)
        : client_(client), channel_(channel) {
        if (!channel_) throw std::invalid_argument("The reply channel ID must be nonzero");
    }

    void operator()(const discord::Message& message) {
        // Restrict all replies and owner commands to the explicitly chosen channel.
        if (message.channel_id != channel_ || message.author.bot) return;
        const auto self = client_.user();
        if (!self) return;
        const auto text = normalize(message.content);

        if (message.author.id == self->id) {
            // Only your own messages control this process. Reply messages written by
            // this account return here too, so they cannot trigger another reply.
            if (text == "!autoreply on") enabled_ = true;
            else if (text == "!autoreply off") enabled_ = false;
            else if (text == "!autoreply quit") { client_.close(); return; }
            else if (text != "!autoreply status") return;
            std::cout << "Auto-reply is " << (enabled_ ? "on" : "off") << ".\n" << std::flush;
            return;
        }
        if (!enabled_) return;
        const auto reply = response(text);
        if (reply.empty()) return; // Unknown text stays unanswered; edit response() below.

        // Remember the last 512 recognized message IDs, including throttled ones.
        // A replay after reconnect must not create a second reply within this window.
        if (!seen_.insert(message.id.value()).second) return;
        recent_.push_back(message.id.value());
        if (recent_.size() > 512) { seen_.erase(recent_.front()); recent_.pop_front(); }

        const auto now = Clock::now();
        if (now < next_reply_) return;
        // Expire entries so a process running for days does not accumulate user IDs.
        std::erase_if(last_reply_, [&](const auto& item) { return now - item.second >= user_cooldown; });
        if (last_reply_.contains(message.author.id.value())) return;

        // Reserve both cooldowns before sending: a failed mutation is not replayed
        // by this example. These limits are additional to Discord's HTTP rate limits.
        next_reply_ = now + global_cooldown;
        last_reply_[message.author.id.value()] = now;
        const auto finish_attempt = [&] {
            // A slow HTTP request must not consume the cooldown while still sending.
            const auto finished = Clock::now();
            next_reply_ = finished + global_cooldown;
            last_reply_[message.author.id.value()] = finished;
        };
        try { (void)message.reply(std::string(reply)); } // Mentions and reply pings default to off.
        catch (...) { finish_attempt(); throw; }
        finish_attempt();
        std::cout << "Auto-reply sent.\n" << std::flush;
    }

private:
    using Clock = std::chrono::steady_clock; // Wall-clock changes do not affect cooldowns.
    static constexpr auto global_cooldown = std::chrono::seconds{2};
    static constexpr auto user_cooldown = std::chrono::seconds{30};
    discord::Client& client_;
    discord::Snowflake channel_;
    bool enabled_ = true;
    Clock::time_point next_reply_{};
    std::unordered_map<std::uint64_t, Clock::time_point> last_reply_;
    std::unordered_set<std::uint64_t> seen_;
    std::deque<std::uint64_t> recent_;

    static std::string normalize(std::string text) {
        // These example triggers are ASCII: trim whitespace and ignore letter case.
        // UTF-8 text is otherwise left intact; this is not Unicode case folding.
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            return ch < 128 ? static_cast<char>(std::tolower(ch)) : static_cast<char>(ch);
        });
        return text;
    }

    static std::string_view response(std::string_view text) {
        // Customize these triggers and replies. Matching is exact after normalization.
        if (text == "hello" || text == "hi") return "Hello! Try !help to see what I can answer.";
        if (text == "!ping") return "Pong!";
        if (text == "!help") return "Try hello, hi, !ping or !about.";
        if (text == "!about") return "An auto-reply example built with discord.cpp-self.";
        return {};
    }
};
}
