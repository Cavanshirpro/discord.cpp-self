# discord.cpp-self v0.2.0

This release adds an optional native C++20 voice/video backend to the existing
Discord user-account REST/Gateway library. Package version: **0.2.0**.

## Changes

- `MediaSession` sends camera, file audio and Go Live with independent looping
  inputs and live source/channel controls.
- Receive Opus speech and H264/H265 cameras/Go Live; optionally play audio on
  Windows and record mixed WAV plus a separate MP4 for each video source.
- Use official C++ libdave, MLS++, OpenSSL and libopus with native FFmpeg.
  Node, Python and Rust are not media/library runtime dependencies.
- Cache file encodes, bound capture/repair queues, repair RTP/RTX and expose
  actual participant/viewer metadata, RTCP feedback and sender pacing.
- Explicitly subscribe to Opus streams alongside video, fixing missing speech
  when the receive catch-all suppresses unspecified streams.
- Document a user-client roadmap covering settings, interactions, shop,
  subscriptions, quests and developer-presented verification challenges.
- Keep the personal test TUI, credentials, live scripts and recordings outside
  the source distribution. The public media API and JSON-console example remain.

## Downloads

- [Source ZIP](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/discord.cpp-self-v0.2.0.zip)
- [Source TAR.GZ](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/discord.cpp-self-v0.2.0.tar.gz)
- [vcpkg overlay](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/discord.cpp-self-v0.2.0-vcpkg-overlay.zip)
- [MSYS2 MINGW64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/mingw-w64-x86_64-discord-cpp-self-0.2.0-1-any.pkg.tar.zst)
- [MSYS2 UCRT64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/mingw-w64-ucrt-x86_64-discord-cpp-self-0.2.0-1-any.pkg.tar.zst)
- [MSYS2 source recipe](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/discord.cpp-self-v0.2.0-msys2-recipe.zip)
- [Release notes](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/release.md)
- [SHA256SUMS](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.0/SHA256SUMS)

Source archives include the library, documentation, examples, tests, package
recipes and LGPL notices; dependencies are not bundled. Prebuilt MSYS2 packages,
Conan and vcpkg recipes provide the REST/Gateway core with media disabled. For
media, build from source with `DISCORD_SELF_NATIVE_MEDIA=ON` following the
[native media guide](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.2.0/docs/MEDIA_NATIVE.md).
The matching MSYS2 toolchain's `bin` directory must be on PATH.

## Validation and limits

Static and DLL Windows suites cover the core HTTP/Gateway library and native
media transport, decoding, capture pipes, source caching and recording. An
installed CMake consumer links against the native build. A designated live test
produced a 20.000-second non-silent WAV, a 9.965-second camera MP4 and a
10.033-second watched-stream MP4; all decoded successfully. Measured outbound
video pacing averaged 33.33 ms (30 FPS). These results do not establish every
receiver's rendered FPS or guarantee elimination of Discord error 2012.

Media supports guild voice channels, H264 sending, H264/H265 receiving and Opus.
Windows audio monitoring uses waveOut. Other incoming codecs, private/group
calls and broad platform/device validation are future work. The media API is
independent of `Client` and uses a separate account Gateway session.

Shop, quests, full settings and challenge-completion APIs are roadmap items,
not newly implemented features. v0.2.0 still raises `VerificationRequired`
without a challenge payload/completion interface. Discord's user API remains
unofficial and account permissions/verification still apply.

Copyright (C) 2026 Cavanshirpro. **LGPL-3.0-or-later**.
