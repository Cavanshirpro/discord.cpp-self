// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <discord/media.hpp>
namespace discord::detail {
void run_native_media(MediaOptions options, std::string token, std::atomic_bool &stop,
                      const MediaSession::Handler &emit, const std::function<std::deque<Json>()> &commands);
}
