# C++ camera, audio and Go Live

The media backend and console example are C++20. They do not launch Node, Python or
Rust. The normal REST/Gateway client remains independent of media dependencies.
Enable `DISCORD_SELF_NATIVE_MEDIA` to link Discord's official C++ libdave,
OpenSSL 3 and libopus. A native FFmpeg executable encodes inputs and muxes recordings.
Python is used only by optional development tests and release tooling.

## Build

On MSYS2 MINGW64 install the normal project dependencies plus
`mingw-w64-x86_64-openssl` and `mingw-w64-x86_64-opus`. Put FFmpeg with libx264
and libopus on PATH, then run in PowerShell:

```powershell
.\scripts\setup-cpp-media.ps1
```

This builds pinned C++ libdave/MLS++ sources under ignored `build/`, then builds
`discord_self_media`. There is no npm/pip/cargo step.
The script uses one compiler job by default to limit peak memory.

The corresponding CMake options are:

```text
-DDISCORD_SELF_NATIVE_MEDIA=ON
-DDISCORD_SELF_BUILD_LIVE_EXAMPLES=ON
-DDISCORD_SELF_BUILD_TESTS=OFF
-DCMAKE_PREFIX_PATH=/path/to/native-prefix
```

On Linux install libopus/OpenSSL development packages, FFmpeg, and the normal
C++ dependencies. The setup PowerShell script also runs under `pwsh` with
`-ToolchainBin ''`; alternatively build its pinned MLS++ and libdave revisions
with CMake and point the project at their install prefix. Playback monitoring
currently uses Windows waveOut; leave it disabled on other systems.

## Console example and application controls

```powershell
.\scripts\start-media.ps1 -GuildId 'GUILD_ID' -ChannelId 'VOICE_CHANNEL_ID' `
  -Video 'C:\media\video.mp4' -Audio 'C:\media\audio.mp3' `
  -TokenFile 'C:\private\token.txt' -WatchStreams -PlayAudio
```

This opens a visible JSON event console. Ctrl+C or closing the terminal stops the
session and supervised encoder children. `-InCurrentTerminal` uses the existing
terminal. The token file is read by C++, never passed as token text to child
arguments, environment variables or temporary configuration.

Use `-Mode Receive`, `Microphone`, `Camera` or `GoLive` to select a destination.
`-DurationSeconds`, `-RecordDirectory`, `-AudioSeconds`, `-VideoSeconds` and
`-WaitForMedia` configure a finite test. Nothing records by default. `-Headless`
is retained as a compatibility flag; this public launcher always uses the JSON
console. Interactive personal test applications are external to this repository.
The library exposes source, channel, watching, listening and recording controls
for developers to connect to their own UI, as shown below.

Paths with spaces and Unicode are accepted. Playback continues while a replacement
file is prepared; an invalid replacement reports an error and keeps the old
source. Video/audio loop independently. File inputs are encoded into a keyed
cache once; looping does not repeatedly decode a high-resolution original.

## C++ API

```cpp
#include <discord/discord.hpp>

discord::MediaOptions options;
options.guild_id = discord::Snowflake("GUILD_ID");
options.channel_id = discord::Snowflake("VOICE_CHANNEL_ID");
options.video_source = "video.mp4";
options.audio_source = "audio.mp3";
options.watch_streams = true;
options.play_received_audio = true; // Windows only

discord::MediaSession media(options);
media.run(discord::token_from_env(), [&](const discord::Json& event) {
    // Serial callbacks on the run() caller; keep this work short.
    // snapshot, playing, sources_changed, recording_started/finished,
    // participant_changed, stream_watched, command_error, transport_error...
});
```

Run this blocking call on an application-owned thread when using the controls
from another thread. Keep the session alive, call `stop()`, then join that thread.
`MediaSession` is single-use; changing sources/channels does not create a new one.
`MediaSession::available()` checks whether the linked library includes media.

```cpp
auto state = media.snapshot();
media.set_sources("other-video.mp4", "other-audio.mp3");
media.join(discord::Snowflake("OTHER_GUILD_ID"), discord::Snowflake("OTHER_CHANNEL_ID"));
media.record("recordings/take-1", 20, 10); // directory must be empty
media.stop_recording();
media.set_deaf(false);
media.watch_stream(state["streams"][0]["key"].get<std::string>());
media.unwatch_stream(state["streams"][0]["key"].get<std::string>());
```

`snapshot()` includes current participants, camera/stream/mute/deaf states,
visible cached names/roles/avatar/public flags, stream owners and actual Gateway
viewer IDs/counts, receive counters, DAVE readiness, UDP/RTX errors, sender pacing
and received RTCP reports. Unknown viewer metadata is null, not an invented count.
Keys/IDs are strings to preserve all 64 bits. Tokens, session IDs and encryption
keys are excluded. Metadata is limited to what Discord delivers to this account.

Set `emit_media_packets=true` for optional encoded Opus/H264/H265 frame callbacks.
They include source, user ID, SSRC, RTP timestamp and base64 payload up to 256 KiB.
Larger frames report `payload_omitted=true`; frames still reach recording.
Keep callbacks short to avoid delaying media. There is no IPC payload copy by default.

## Physical capture

Capture is explicit; no device opens merely by joining a channel. Set an FFmpeg
input format and device source in `MediaOptions` or a JSON configuration:

```cpp
options.audio_format = "dshow";
options.audio_source = "audio=Microphone device name";
options.video_format = "dshow";
options.video_source = "video=Camera device name";
```

FFmpeg device names depend on the machine. The example launcher accepts file
inputs; device inputs require an explicit configuration or C++ options.
Live capture uses bounded native pipes and queues; it does not write an unbounded
capture spool. Physical devices were not part of the supplied file-based live test.

## Transport and recording

The C++ backend negotiates AES-256-GCM RTP transport and DAVE. Media waits for a
ratchet; no clear-media fallback is used. H264 is sent; H264/H265 and Opus are
received. A bounded sequence-repair window handles wrap, reordering and RTX.
Video requests, PLI/NACK, retransmission caching and RTCP reports are handled in C++.
Receive subscriptions explicitly include mapped Opus SSRCs as well as selected
video layers, so the catch-all `any: 0` filter does not suppress conversation audio.
Audio-only mode requests no video layers; disabling audio omits audio subscriptions.
Other video codecs are not advertised as supported.

Recordings contain `conversation.wav` (48 kHz stereo), one MP4 per received camera
or watched stream, retained encoded source files, mux logs and `manifest.json`.
The manifest reports real received packet/frame counts, durations, audio peak/RMS
and mux errors. Audio uses a fixed RTP-based timeline and mixes independent speakers;
missing speech remains silence. Video starts at a configuration-bearing keyframe.
Sources that do not deliver that keyframe cannot produce a valid clip. Recording
allows a short startup window; user camera changes can still make a clip incomplete.

Only the selected channel's announced streams can be watched, up to eight at once.
Automatic watching is opt-in. Manual unwatch remains in effect until explicitly
watched again or the channel changes. Retry events and source failures are visible.
An incoming stream failing does not automatically stop the primary voice transport.

Live scripts, personal interfaces, tokens, media and logs are excluded from
source publication. Automated public tests use synthetic fixtures only.

## Verification and references

With tests enabled, `ctest -R native_media --output-on-failure` checks authenticated
RTP, tamper rejection, nonce uniqueness, sequence wrap/reordering/repair, native
Opus decoding on a fixed 20-second timeline, cached source framing and a decodable
10-second camera recording. Live results and their limits are in [LIVE_TESTS](LIVE_TESTS.md).

Protocol research used [Discord voice documentation](https://docs.discord.com/developers/topics/voice-connections),
[official libdave C++ sources](https://github.com/discord/libdave/tree/main/cpp),
[MLS++](https://github.com/cisco/mlspp), and the public implementations
[discord.py-self](https://github.com/dolfies/discord.py-self) and
[discord-native-voice](https://github.com/dolfies/discord-native-voice) to verify
undocumented video/Go Live signaling. No Python/Rust/Node source is bundled or
required at runtime. Sender counters alone cannot establish remote UI quality or
prove that every Discord receiver will never display error 2012.
