# discord.cpp-self v0.2.1

This update fixes media deaf/mute handling and prevents duplicate outgoing file
audio by default. Package version: **0.2.1**.

## Fixes

- Deaf state now suppresses Opus on every voice transport, including watched
  Go Live streams, rather than relying solely on the main Gateway flag.
- Local audio playback stops immediately when deafened or disabled; pending
  waveOut buffers and queued PCM are cleared.
- Add independent `set_audio_playback()` and `set_muted()` controls. Local
  playback, incoming media and outgoing file/device audio have distinct states.
- The file audio source goes to the voice channel once by default. Go Live
  audio requires `stream_audio=true`; `voice_audio=false` selects stream-only
  delivery. Sending identical audio to both destinations remains an explicit
  choice, since viewers can otherwise hear two copies.
- Snapshots expose mute, requested/active playback and audio destination flags.
- Regression fixtures check muted output, deafened stream audio rejection and
  default audio routing without real accounts or audio devices.

The personal TUI remains external to the repository. Its local listening output
defaults to off when used alongside the normal Discord client. The SDK never
routes received conversation packets into the outgoing file/device source.
These fixes address confirmed routing/control defects; absence of acoustic echo
on a particular speaker/microphone setup requires a listening test.

## Downloads

- [Source ZIP](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/discord.cpp-self-v0.2.1.zip)
- [Source TAR.GZ](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/discord.cpp-self-v0.2.1.tar.gz)
- [vcpkg overlay](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/discord.cpp-self-v0.2.1-vcpkg-overlay.zip)
- [MSYS2 MINGW64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/mingw-w64-x86_64-discord-cpp-self-0.2.1-1-any.pkg.tar.zst)
- [MSYS2 UCRT64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/mingw-w64-ucrt-x86_64-discord-cpp-self-0.2.1-1-any.pkg.tar.zst)
- [MSYS2 recipe](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/discord.cpp-self-v0.2.1-msys2-recipe.zip)
- [Release notes](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/release.md)
- [SHA256SUMS](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.1/SHA256SUMS)

Prebuilt packages contain the REST/Gateway core; optional native media is built
from source using [the media guide](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.2.1/docs/MEDIA_NATIVE.md).
Rebuild media consumers against the new headers/library. No runtime Node/Python/
Rust dependency is introduced. Personal configuration, tokens, scripts and media
remain excluded from publication. See [v0.2.0](https://github.com/Cavanshirpro/discord.cpp-self/releases/tag/v0.2.0) for the native backend's
original feature list and live validation limits.

Copyright (C) 2026 Cavanshirpro. **LGPL-3.0-or-later**.
