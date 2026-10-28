// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <discord/errors.hpp>
#include <discord/media.hpp>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <thread>
namespace discord::detail::media {
using Bytes = std::vector<std::uint8_t>;
using View = std::span<const std::uint8_t>;
using Clock = std::chrono::steady_clock;
class MediaPollTimer {
#ifdef _WIN32
    HANDLE timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
#endif
  public:
    ~MediaPollTimer() {
#ifdef _WIN32
        if (timer_)
            CloseHandle(timer_);
#endif
    }
    void wait(std::chrono::nanoseconds duration) {
        if (duration.count() <= 0)
            return;
#ifdef _WIN32
        if (timer_) {
            LARGE_INTEGER due;
            due.QuadPart = -std::max<int64_t>(1, duration.count() / 100);
            if (SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
                WaitForSingleObject(timer_, INFINITE);
                return;
            }
        }
#endif
        std::this_thread::sleep_for(duration);
    }
};
inline std::string path_text(const std::filesystem::path &p) {
    auto s = p.u8string();
    return {s.begin(), s.end()};
}
inline std::filesystem::path path_from(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
inline uint16_t u16(View s, size_t p = 0) { return uint16_t(s[p]) << 8 | s[p + 1]; }
inline uint32_t u32(View s, size_t p = 0) { return uint32_t(u16(s, p)) << 16 | u16(s, p + 2); }
inline void put16(Bytes &s, uint16_t v) {
    s.push_back(uint8_t(v >> 8));
    s.push_back(uint8_t(v));
}
inline void put32(Bytes &s, uint32_t v) {
    put16(s, uint16_t(v >> 16));
    put16(s, uint16_t(v));
}
inline void append(Bytes &out, View v) { out.insert(out.end(), v.begin(), v.end()); }
inline std::string id_text(const Json &v) {
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_unsigned())
        return std::to_string(v.get<uint64_t>());
    return {};
}
inline std::string field_id(const Json &v, const char *key) {
    auto p = v.find(key);
    return p == v.end() ? std::string{} : id_text(*p);
}
inline uint32_t random32() {
    static std::atomic_uint32_t seq{uint32_t(Clock::now().time_since_epoch().count())};
    return seq.fetch_add(2654435761U);
}

// Spawn only the named native executable, with CRT argument escaping and no shell.
// A Windows job owns its descendants even if the terminal is forcibly closed.
class NativeProcess {
#ifdef _WIN32
    HANDLE process_ = nullptr, job_ = nullptr, read_ = nullptr;
    static std::wstring wide(const std::string &s) {
        auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
        if (!n)
            throw Error("Invalid UTF-8 process argument");
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), w.data(), n);
        return w;
    }
    static std::wstring quote(const std::wstring &s) {
        std::wstring o = L"\"";
        size_t slash = 0;
        for (auto c : s) {
            if (c == L'\\') {
                ++slash;
                continue;
            }
            o.append(slash * (c == L'"' ? 2 : 1), L'\\');
            if (c == L'"')
                o += L'\\';
            o += c;
            slash = 0;
        }
        o.append(slash * 2, L'\\');
        return o + L'"';
    }
#else
    pid_t pid_ = -1;
    int read_ = -1;
#endif
    std::optional<int> exit_code_;

  public:
    NativeProcess(const std::vector<std::string> &args, const std::filesystem::path &log,
                  bool pipe_output = false) {
        if (args.empty())
            throw Error("Missing native executable");
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                   OPEN_EXISTING, 0, nullptr);
        if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE)
            throw Error("Cannot create native process log");
        HANDLE child_output = output;
        if (pipe_output) {
            if (!CreatePipe(&read_, &child_output, &sa, 65536) ||
                !SetHandleInformation(read_, HANDLE_FLAG_INHERIT, 0))
                throw Error("Cannot create capture pipe");
        }
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job_ ||
            !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(input);
            CloseHandle(output);
            throw Error("Cannot supervise native process");
        }
        SIZE_T n = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &n);
        Bytes storage(n);
        auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &n))
            throw Error("Cannot initialize process handles");
        HANDLE handles[]{input, output, child_output};
        const SIZE_T handle_count = pipe_output ? 3 : 2;
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                       sizeof(HANDLE) * handle_count, nullptr, nullptr))
            throw Error("Cannot restrict native process handles");
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = input;
        si.StartupInfo.hStdOutput = child_output;
        si.StartupInfo.hStdError = output;
        si.lpAttributeList = list;
        std::wstring command;
        for (const auto &a : args) {
            if (!command.empty())
                command += L' ';
            command += quote(wide(a));
        }
        PROCESS_INFORMATION pi{};
        auto ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                 nullptr, &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(list);
        CloseHandle(output);
        if (pipe_output)
            CloseHandle(child_output);
        CloseHandle(input);
        if (!ok) {
            CloseHandle(job_);
            job_ = nullptr;
            throw Error("Cannot start native executable: " + args.front());
        }
        process_ = pi.hProcess;
        if (!AssignProcessToJobObject(job_, process_)) {
            TerminateProcess(process_, 1);
            CloseHandle(pi.hThread);
            throw Error("Cannot assign native process job");
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
#else
        int capture[2]{-1, -1};
        if (pipe_output && pipe(capture))
            throw Error("Cannot create capture pipe");
        pid_ = fork();
        if (pid_ < 0)
            throw Error("Cannot fork native executable");
        if (!pid_) {
            setpgid(0, 0);
            int f = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            int in = open("/dev/null", O_RDONLY);
            dup2(in, 0);
            dup2(pipe_output ? capture[1] : f, 1);
            dup2(f, 2);
            close(f);
            close(in);
            if (pipe_output) {
                close(capture[0]);
                close(capture[1]);
            }
            std::vector<char *> argv;
            for (const auto &a : args)
                argv.push_back(const_cast<char *>(a.c_str()));
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        setpgid(pid_, pid_);
        if (pipe_output) {
            close(capture[1]);
            read_ = capture[0];
            fcntl(read_, F_SETFL, O_NONBLOCK);
        }
#endif
    }
    NativeProcess(const NativeProcess &) = delete;
    ~NativeProcess() {
#ifdef _WIN32
        if (job_)
            CloseHandle(job_);
        if (process_) {
            WaitForSingleObject(process_, 2000);
            CloseHandle(process_);
        }
        if (read_)
            CloseHandle(read_);
#else
        if (pid_ > 0) {
            kill(-pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
        if (read_ >= 0)
            close(read_);
#endif
    }
    std::optional<int> status() {
        if (exit_code_)
            return exit_code_;
#ifdef _WIN32
        if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0)
            return {};
        DWORD c;
        GetExitCodeProcess(process_, &c);
        exit_code_ = int(c);
        return exit_code_;
#else
        int s;
        auto p = waitpid(pid_, &s, WNOHANG);
        if (!p)
            return {};
        pid_ = -1;
        exit_code_ = WIFEXITED(s) ? WEXITSTATUS(s) : 1;
        return exit_code_;
#endif
    }
    Bytes read() {
        std::array<uint8_t, 65536> buffer;
#ifdef _WIN32
        DWORD available = 0, n = 0;
        if (!read_ || !PeekNamedPipe(read_, nullptr, 0, nullptr, &available, nullptr) || !available)
            return {};
        if (!ReadFile(read_, buffer.data(), std::min<DWORD>(available, DWORD(buffer.size())), &n, nullptr))
            return {};
        return Bytes(buffer.begin(), buffer.begin() + n);
#else
        if (read_ < 0)
            return {};
        auto n = ::read(read_, buffer.data(), buffer.size());
        if (n <= 0)
            return {};
        return Bytes(buffer.begin(), buffer.begin() + n);
#endif
    }
    void wait(std::atomic_bool &stop, std::chrono::seconds timeout = std::chrono::seconds{300}) {
        auto end = Clock::now() + timeout;
        while (!stop && Clock::now() < end) {
            if (auto code = status()) {
                if (*code)
                    throw Error("Native encoder failed; inspect its local log");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        throw Error(stop ? "Media preparation cancelled" : "Native encoder timed out");
    }
};

inline Bytes read_bytes(const std::filesystem::path &p, size_t max = 256 * 1024 * 1024) {
    if (std::filesystem::file_size(p) > max)
        throw Error("Encoded source exceeds memory limit");
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw Error("Cannot read encoded media");
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
inline std::vector<Bytes> nals(View s) {
    std::vector<Bytes> result;
    size_t start = 0;
    bool found = false;
    for (size_t i = 0; i + 3 <= s.size();) {
        size_t prefix = 0;
        if (i + 4 <= s.size() && s[i] == 0 && s[i + 1] == 0 && s[i + 2] == 0 && s[i + 3] == 1)
            prefix = 4;
        else if (s[i] == 0 && s[i + 1] == 0 && s[i + 2] == 1)
            prefix = 3;
        if (prefix) {
            if (found && i > start)
                result.emplace_back(s.begin() + start, s.begin() + i);
            start = i + prefix;
            found = true;
            i += prefix;
        } else
            ++i;
    }
    if (found && start < s.size())
        result.emplace_back(s.begin() + start, s.end());
    return result;
}
inline void nal_append(Bytes &f, View n) {
    f.insert(f.end(), {0, 0, 0, 1});
    append(f, n);
}
class Capture {
    std::unique_ptr<NativeProcess> process_;
    bool video_;
    std::mutex mutex_;
    std::deque<Bytes> packets_;
    std::exception_ptr failure_;
    std::jthread reader_;
    void enqueue(Bytes packet) {
        std::lock_guard lock(mutex_);
        if (packets_.size() >= size_t(video_ ? 8 : 25))
            packets_.pop_front();
        packets_.push_back(std::move(packet));
    }

  public:
    Capture(const MediaOptions &o, bool video, const std::filesystem::path &cache) : video_(video) {
        std::vector<std::string> args{o.ffmpeg,   "-nostdin", "-hide_banner", "-loglevel", "error",
                                      "-threads", "2"};
        const auto &format = video ? o.video_format : o.audio_format;
        if (format == "lavfi")
            args.push_back("-re");
        args.insert(args.end(), {"-f", format, "-i", video ? o.video_source : o.audio_source});
        std::vector<std::string> tail;
        if (video)
            tail = {"-an",
                    "-vf",
                    "scale=" + std::to_string(o.width) + ":" + std::to_string(o.height) +
                        ":force_original_aspect_ratio=decrease,pad=" + std::to_string(o.width) + ":" +
                        std::to_string(o.height) + ":(ow-iw)/2:(oh-ih)/2,fps=" + std::to_string(o.fps),
                    "-c:v",
                    "libx264",
                    "-threads",
                    "2",
                    "-preset",
                    "veryfast",
                    "-tune",
                    "zerolatency",
                    "-profile:v",
                    "baseline",
                    "-pix_fmt",
                    "yuv420p",
                    "-b:v",
                    std::to_string(o.bitrate_kbps) + "k",
                    "-g",
                    std::to_string(o.fps),
                    "-bf",
                    "0",
                    "-x264-params",
                    "repeat-headers=1:aud=1:scenecut=0",
                    "-flush_packets",
                    "1",
                    "-f",
                    "h264",
                    "pipe:1"};
        else
            tail = {"-vn",     "-c:a",
                    "libopus", "-threads",
                    "2",       "-ar",
                    "48000",   "-ac",
                    "2",       "-frame_duration",
                    "20",      "-b:a",
                    "96k",     "-flush_packets",
                    "1",       "-page_duration",
                    "20000",   "-f",
                    "opus",    "pipe:1"};
        args.insert(args.end(), tail.begin(), tail.end());
        process_ = std::make_unique<NativeProcess>(
            args,
            cache / ((video ? "video-capture-" : "audio-capture-") + std::to_string(random32()) + ".log"),
            true);
        reader_ = std::jthread([this](std::stop_token stop) {
            try {
                Bytes pending, packet;
                while (!stop.stop_requested()) {
                    auto bytes = process_->read();
                    append(pending, bytes);
                    if (pending.size() > 8 * 1024 * 1024)
                        throw Error("Capture framing buffer limit exceeded");
                    if (video_) {
                        size_t second = 0, seen = 0;
                        for (size_t i = 0; i + 5 < pending.size(); ++i) {
                            size_t prefix = 0;
                            if (pending[i] == 0 && pending[i + 1] == 0 && pending[i + 2] == 0 &&
                                pending[i + 3] == 1)
                                prefix = 4;
                            else if (pending[i] == 0 && pending[i + 1] == 0 && pending[i + 2] == 1)
                                prefix = 3;
                            if (prefix && i + prefix < pending.size() && (pending[i + prefix] & 31) == 9) {
                                if (++seen == 2) {
                                    second = i;
                                    break;
                                }
                                i += prefix;
                            }
                        }
                        if (second) {
                            enqueue(Bytes(pending.begin(), pending.begin() + second));
                            pending.erase(pending.begin(), pending.begin() + second);
                            continue;
                        }
                    } else {
                        while (pending.size() >= 27) {
                            if (std::memcmp(pending.data(), "OggS", 4))
                                throw Error("Invalid live Opus framing");
                            size_t segments = pending[26];
                            if (pending.size() < 27 + segments)
                                break;
                            size_t total = 27 + segments;
                            for (size_t i = 0; i < segments; ++i)
                                total += pending[27 + i];
                            if (pending.size() < total)
                                break;
                            size_t body = 27 + segments;
                            for (size_t i = 0; i < segments; ++i) {
                                auto n = pending[27 + i];
                                append(packet, View(pending).subspan(body, n));
                                body += n;
                                if (n < 255) {
                                    if (!packet.empty() &&
                                        (packet.size() < 8 || (std::memcmp(packet.data(), "OpusHead", 8) &&
                                                               std::memcmp(packet.data(), "OpusTags", 8))))
                                        enqueue(std::move(packet));
                                    packet = {};
                                }
                            }
                            pending.erase(pending.begin(), pending.begin() + total);
                        }
                    }
                    if (bytes.empty()) {
                        if (auto code = process_->status())
                            throw Error("Capture encoder exited (code " + std::to_string(*code) + ")");
                        std::this_thread::sleep_for(std::chrono::milliseconds{2});
                    }
                }
            } catch (...) {
                std::lock_guard lock(mutex_);
                failure_ = std::current_exception();
            }
        });
    }
    Bytes take() {
        std::lock_guard lock(mutex_);
        if (failure_)
            std::rethrow_exception(failure_);
        if (packets_.empty())
            return {};
        auto p = std::move(packets_.front());
        packets_.pop_front();
        return p;
    }
};
struct Sources {
    std::vector<Bytes> video, audio;
    std::unique_ptr<Capture> live_video, live_audio;
    bool has_video() const { return live_video || !video.empty(); }
    bool has_audio() const { return live_audio || !audio.empty(); }
    Bytes take_video(size_t &index, bool loop) {
        if (live_video)
            return live_video->take();
        if (video.empty())
            return {};
        auto packet = video.at(index);
        if (++index == video.size()) {
            index = 0;
            if (!loop)
                video.clear();
        }
        return packet;
    }
    Bytes take_audio(size_t &index, bool loop) {
        if (live_audio)
            return live_audio->take();
        if (audio.empty())
            return {};
        auto packet = audio.at(index);
        if (++index == audio.size()) {
            index = 0;
            if (!loop)
                audio.clear();
        }
        return packet;
    }
    static std::shared_ptr<Sources> prepare(const MediaOptions &o, std::atomic_bool &stop,
                                            const MediaSession::Handler &emit) {
        auto result = std::make_shared<Sources>();
        auto cache = o.cache_directory.empty()
                         ? std::filesystem::temp_directory_path() / "discord-cpp-media-cache"
                         : o.cache_directory;
        std::filesystem::create_directories(cache);
        auto make = [&](const std::string &source, bool video) {
            auto path = path_from(source);
            if (!std::filesystem::is_regular_file(path))
                throw Error("Media source file not found");
            const auto stamp = std::filesystem::last_write_time(path).time_since_epoch().count();
            const auto key =
                std::hash<std::string>{}(path_text(std::filesystem::absolute(path)) + std::to_string(stamp) +
                                         std::to_string(std::filesystem::file_size(path)) +
                                         std::to_string(o.width) + "x" + std::to_string(o.height) + "/" +
                                         std::to_string(o.fps) + "/" + std::to_string(o.bitrate_kbps) + "v2");
            auto out = cache / (std::to_string(key) + (video ? ".h264" : ".opus"));
            if (!std::filesystem::exists(out)) {
                auto temp = out;
                temp += ".preparing";
                auto log = out;
                log += ".log";
                emit({{"event", "preparing_source"}, {"media", video ? "video" : "audio"}, {"path", source}});
                std::vector<std::string> args{o.ffmpeg, "-nostdin", "-hide_banner", "-loglevel",
                                              "error",  "-y",       "-threads",     "2",
                                              "-i",     source};
                std::vector<std::string> tail;
                if (video)
                    tail = {"-an",
                            "-vf",
                            "scale=" + std::to_string(o.width) + ":" + std::to_string(o.height) +
                                ":force_original_aspect_ratio=decrease,pad=" + std::to_string(o.width) + ":" +
                                std::to_string(o.height) +
                                ":(ow-iw)/2:(oh-ih)/2,fps=" + std::to_string(o.fps),
                            "-c:v",
                            "libx264",
                            "-threads",
                            "2",
                            "-preset",
                            "veryfast",
                            "-tune",
                            "zerolatency",
                            "-profile:v",
                            "baseline",
                            "-pix_fmt",
                            "yuv420p",
                            "-b:v",
                            std::to_string(o.bitrate_kbps) + "k",
                            "-maxrate",
                            std::to_string(o.bitrate_kbps) + "k",
                            "-bufsize",
                            std::to_string(o.bitrate_kbps * 2) + "k",
                            "-g",
                            std::to_string(o.fps),
                            "-bf",
                            "0",
                            "-x264-params",
                            "repeat-headers=1:aud=1:scenecut=0",
                            "-f",
                            "h264",
                            path_text(temp)};
                else
                    tail = {"-vn",   "-c:a", "libopus", "-threads",     "2",   "-ar",
                            "48000", "-ac",  "2",       "-b:a",         "96k", "-frame_duration",
                            "20",    "-f",   "opus",    path_text(temp)};
                args.insert(args.end(), tail.begin(), tail.end());
                NativeProcess process(args, log);
                try {
                    process.wait(stop);
                    std::filesystem::rename(temp, out);
                } catch (...) {
                    std::error_code ec;
                    std::filesystem::remove(temp, ec);
                    throw;
                }
            }
            return read_bytes(out);
        };
        if (!o.video_source.empty() && !o.video_format.empty())
            result->live_video = std::make_unique<Capture>(o, true, cache);
        else if (!o.video_source.empty()) {
            auto data = make(o.video_source, true);
            Bytes frame;
            for (const auto &n : nals(data)) {
                if ((n[0] & 31) == 9 && !frame.empty()) {
                    result->video.push_back(std::move(frame));
                    frame = {};
                }
                nal_append(frame, n);
            }
            if (!frame.empty())
                result->video.push_back(std::move(frame));
        }
        if (!o.audio_source.empty() && !o.audio_format.empty())
            result->live_audio = std::make_unique<Capture>(o, false, cache);
        else if (!o.audio_source.empty()) {
            auto data = make(o.audio_source, false);
            Bytes packet;
            size_t pos = 0;
            while (pos + 27 <= data.size()) {
                if (std::memcmp(data.data() + pos, "OggS", 4))
                    throw Error("Invalid cached Ogg header");
                auto count = data[pos + 26];
                if (pos + 27 + count > data.size())
                    throw Error("Truncated Ogg page");
                size_t body = pos + 27 + count;
                for (size_t i = 0; i < count; ++i) {
                    auto len = data[pos + 27 + i];
                    if (body + len > data.size())
                        throw Error("Truncated Ogg payload");
                    append(packet, View(data).subspan(body, len));
                    body += len;
                    if (len < 255) {
                        if (!packet.empty() &&
                            (packet.size() < 8 || (std::memcmp(packet.data(), "OpusHead", 8) &&
                                                   std::memcmp(packet.data(), "OpusTags", 8))))
                            result->audio.push_back(std::move(packet));
                        packet = {};
                    }
                }
                pos = body;
            }
        }
        return result;
    }
};
} // namespace discord::detail::media
