// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include "native_recording.hpp"
#include <iostream>
#include <numbers>
using namespace discord;
using namespace discord::detail::media;
void require(bool value, const char *message) {
    if (!value)
        throw Error(message);
}
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Expected scratch directory and ffmpeg");
        auto root = std::filesystem::absolute(path_from(argv[1]));
        std::filesystem::create_directories(root);
        std::atomic_bool cancel = false;
        TransportCrypto crypto;
        crypto.key(Json::array({0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}));
        Bytes head{0x90, 101, 0, 9, 0, 0, 1, 2, 0, 0, 3, 4, 0xBE, 0xDE, 0, 1},
            plain{0xB2, '1', '0', '0', 1, 2, 3, 4, 5};
        auto packet = crypto.encrypt(head, plain);
        require(crypto.decrypt(packet, head.size()) == plain, "AES RTP roundtrip failed");
        require(crypto.encrypt(head, plain) != packet, "Transport nonce reused");
        packet[5] ^= 1;
        require(crypto.decrypt(packet, head.size()).empty(), "Tampered RTP header accepted");
        packet[5] ^= 1;
        packet[head.size() + 1] ^= 1;
        require(crypto.decrypt(packet, head.size()).empty(), "Tampered ciphertext accepted");
        Bytes annex{0, 0, 0, 1, 0x67, 1, 2, 0, 0, 1, 0x68, 3, 0, 0, 0, 1, 0x65, 4, 5};
        require(nals(annex).size() == 3, "Annex B mixed start codes failed");
        RtpOrder order;
        const std::map<uint32_t, RemoteSource> remote{{1, {"remote", 1, 0, "opus"}},
                                                      {2, {"remote", 2, 3, "H264"}},
                                                      {3, {"remote", 2, 3, "H264"}},
                                                      {4, {"self", 4, 0, "opus"}},
                                                      {5, {"remote", 5, 6, "H264"}}};
        const std::map<std::string, uint32_t> selected{{"remote", 2}};
        const auto wants = receiver_wants(remote, selected, "self", true, true, 921600);
        require(wants["1"] == 100 && wants["2"] == 100 && wants["any"] == 0,
                "Audio subscription was disabled by the video default");
        require(!wants.contains("3") && !wants.contains("4") && !wants.contains("5"),
                "Unselected/own/RTX stream subscribed");
        const auto audio_only = receiver_wants(remote, selected, "self", true, false, 921600);
        require(audio_only.contains("1") && !audio_only.contains("2"),
                "Audio-only subscription requested video");
        require(!receiver_wants(remote, selected, "self", false, true, 921600).contains("1"),
                "Disabled audio was subscribed");
        MediaOptions routing;
        routing.audio_source = "tone.opus";
        require(audio_output_enabled(routing, false) && !audio_output_enabled(routing, true),
                "Default audio duplicated into microphone and Go Live");
        routing.stream_audio = true;
        require(audio_output_enabled(routing, true), "Explicit stream audio was unavailable");
        routing.muted = true;
        require(!audio_output_enabled(routing, false) && !audio_output_enabled(routing, true),
                "Muting did not suppress both outgoing transports");
        std::vector<uint16_t> received;
        const auto now = Clock::now();
        auto push = [&](uint16_t sequence) {
            OrderedRtp p;
            p.sequence = sequence;
            order.push(std::move(p));
        };
        auto deliver = [&](OrderedRtp p) { received.push_back(p.sequence); };
        push(65534);
        order.drain(now, deliver, [](uint16_t) {});
        push(0);
        push(65535);
        push(65535);
        order.drain(now, deliver, [](uint16_t) {});
        require(received == std::vector<uint16_t>{65534, 65535, 0}, "RTP reorder/RTX wrap failed");
        push(2);
        int nacks = 0;
        order.drain(now, deliver, [&](uint16_t seq) {
            require(seq == 1, "Wrong NACK sequence");
            ++nacks;
        });
        order.drain(now + std::chrono::milliseconds{15}, deliver, [&](uint16_t) { ++nacks; });
        require(nacks == 1 && received.size() == 3, "Missing packet repair window failed");
        push(1);
        order.drain(now + std::chrono::milliseconds{20}, deliver, [](uint16_t) {});
        require(received.back() == 2 && order.skipped == 0, "RTX did not repair a missing packet");
        require(order.extended_high_sequence() == 65538, "RTCP extended sequence failed across wrap");
        MediaOptions o;
        o.guild_id = 1;
        o.channel_id = 2;
        o.camera = false;
        o.go_live = false;
        o.ffmpeg = argv[2];
        o.play_received_audio = false;
        std::vector<Json> events;
        Recorder recorder(o, [&](const Json &e) { events.push_back(e); });
        auto dir = root / ("record-" + std::to_string(random32()));
        recorder.start(dir, 20, 0);
        int error = 0;
        auto enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
        require(enc && !error, "Opus encoder unavailable");
        std::vector<int16_t> tone(1920);
        for (size_t i = 0; i < 960; ++i) {
            tone[i * 2] = tone[i * 2 + 1] =
                int16_t(std::sin(double(i) * 2 * std::numbers::pi * 440 / 48000) * 6000);
        }
        Bytes compressed(4096);
        auto size = opus_encode(enc, tone.data(), 960, compressed.data(), int(compressed.size()));
        opus_encoder_destroy(enc);
        require(size > 0, "Opus tone encoding failed");
        compressed.resize(size);
        auto first = Clock::now();
        recorder.set_deaf(true);
        recorder.set_playback(true);
        require(!recorder.playback_enabled(), "Playback bypassed deaf state for stream audio");
        recorder.set_playback(false);
        require(!recorder.playback_enabled(), "Deaf state left local playback enabled");
        // A watched-stream voice transport may still deliver audio after the
        // main Gateway is deafened. It must never reach the local mix/WAV.
        for (int i = 0; i < 5; ++i)
            recorder.frame({"stream-remote", "1", "opus", 8, uint32_t(i * 960), compressed, first});
        recorder.set_deaf(false);
        for (int i = 0; i < 50; ++i) {
            ReceivedFrame f{"voice",
                            "1",
                            "opus",
                            7,
                            uint32_t(0xFFFFF000U + i * 960),
                            compressed,
                            first + std::chrono::milliseconds{i * 20}};
            recorder.frame(std::move(f));
        }
        recorder.finish();
        recorder.flush();
        auto data = read_bytes(dir / "conversation.wav");
        require(data.size() == 44 + 20 * 48000 * 4, "Conversation duration is not exactly twenty seconds");
        int peak = 0;
        for (size_t i = 44; i + 1 < data.size(); i += 2) {
            int16_t sample = int16_t(uint16_t(data[i]) | uint16_t(data[i + 1]) << 8);
            peak = std::max(peak, std::abs(int(sample)));
        }
        require(peak > 1000, "Real decoded Opus did not reach WAV");
        require(events.back().at("manifest").at("audio_packets") == 50, "Packet manifest mismatch");
        NativeProcess generated(
            {o.ffmpeg,   "-nostdin", "-hide_banner", "-loglevel", "error",
             "-y",       "-f",       "lavfi",        "-i",        "testsrc2=size=160x90:rate=30",
             "-t",       "1",        "-an",          "-c:v",      "libx264",
             "-threads", "1",        "-pix_fmt",     "yuv420p",   path_text(root / "source with spaces.mp4")},
            root / "generate.log");
        generated.wait(cancel, std::chrono::seconds{20});
        o.video_source = path_text(root / "source with spaces.mp4");
        o.cache_directory = root / "cache";
        o.width = 160;
        o.height = 90;
        auto sources = Sources::prepare(o, cancel, [](const Json &) {});
        require(sources->video.size() == 30, "Cached H264 frame count mismatch");
        {
            Recorder churn(o, [](const Json &) {});
            for (int i = 0; i < 300; ++i)
                churn.frame({"camera", std::to_string(10000 - i), "H264", uint32_t(10000 - i), 0,
                             sources->video[0], Clock::now()});
            for (int i = 0; i < 80; ++i)
                churn.frame({"voice", std::to_string(10000 - i), "opus", uint32_t(10000 - i), 0, compressed,
                             Clock::now()});
        }
        {
            auto capture_options = o;
            capture_options.video_format = "lavfi";
            capture_options.video_source = "testsrc2=size=160x90:rate=30";
            capture_options.audio_format = "lavfi";
            capture_options.audio_source = "sine=frequency=440:sample_rate=48000";
            auto capture = Sources::prepare(capture_options, cancel, [](const Json &) {});
            const auto deadline = Clock::now() + std::chrono::seconds{5};
            int video_frames = 0, audio_frames = 0;
            size_t unused = 0;
            while (Clock::now() < deadline && (video_frames < 5 || audio_frames < 5)) {
                if (!capture->take_video(unused, true).empty())
                    ++video_frames;
                if (!capture->take_audio(unused, true).empty())
                    ++audio_frames;
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
            require(video_frames >= 5 && audio_frames >= 5,
                    "Native capture pipes failed to deliver video/audio");
        }
        Recorder video_recorder(o, [&](const Json &e) { events.push_back(e); });
        auto video_dir = root / ("video-" + std::to_string(random32()));
        video_recorder.start(video_dir, 0, 10);
        uint32_t timestamp = 0xFFFF0000;
        for (int i = 0; i < 300; ++i) {
            video_recorder.frame({"camera", "9", "H264", 55, timestamp, sources->video[size_t(i) % 30],
                                  first + std::chrono::milliseconds{i * 1000 / 30}});
            timestamp += 3000;
        }
        video_recorder.finish();
        video_recorder.flush();
        auto manifest = Json::parse(std::ifstream(video_dir / "manifest.json"));
        require(manifest["errors"].empty(), "Recorded video mux failed");
        require(manifest["videos"].size() == 1, "Missing recorded camera");
        require(manifest["videos"][0]["frames"] == 300, "Ten-second camera lost frames");
        NativeProcess decode({o.ffmpeg, "-nostdin", "-v", "error", "-threads", "1", "-i",
                              path_text(video_dir / "camera-9-55.mp4"), "-f", "null", "-"},
                             root / "decode.log");
        decode.wait(cancel, std::chrono::seconds{20});
        std::cout << "Native C++ media: authenticated RTP, Annex B, Opus timeline/wrap, source cache and 10s "
                     "camera decoding passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
