// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#endif
#include "media_config.hpp"
#include <iostream>
#include <thread>
namespace {
std::atomic_bool interrupted = false;
#ifdef _WIN32
BOOL WINAPI interrupt(DWORD e) {
    if (e == CTRL_C_EVENT || e == CTRL_BREAK_EVENT || e == CTRL_CLOSE_EVENT) {
        interrupted = true;
        return TRUE;
    }
    return FALSE;
}
#else
void interrupt(int) { interrupted = true; }
#endif
int execute(const std::vector<std::filesystem::path> &args) {
    try {
        if (args.size() != 3 && args.size() != 4) {
            std::cerr << "Usage: discord_self_media CONFIG.json TOKEN_FILE [DURATION_SECONDS]\n";
            return 2;
        }
        auto options = media_example::config(args[1]);
        if (args.size() == 4)
            options.duration = std::chrono::seconds{std::stoi(args[3].string())};
        discord::MediaSession session(std::move(options));
#ifdef _WIN32
        SetConsoleCtrlHandler(interrupt, TRUE);
#else
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
#endif
        std::jthread monitor([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                if (interrupted) {
                    session.stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
            }
        });
        try {
            session.run(media_example::token(args[2]), [](const discord::Json &event) {
                std::cout << event.dump() << '\n' << std::flush;
            });
        } catch (...) {
            monitor.request_stop();
            throw;
        }
        monitor.request_stop();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Media: " << e.what() << '\n';
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) { return execute({argv, argv + argc}); }
#else
int main(int argc, char **argv) { return execute({argv, argv + argc}); }
#endif
