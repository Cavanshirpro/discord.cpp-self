# discord.cpp-self v0.1.1

A release of the C++20 library for Discord user accounts, with expanded,
commented examples and a continuous auto-responder. Package version: **0.1.1**.

## What's included

- English code comments explaining the examples' setup, callbacks and lifetimes.
- A channel-scoped auto-reply example with editable keyword/reply rules.
- Owner-only on/off/status/quit controls and Ctrl+C shutdown.
- Own-message/bot filtering, global and per-user cooldowns, bounded replay tracking.
- An offline HTTP/Gateway scenario covering the actual responder policy.
- This downloadable `release.md`, source archives and a commit-pinned vcpkg overlay.
- Public class/method/field hover documentation, VS Code/clangd setup and real LSP checks.
- Native MINGW64/UCRT64 MSYS2 packages and a source-building PKGBUILD recipe.

The library API remains the same as v0.1. The original v0.1 tag and source assets
remain available as a historical prerelease.

## Downloads

- [Source ZIP](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/discord.cpp-self-v0.1.1.zip)
- [Source TAR.GZ](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/discord.cpp-self-v0.1.1.tar.gz)
- [vcpkg overlay ZIP](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/discord.cpp-self-v0.1.1-vcpkg-overlay.zip)
- [MSYS2 MINGW64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/mingw-w64-x86_64-discord-cpp-self-0.1.1-1-any.pkg.tar.zst)
- [MSYS2 UCRT64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/mingw-w64-ucrt-x86_64-discord-cpp-self-0.1.1-1-any.pkg.tar.zst)
- [MSYS2 source recipe](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/discord.cpp-self-v0.1.1-msys2-recipe.zip)
- [Release notes (release.md)](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/release.md)
- [SHA256SUMS](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.1.1/SHA256SUMS)

The source archives contain the examples, documentation, CMake files, Conan recipe
and complete LGPL notices. Source archives do not bundle dependencies. The native
MSYS2 packages include this library's DLL, import library and documented headers;
pacman resolves their declared runtime dependencies. Choose the package matching
your compiler environment. Other compiler ABIs can build from source.

## Install through MSYS2

In **MSYS2 MINGW64**, after downloading the MINGW64 package above:

```bash
pacman -U ./mingw-w64-x86_64-discord-cpp-self-0.1.1-1-any.pkg.tar.zst
```

In **MSYS2 UCRT64**, use the corresponding UCRT64 package instead. Applications use
`find_package(discordSelf CONFIG REQUIRED)` and link `discord::self`.
These are project-hosted packages; central `pacman -S` installation requires MSYS2
repository acceptance. See the [MSYS2 guide](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.1.1/packaging/msys2/README.md).

Public headers include editor hover explanations for types, methods, parameters
and fields. See the [editor guide](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.1.1/docs/EDITORS.md)
for VS Code IntelliSense and clangd configuration. No Doxygen executable is needed.

## Run the auto-reply example

Build with a C++20 compiler, CMake 3.25+, libcurl 7.86+ with ws/wss and TLS,
nlohmann/json 3.11+, and zlib. In MSYS2 MINGW64:

```bash
cmake --preset mingw64
cmake --build --preset mingw64 --target discord_self_auto_reply
read -rs -p 'Discord token: ' DISCORD_TOKEN; echo
export DISCORD_TOKEN
export DISCORD_REPLY_CHANNEL='123456789012345678'
./build/mingw64/discord_self_auto_reply.exe
unset DISCORD_TOKEN
```

Use your channel's ID. From another account, send `hello`, `hi`, `!ping`,
`!help` or `!about` there. From your own account, use `!autoreply off`,
`!autoreply on`, `!autoreply status` or `!autoreply quit`. Ctrl+C also stops it.

Replies are limited to one every two seconds globally and one per user every
30 seconds. The program keeps listening while running and reconnects within the
configured budget. Account verification and fatal connection errors end it.

Read the [examples guide](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.1.1/examples/README.md)
and [installation guide](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.1.1/docs/DISTRIBUTION.md)
for the complete setup, customization and Conan/vcpkg instructions.

## Scope and license

This is an early user-account library; it does not offer bot-token mode, custom
embed sending, file uploads, voice/video, coroutines or complete discord.py-self
parity. Discord prohibits user-account automation and can require verification.
No account restriction or CAPTCHA bypass is provided.

Copyright (C) 2026 Cavanshirpro. **LGPL-3.0-or-later**.
