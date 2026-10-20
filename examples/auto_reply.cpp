// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include "auto_reply_handler.hpp"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <thread>

namespace {
std::atomic_flag interrupted = ATOMIC_FLAG_INIT;
void request_stop(int) {
    // Signal handlers cannot safely take the locks used by client.close().
    // atomic_flag is lock-free; an ordinary thread performs shutdown below.
    interrupted.test_and_set(std::memory_order_relaxed);
}
}

int main() {
    try {
        // Copy the ID of one text channel (or an existing DM) you want to use.
        // The token stays in the environment; neither credential nor message text is logged.
        const auto token = discord::token_from_env();
        const auto* channel_value = std::getenv("DISCORD_REPLY_CHANNEL");
        if (!channel_value || !*channel_value)
            throw std::invalid_argument("Set DISCORD_REPLY_CHANNEL to the channel ID to answer in");
        const discord::Snowflake channel{std::string_view(channel_value)};

        discord::Client client;
        examples::AutoReplyHandler responder(client, channel);
        bool failed = false;

        // Handlers run serially on run()'s thread, so responder's state needs no mutex.
        // Both the handler and its client outlive every callback registered here.
        client.on_message([&](const discord::Message& message) { responder(message); });
        client.on_ready([&](const discord::Ready& ready) {
            if (ready.user.bot) {
                std::cerr << "This example expects a user account.\n";
                failed = true;
                client.close();
                return;
            }
            std::cout << "Auto-reply is listening. Owner commands: !autoreply on, off, status, quit.\n" << std::flush;
        });
        client.on_resumed([] { std::cout << "Gateway session resumed.\n" << std::flush; });
        client.on_error([&](const std::exception& error) {
            // A normal callback failure is logged and the process keeps listening.
            if (interrupted.test(std::memory_order_relaxed) && dynamic_cast<const discord::Cancelled*>(&error)) return;
            std::cerr << "Callback error: " << error.what() << '\n';
            const auto* http = dynamic_cast<const discord::HttpError*>(&error);
            if (dynamic_cast<const discord::VerificationRequired*>(&error) ||
                (http && (http->status == 401 || http->status == 403))) {
                // Authentication, access and verification failures need attention;
                // repeatedly reconnecting or sending again will not resolve them.
                failed = true;
                client.close();
            }
        });

        std::signal(SIGINT, request_stop);  // Ctrl+C in the terminal.
        std::signal(SIGTERM, request_stop); // A normal service-stop signal on POSIX.
        std::jthread shutdown([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                // Recheck until run() returns, covering a signal received during startup.
                if (interrupted.test(std::memory_order_relaxed)) client.close();
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
        });

        // No polling loop or sleep belongs in the message callback. run() keeps the
        // program connected, with heartbeats and bounded reconnect/resume built in.
        // Fatal Gateway errors or an exhausted reconnect budget reach the outer catch.
        client.run(token);
        shutdown.request_stop();
        std::cout << "Auto-reply stopped.\n";
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
