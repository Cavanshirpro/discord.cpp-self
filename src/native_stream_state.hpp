// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <cstdint>
namespace discord::detail::media {
// A stopped stream must not attach an outstanding transport or race a new create
// against its STREAM_DELETE acknowledgement. Voice has an independent lifetime.
struct StreamState {
    bool requested = false, stopping = false;
    std::uint64_t epoch = 0;
    bool begin() {
        if (requested || stopping) return false;
        requested = true;
        ++epoch;
        return true;
    }
    bool stop() {
        if (!requested) return false;
        requested = false;
        stopping = true;
        ++epoch;
        return true;
    }
    void reset() { requested = stopping = false; ++epoch; }
    bool accepts(std::uint64_t candidate) const { return requested && !stopping && candidate == epoch; }
};
}
