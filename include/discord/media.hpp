// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include "models.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <deque>

namespace discord {
/// @brief Options for the optional C++ RTP/DAVE media backend. All strings are UTF-8.
/// Files loop independently. A nonempty input format selects an FFmpeg capture
/// device instead (for example audio_format="dshow", audio_source="audio=Microphone").
struct MediaOptions {
    Snowflake guild_id;
    Snowflake channel_id;
    std::string ffmpeg = "ffmpeg"; ///< FFmpeg executable with libx264 and libopus.
    std::string video_source;
    std::string audio_source;
    std::string video_format;
    std::string audio_format;
    bool camera = true;
    bool go_live = true;
    bool loop = true;
    bool muted = false; ///< Suppress all outgoing file/device audio, independently of receiving.
    bool voice_audio = true; ///< Send the audio source to the voice channel.
    bool stream_audio = false; ///< Explicit opt-in; sending the same source twice can sound like echo.
    int width = 1280; ///< Even output width, 16..3840.
    int height = 720; ///< Even output height, 16..2160; source is fitted with letterboxing.
    int fps = 30; ///< 1..60.
    int bitrate_kbps = 2500; ///< Video bitrate, 100..20000.
    std::chrono::milliseconds connect_timeout{45'000};
    std::chrono::seconds duration{0}; ///< Zero runs until stop() or EOF.
    bool receive_audio = true; ///< Native backend joins with self_deaf=false and receives audio.
    bool receive_video = true; ///< Request cameras in the selected channel.
    bool play_received_audio = false; ///< Optional Windows waveOut playback, separate from microphone output.
    bool emit_media_packets = false; ///< Optional base64 payload callbacks; disabled to minimize copying.
    bool watch_streams = false; ///< Automatically watch other users' Go Live streams in this channel.
    std::filesystem::path recording_directory; ///< Empty disables recording; runtime default never records.
    int record_audio_seconds = 20;
    int record_video_seconds = 10;
    bool record_wait_for_media = false; ///< Start a configured recording after selected remote media arrives.
    std::filesystem::path cache_directory; ///< Native backend pre-encodes file video once and reuses it.
};

/// @brief An independent media Gateway session with camera, microphone and Go Live.
/// Requires DISCORD_SELF_NATIVE_MEDIA=ON, libdave, OpenSSL, Opus and native FFmpeg.
/// Uses no Node, Python or Rust runtime. Does not reuse a Client's Gateway. One active
/// media session per account is recommended. Physical capture is opt-in by format.
class MediaSession {
public:
    using Handler = std::function<void(const Json&)>;
    explicit MediaSession(MediaOptions options);
    /// @brief Whether this library was built with the C++ media backend enabled.
    [[nodiscard]] static bool available() noexcept;
    ~MediaSession() = default;
    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;
    /// @brief Block until playback ends or stop() is requested. Events run here.
    /// Token stays in this process and authenticated TLS connections, never in
    /// encoder arguments or temporary files. Throws Error on startup/transport failure.
    /// A throwing handler stops the session and its native encoder children.
    /// Join the caller before destruction. Concurrent/sequential reuse is rejected.
    void run(std::string token, Handler handler = {});
    /// @brief Request graceful shutdown; safe from another thread or a signal handler.
    void stop() noexcept { stop_.store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    /// @brief Latest visible participants, stream viewers, receive and transport statistics.
    /// IDs remain decimal strings. Secret/session/crypto material is excluded.
    [[nodiscard]] Json snapshot() const;
    /// @brief Native backend: watch a key from snapshot()["streams"]; only the current channel is allowed.
    void watch_stream(std::string stream_key);
    void unwatch_stream(std::string stream_key);
    /// @brief Native backend: change local deaf state without muting microphone output.
    void set_deaf(bool deaf);
    /// @brief Mute all outgoing audio without stopping video or received audio.
    void set_muted(bool muted);
    /// @brief Enable/disable local received-audio playback, including queued samples.
    /// Receiving/recording continues; deaf state overrides playback on every transport.
    void set_audio_playback(bool enabled);
    /// @brief Finish the active recording, preserving the received portion.
    void stop_recording();
    /// @brief Replace the outgoing files. Empty paths disable that media source.
    /// Existing playback continues while the replacement is prepared.
    void set_sources(std::string video, std::string audio);
    /// @brief Leave the current channel and join the requested guild voice channel.
    void join(Snowflake guild_id, Snowflake channel_id);
    /// @brief Native backend: start a finite recording of received audio/cameras/streams.
    void record(std::filesystem::path directory, int audio_seconds = 20, int video_seconds = 10);
private:
    void command(Json value);
    MediaOptions options_;
    std::atomic_bool stop_{false}, running_{false}, used_{false};
    mutable std::mutex state_mutex_;
    Json snapshot_ = Json::object();
    std::deque<Json> commands_;
};
}
