// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <discord/discord.hpp>
#include <fstream>
namespace media_example {
inline std::filesystem::path path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
inline std::string text(const std::filesystem::path &p) {
    auto s = p.u8string();
    return {s.begin(), s.end()};
}
inline std::string token(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw discord::Error("Cannot open token file");
    std::string s((std::istreambuf_iterator<char>(f)), {});
    if (s.starts_with("\xef\xbb\xbf"))
        s.erase(0, 3);
    auto end = s.find_last_not_of(" \t\r\n");
    if (end == std::string::npos)
        throw discord::Error("Token file is empty");
    s.resize(end + 1);
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    return s;
}
inline discord::MediaOptions config(const std::filesystem::path &p) {
    std::ifstream f(p);
    if (!f)
        throw discord::Error("Cannot open media config");
    auto c = discord::Json::parse(f);
    discord::MediaOptions o;
    o.guild_id = c.at("guild_id").get<discord::Snowflake>();
    o.channel_id = c.at("channel_id").get<discord::Snowflake>();
    if (c.contains("worker") || c.contains("runtime") || c.contains("node"))
        throw discord::Error("Legacy helper configuration is unsupported; use the C++ media configuration");
    o.ffmpeg = c.value("ffmpeg", std::string{"ffmpeg"});
    o.video_source = c.value("video_source", std::string{});
    o.audio_source = c.value("audio_source", std::string{});
    o.video_format = c.value("video_format", std::string{});
    o.audio_format = c.value("audio_format", std::string{});
    o.camera = c.value("camera", true);
    o.go_live = c.value("go_live", true);
    o.loop = c.value("loop", true);
    o.width = c.value("width", 1280);
    o.height = c.value("height", 720);
    o.fps = c.value("fps", 30);
    o.bitrate_kbps = c.value("bitrate_kbps", 1800);
    o.receive_audio = c.value("receive_audio", true);
    o.receive_video = c.value("receive_video", true);
    o.play_received_audio = c.value("play_received_audio", false);
    o.emit_media_packets = c.value("emit_media_packets", false);
    o.watch_streams = c.value("watch_streams", false);
    o.recording_directory = path(c.value("recording_directory", std::string{}));
    o.cache_directory = path(c.value("cache_directory", std::string{}));
    o.record_audio_seconds = c.value("record_audio_seconds", 20);
    o.record_video_seconds = c.value("record_video_seconds", 10);
    o.record_wait_for_media = c.value("record_wait_for_media", false);
    o.connect_timeout = std::chrono::milliseconds{c.value("connect_timeout_ms", 45000)};
    o.duration = std::chrono::seconds{c.value("duration_seconds", 0)};
    return o;
}
} // namespace media_example
