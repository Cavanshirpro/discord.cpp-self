// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>
#include <atomic>
#include <iostream>
#include <thread>

// Opt-in live probe. Reads the environment, prints no identity, message or token,
// and never sends a message, reaction, typing indicator or presence update.
int main() {
    try {
        const auto token = discord::token_from_env();
        // Keep the probe short and fail directly instead of reconnecting repeatedly.
        discord::ClientOptions options;
        options.request_timeout = std::chrono::seconds{15};
        options.max_reconnect_attempts = 0;
        discord::Client client(options);
        client.login(token);
        // REST-only authentication happens before the Gateway connectivity check.
        const auto me = client.fetch_me();
        if (me.bot) throw discord::Error("Expected a user account");
        std::cout << "REST_ME_OK\n" << std::flush;
        (void)client.fetch_guilds();
        std::cout << "REST_GUILDS_OK\n" << std::flush;
        std::atomic_bool ready{false}, acknowledged{false}, timed_out{false};
        // The monitor and callbacks run on different threads, so flags are atomic.
        client.on_ready([&](const discord::Ready& event) {
            if (event.user.id != me.id || event.user.bot) throw discord::Error("Unexpected Gateway identity");
            ready = true;
            std::cout << "GATEWAY_READY_OK\n" << std::flush;
        });
        std::jthread monitor([&](std::stop_token stop) {
            // A non-negative latency means at least one heartbeat ACK was received.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
            while (!stop.stop_requested()) {
                if (ready && client.latency().count() >= 0) {
                    acknowledged = true;
                    client.close();
                    return;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    timed_out = true;
                    client.close();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
            }
        });
        client.run(token);
        // jthread joins before client destruction, including exception unwinding.
        monitor.request_stop();
        if (timed_out || !acknowledged) throw discord::Error("No authenticated heartbeat acknowledgement");
        std::cout << "HEARTBEAT_ACK_OK\n";
        return 0;
    } catch (const discord::HttpError& error) {
        std::cerr << "HTTP_CHECK_FAILED status=" << error.status << " code=" << error.code << '\n';
    } catch (const discord::GatewayError& error) {
        std::cerr << "GATEWAY_CHECK_FAILED code=" << error.code << '\n';
    } catch (const discord::TransportError&) {
        std::cerr << "TRANSPORT_CHECK_FAILED\n";
    } catch (const discord::ProtocolError&) {
        std::cerr << "PROTOCOL_CHECK_FAILED\n";
    } catch (const std::exception&) {
        std::cerr << "CONNECTION_CHECK_FAILED\n";
    }
    return 1;
}
