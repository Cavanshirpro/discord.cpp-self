// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include "native_voice.hpp"
#include <cmath>
#include <future>
#include <opus/opus.h>
#ifdef _WIN32
#include <mmsystem.h>
#endif
namespace discord::detail::media {
inline void little(std::ostream &f, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        f.put(char(value >> (i * 8)));
}
inline void wav(const std::filesystem::path &path, std::span<const int16_t> samples) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw Error("Cannot write conversation WAV");
    uint32_t n = uint32_t(samples.size() * 2);
    f.write("RIFF", 4);
    little(f, n + 36, 4);
    f.write("WAVEfmt ", 8);
    little(f, 16, 4);
    little(f, 1, 2);
    little(f, 2, 2);
    little(f, 48000, 4);
    little(f, 192000, 4);
    little(f, 4, 2);
    little(f, 16, 2);
    f.write("data", 4);
    little(f, n, 4);
    f.write(reinterpret_cast<const char *>(samples.data()), n);
    if (!f)
        throw Error("Cannot finish conversation WAV");
}
struct Decoder {
    OpusDecoder *value = nullptr;
    uint32_t first_ts = 0;
    Clock::time_point first_arrival{};
    bool initialized = false;
    Decoder() {
        int error = 0;
        value = opus_decoder_create(48000, 2, &error);
        if (error || !value)
            throw Error("Cannot create Opus decoder");
    }
    ~Decoder() { opus_decoder_destroy(value); }
    Decoder(const Decoder &) = delete;
    std::vector<int16_t> decode(View data) {
        std::vector<int16_t> out(5760 * 2);
        auto n = opus_decode(value, data.data(), int(data.size()), out.data(), 5760, 0);
        if (n < 0)
            return {};
        out.resize(size_t(n) * 2);
        return out;
    }
};
class Monitor {
#ifdef _WIN32
    HWAVEOUT device_ = nullptr;
    struct Block {
        WAVEHDR header{};
        std::array<int16_t, 1920> data{};
        bool prepared = false;
    };
    std::array<Block, 16> blocks_{};
#endif
    bool enabled_ = false;
    std::map<int64_t, std::vector<int32_t>> slots_;
    Clock::time_point start_ = Clock::now();
    int64_t played_ = 0;

  public:
    explicit Monitor(bool enabled) { set_enabled(enabled); }
    bool enabled() const { return enabled_; }
    void set_enabled(bool enabled) {
        if (enabled == enabled_)
            return;
        if (!enabled) {
            enabled_ = false;
            slots_.clear();
#ifdef _WIN32
            if (device_)
                waveOutReset(device_);
#endif
            return;
        }
#ifdef _WIN32
        if (!device_) {
            WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
            if (waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
                throw Error("Cannot open local audio output");
        }
#else
        throw Error("Local audio monitoring currently requires Windows waveOut");
#endif
        start_ = Clock::now();
        played_ = 0;
        slots_.clear();
        enabled_ = true;
    }
    ~Monitor() {
#ifdef _WIN32
        if (device_) {
            waveOutReset(device_);
            for (auto &b : blocks_)
                if (b.prepared)
                    waveOutUnprepareHeader(device_, &b.header, sizeof(b.header));
            waveOutClose(device_);
        }
#endif
    }
    void add(std::span<const int16_t> pcm, Clock::time_point arrival) {
        if (!enabled_)
            return;
        auto sample = int64_t(std::chrono::duration<double>(arrival - start_).count() * 48000) + 5760;
        for (size_t i = 0; i < pcm.size(); ++i) {
            auto pos = sample * 2 + int64_t(i), block = pos / 1920;
            if (block < played_ || block > played_ + 25)
                continue;
            auto &data = slots_[block];
            if (data.empty())
                data.resize(1920);
            data[size_t(pos % 1920)] += pcm[i];
        }
    }
    void tick() {
        if (!enabled_)
            return;
#ifdef _WIN32
        auto now = int64_t(std::chrono::duration<double>(Clock::now() - start_).count() * 50);
        if (played_ + 15 < now) {
            played_ = now;
            slots_.clear();
        }
        while (played_ <= now) {
            auto &b = blocks_[size_t(played_) % blocks_.size()];
            if (b.prepared && !(b.header.dwFlags & WHDR_DONE))
                break;
            if (b.prepared) {
                waveOutUnprepareHeader(device_, &b.header, sizeof(b.header));
                b.prepared = false;
            }
            auto slot = slots_.find(played_);
            for (size_t i = 0; i < b.data.size(); ++i)
                b.data[i] = slot == slots_.end() ? 0 : int16_t(std::clamp(slot->second[i], -32768, 32767));
            if (slot != slots_.end())
                slots_.erase(slot);
            b.header = {};
            b.header.lpData = reinterpret_cast<char *>(b.data.data());
            b.header.dwBufferLength = sizeof(b.data);
            if (waveOutPrepareHeader(device_, &b.header, sizeof(b.header)) == MMSYSERR_NOERROR) {
                b.prepared = true;
                waveOutWrite(device_, &b.header, sizeof(b.header));
            }
            ++played_;
        }
#endif
    }
};
struct VideoFile {
    std::string source, user, codec;
    uint32_t ssrc = 0, first_ts = 0, last_ts = 0;
    size_t frames = 0;
    Clock::time_point first_arrival{};
    std::filesystem::path raw, output;
    std::ofstream file;
    bool complete = false;
};
class Recorder {
    MediaOptions options_;
    MediaSession::Handler emit_;
    Monitor monitor_;
    std::map<std::string, std::unique_ptr<Decoder>> decoders_;
    std::map<std::string, std::map<int, Bytes>> headers_;
    std::map<std::string, VideoFile> videos_;
    std::vector<int32_t> mix_;
    std::filesystem::path directory_;
    Clock::time_point started_{};
    int audio_seconds_ = 0, video_seconds_ = 0;
    size_t audio_packets_ = 0;
    bool active_ = false;
    bool deaf_ = false;
    std::vector<std::future<Json>> finalizers_;
    bool keyframe(const ReceivedFrame &f, Bytes &configured) {
        auto key = f.source + ":" + std::to_string(f.ssrc);
        if (!headers_.contains(key) && headers_.size() >= 256)
            headers_.erase(headers_.begin());
        auto &params = headers_[key];
        bool idr = false;
        std::set<int> present;
        for (const auto &nal : nals(f.data)) {
            int type = f.codec == "H265" ? (nal[0] >> 1) & 63 : nal[0] & 31;
            present.insert(type);
            if ((f.codec == "H264" && (type == 7 || type == 8)) ||
                (f.codec == "H265" && (type == 32 || type == 33 || type == 34))) {
                if (nal.size() <= 65536)
                    params[type] = nal;
            }
            if ((f.codec == "H264" && type == 5) ||
                (f.codec == "H265" && (type == 19 || type == 20 || type == 21)))
                idr = true;
        }
        if (!idr)
            return false;
        for (int type : f.codec == "H265" ? std::vector<int>{32, 33, 34} : std::vector<int>{7, 8}) {
            auto p = params.find(type);
            if (p == params.end())
                return false;
            if (!present.contains(type))
                nal_append(configured, p->second);
        }
        append(configured, f.data);
        return true;
    }

  public:
    Recorder(const MediaOptions &options, MediaSession::Handler emit)
        : options_(options), emit_(std::move(emit)), monitor_(options.play_received_audio) {}
    ~Recorder() {
        try {
            finish(true);
            for (auto &f : finalizers_)
                if (f.valid())
                    f.wait();
        } catch (...) {
        }
    }
    bool active() const { return active_; }
    bool playback_enabled() const { return monitor_.enabled(); }
    void set_deaf(bool value) {
        if (deaf_ == value)
            return;
        deaf_ = value;
        monitor_.set_enabled(options_.play_received_audio && !deaf_);
    }
    void set_playback(bool value) {
        monitor_.set_enabled(value && !deaf_);
        options_.play_received_audio = value;
    }
    void start(const std::filesystem::path &directory, int audio, int video) {
        if (active_)
            throw Error("A recording is already active");
        if (std::filesystem::exists(directory) && !std::filesystem::is_empty(directory))
            throw Error("Recording directory must be empty");
        std::filesystem::create_directories(directory);
        directory_ = directory;
        audio_seconds_ = audio;
        video_seconds_ = video;
        started_ = Clock::now();
        audio_packets_ = 0;
        mix_.assign(size_t(audio) * 96000, 0);
        videos_.clear();
        active_ = true;
        for (auto &[_, d] : decoders_)
            d->initialized = false;
        emit_({{"event", "recording_started"},
               {"directory", path_text(directory_)},
               {"audio_seconds", audio},
               {"video_seconds", video}});
    }
    void frame(ReceivedFrame f) {
        if (f.codec == "opus") {
            if (deaf_)
                return;
            auto key = f.source + ":" + std::to_string(f.ssrc);
            if (!decoders_.contains(key) && decoders_.size() >= 64)
                decoders_.erase(decoders_.begin());
            auto &decoder = decoders_[key];
            if (!decoder)
                decoder = std::make_unique<Decoder>();
            auto pcm = decoder->decode(f.data);
            monitor_.add(pcm, f.arrival);
            if (active_ && audio_seconds_) {
                if (!decoder->initialized) {
                    decoder->first_ts = f.timestamp;
                    decoder->first_arrival = f.arrival;
                    decoder->initialized = true;
                }
                auto offset =
                    int64_t(std::chrono::duration<double>(decoder->first_arrival - started_).count() *
                            48000) +
                    int32_t(f.timestamp - decoder->first_ts);
                for (size_t i = 0; i < pcm.size(); ++i) {
                    auto index = offset * 2 + int64_t(i);
                    if (index >= 0 && size_t(index) < mix_.size())
                        mix_[size_t(index)] += pcm[i];
                }
                ++audio_packets_;
            }
        } else {
            Bytes configured;
            bool idr = keyframe(f, configured);
            if (active_ && video_seconds_) {
                auto key = f.source + ":" + std::to_string(f.ssrc);
                auto it = videos_.find(key);
                if (it == videos_.end() && idr && videos_.size() < 32) {
                    VideoFile v;
                    v.source = f.source;
                    v.user = f.user;
                    v.ssrc = f.ssrc;
                    v.codec = f.codec;
                    v.first_ts = f.timestamp;
                    v.first_arrival = f.arrival;
                    auto name = f.source + "-" + f.user + "-" + std::to_string(f.ssrc);
                    for (auto &c : name)
                        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-')
                            c = '_';
                    v.output = directory_ / (name + ".mp4");
                    v.raw = v.output;
                    v.raw += f.codec == "H265" ? ".encoded.h265" : ".encoded.h264";
                    v.file.open(v.raw, std::ios::binary);
                    if (!v.file)
                        throw Error("Cannot create received video recording");
                    it = videos_.emplace(key, std::move(v)).first;
                }
                if (it != videos_.end()) {
                    auto &v = it->second;
                    double elapsed = uint32_t(f.timestamp - v.first_ts) / 90000.0;
                    if (!v.complete && elapsed <= video_seconds_) {
                        const auto &data = (v.frames == 0 ? configured : f.data);
                        v.file.write(reinterpret_cast<const char *>(data.data()),
                                     std::streamsize(data.size()));
                        v.last_ts = f.timestamp;
                        ++v.frames;
                    } else if (elapsed > video_seconds_)
                        v.complete = true;
                }
            }
        }
        if (options_.emit_media_packets) {
            Json j{{"event", "media_packet"},
                   {"source", f.source},
                   {"user_id", f.user},
                   {"codec", f.codec},
                   {"ssrc", f.ssrc},
                   {"timestamp", f.timestamp},
                   {"payload_bytes", f.data.size()}};
            if (f.data.size() <= 256 * 1024) {
                std::string text(4 * ((f.data.size() + 2) / 3), '\0');
                EVP_EncodeBlock(reinterpret_cast<uint8_t *>(text.data()), f.data.data(), int(f.data.size()));
                j["payload_base64"] = std::move(text);
            } else
                j["payload_omitted"] = true;
            emit_(j);
        }
    }
    void finish(bool early = false) {
        if (!active_)
            return;
        active_ = false;
        auto elapsed = std::chrono::duration<double>(Clock::now() - started_).count();
        int audio = audio_seconds_;
        if (early)
            mix_.resize(std::min(mix_.size(), size_t(std::max(0.0, elapsed) * 96000)));
        std::vector<int16_t> samples(mix_.size());
        long double energy = 0;
        int peak = 0;
        for (size_t i = 0; i < mix_.size(); ++i) {
            samples[i] = int16_t(std::clamp(mix_[i], -32768, 32767));
            peak = std::max(peak, std::abs(int(samples[i])));
            energy += double(samples[i]) * samples[i];
        }
        if (audio)
            wav(directory_ / "conversation.wav", samples);
        Json manifest{{"backend", "cpp"},
                      {"audio_seconds", samples.size() / 96000.0},
                      {"audio_packets", audio_packets_},
                      {"audio_rms", samples.empty() ? 0.0 : std::sqrt(double(energy / samples.size()))},
                      {"audio_peak", peak},
                      {"early_stop", early},
                      {"videos", Json::array()},
                      {"errors", Json::array()}};
        struct Mux {
            std::filesystem::path raw, output;
            double fps;
        };
        std::vector<Mux> mux;
        for (auto &[_, v] : videos_) {
            v.file.close();
            double span = uint32_t(v.last_ts - v.first_ts) / 90000.0;
            double fps = v.frames > 1 && span > 0 ? (v.frames - 1) / span : options_.fps;
            fps = std::clamp(fps, 1.0, 120.0);
            manifest["videos"].push_back({{"source", v.source},
                                          {"user_id", v.user},
                                          {"ssrc", v.ssrc},
                                          {"codec", v.codec},
                                          {"frames", v.frames},
                                          {"duration_seconds", v.frames / fps},
                                          {"path", path_text(v.output.filename())}});
            mux.push_back({v.raw, v.output, fps});
        }
        auto dir = directory_;
        auto ffmpeg = options_.ffmpeg;
        mix_.clear();
        videos_.clear();
        finalizers_.push_back(std::async(std::launch::async, [manifest = std::move(manifest),
                                                              mux = std::move(mux), dir, ffmpeg]() mutable {
            for (auto &m : mux)
                try {
                    auto log = m.output;
                    log += ".mux.log";
                    std::atomic_bool cancel = false;
                    NativeProcess process({ffmpeg,         "-nostdin",
                                           "-hide_banner", "-loglevel",
                                           "error",        "-y",
                                           "-threads",     "2",
                                           "-fflags",      "+genpts+discardcorrupt",
                                           "-r",           std::to_string(m.fps),
                                           "-i",           path_text(m.raw),
                                           "-an",          "-c:v",
                                           "copy",         "-movflags",
                                           "+faststart",   path_text(m.output)},
                                          log);
                    process.wait(cancel, std::chrono::seconds{30});
                } catch (const std::exception &e) {
                    manifest["errors"].push_back(e.what());
                }
            std::ofstream f(dir / "manifest.json");
            f << manifest.dump(2);
            return Json{
                {"event", "recording_finished"}, {"directory", path_text(dir)}, {"manifest", manifest}};
        }));
        emit_({{"event", "recording_finalizing"}, {"directory", path_text(dir)}});
    }
    void tick() {
        monitor_.tick();
        if (active_ &&
            Clock::now() - started_ >= std::chrono::seconds{std::max(audio_seconds_, video_seconds_) + 1})
            finish();
        for (auto it = finalizers_.begin(); it != finalizers_.end();)
            if (it->wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
                emit_(it->get());
                it = finalizers_.erase(it);
            } else
                ++it;
    }
    void flush() {
        finish(true);
        while (!finalizers_.empty()) {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }
};
} // namespace discord::detail::media
