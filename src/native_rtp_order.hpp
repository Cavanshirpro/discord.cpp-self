// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include "native_media_common.hpp"
namespace discord::detail::media {
struct OrderedRtp {
    uint32_t ssrc = 0, timestamp = 0;
    uint16_t sequence = 0;
    int payload_type = 0;
    bool marker = false;
    std::string user;
    Bytes payload;
    Clock::time_point arrival = Clock::now();
};
// A bounded repair window. Sequence comparisons deliberately use signed 16-bit
// distance so wraparound, late RTX and duplicate primary packets are handled alike.
class RtpOrder {
    std::map<uint16_t, OrderedRtp> pending_;
    uint16_t next_ = 0, initial_ = 0;
    bool initialized_ = false;
    Clock::time_point missing_since_{}, nack_due_{};

  public:
    uint64_t duplicate = 0, skipped = 0, reordered = 0, delivered = 0;
    uint32_t extended_high_sequence() const { return uint32_t(initial_ + delivered + skipped - 1); }
    void push(OrderedRtp p) {
        if (!initialized_) {
            next_ = p.sequence;
            initial_ = p.sequence;
            initialized_ = true;
        }
        const int16_t distance = int16_t(p.sequence - next_);
        if (distance < 0) {
            ++duplicate;
            return;
        }
        if (distance > 2048 || pending_.size() >= 512) {
            skipped += uint16_t(p.sequence - next_);
            pending_.clear();
            next_ = p.sequence;
            missing_since_ = {};
        }
        if (distance > 0)
            ++reordered;
        if (!pending_.emplace(p.sequence, std::move(p)).second)
            ++duplicate;
    }
    void drain(Clock::time_point now, const std::function<void(OrderedRtp)> &deliver,
               const std::function<void(uint16_t)> &nack) {
        for (size_t count = 0; count < 512 && !pending_.empty(); ++count) {
            auto found = pending_.find(next_);
            if (found != pending_.end()) {
                auto p = std::move(found->second);
                pending_.erase(found);
                ++next_;
                missing_since_ = {};
                ++delivered;
                deliver(std::move(p));
                continue;
            }
            if (missing_since_ == Clock::time_point{}) {
                missing_since_ = now;
                nack_due_ = now + std::chrono::milliseconds{8};
            }
            if (now >= nack_due_) {
                nack(next_);
                nack_due_ = now + std::chrono::milliseconds{20};
            }
            if (now - missing_since_ < std::chrono::milliseconds{80})
                break;
            auto nearest = pending_.begin();
            for (auto it = pending_.begin(); it != pending_.end(); ++it)
                if (uint16_t(it->first - next_) < uint16_t(nearest->first - next_))
                    nearest = it;
            skipped += uint16_t(nearest->first - next_);
            next_ = nearest->first;
            missing_since_ = {};
        }
    }
};
} // namespace discord::detail::media
