# discord.cpp-self

[![Build](https://github.com/Cavanshirpro/discord.cpp-self/actions/workflows/ci.yml/badge.svg)](https://github.com/Cavanshirpro/discord.cpp-self/actions/workflows/ci.yml)
[![Packages](https://github.com/Cavanshirpro/discord.cpp-self/actions/workflows/packages.yml/badge.svg)](https://github.com/Cavanshirpro/discord.cpp-self/actions/workflows/packages.yml)
[![Release](https://img.shields.io/github/v/release/Cavanshirpro/discord.cpp-self?include_prereleases)](https://github.com/Cavanshirpro/discord.cpp-self/releases)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![License: LGPL v3 or later](https://img.shields.io/badge/license-LGPL--3.0--or--later-blue.svg)](LICENSE)

A C++20 library for Discord **user accounts**, with a small API you can start using
in a few lines. A personal project by [Cavanshirpro](https://github.com/Cavanshirpro).

Release **v0.1** (package version `0.1.0`) is an early release. Inspired by
[discord.py-self](https://github.com/dolfies/discord.py-self), independently written
in C++, and deliberately different in API design. It does not offer full endpoint parity.

```cpp
#include <discord/discord.hpp>

int main() {
    discord::Client client;
    client.on_message([&](const discord::Message& message) {
        auto self = client.user();
        if (self && message.author.id == self->id && message.content == "!ping") {
            (void)message.reply("pong");
        }
    });
    client.run(discord::token_from_env());
}
```

## What works

- Send, reply, edit, delete, reactions, message history and DMs.
- Fetch users, profiles, guilds, channels, members and relationships.
- Multiple event listeners and own-message prefix commands with quoted arguments.
- Gateway heartbeats, reconnect/resume, fragmented frames and zlib-stream decompression.
- Route/shared-bucket/global HTTP rate limits, bounded retries and cancellation.
- A bounded message cache, partial update events and raw dispatch events.
- Static/shared builds and an installed CMake package named `discordSelf`.

There is no custom embed sender, bot-token mode, voice/video, file upload or
coroutine interface in this release. REST calls are synchronous; Gateway I/O runs
on a separate thread. See the [feature comparison](docs/COMPARISON.md).

Discord prohibits user-account automation. Its user API is unofficial and may
change without notice; account restrictions and verification challenges are real
limitations. This project does not bypass them. See the
[reference project's notice](https://github.com/dolfies/discord.py-self#readme).

## Build on Windows with MSYS2

Use the **MSYS2 MINGW64** terminal. Do not mix MINGW64 and UCRT64 dependencies.

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-curl mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-zlib mingw-w64-x86_64-python
git clone https://github.com/Cavanshirpro/discord.cpp-self.git
cd discord.cpp-self
cmake --preset mingw64
cmake --build --preset mingw64
ctest --preset mingw64
```

The preset expects MSYS2 at `C:/msys64`. From PowerShell, use `scripts/build.ps1`.
Requirements: a C++20 compiler, CMake 3.25+, Ninja, libcurl 7.86+ with **ws/wss**
support, nlohmann/json 3.11+, and zlib. Python 3.9+ is required for offline tests.
The matching MSYS2 `bin` directory must be on `PATH` when running executables.

On Linux, install the equivalent development packages, then use:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Check that `curl --version` lists `ws` and `wss`. Older distribution packages may
omit WebSockets. See the [build guide](docs/BUILDING.md) for shared builds and installation.

## Add it to your project

The simplest way to start is to add the source to your project:

```cmake
add_subdirectory(external/discord.cpp-self)
target_link_libraries(my_app PRIVATE discord::self)
```

Examples and tests default to off in a parent project. For installed packages:

```cmake
find_package(discordSelf 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE discord::self)
```

Install with `cmake --install <build-directory> --prefix <install-prefix>` and add
that prefix to `CMAKE_PREFIX_PATH`. Use compatible compiler and runtime ABIs.

You can also use CMake `FetchContent`, the Conan 2 recipe, or the vcpkg overlay
attached to [v0.1](https://github.com/Cavanshirpro/discord.cpp-self/releases/tag/v0.1).
The [installation guide](docs/DISTRIBUTION.md) has complete examples. ConanCenter
and the central vcpkg registry are separate submissions; no central listing is
claimed by this release. Dependencies are not bundled in source archives.

## Run an example

Examples read `DISCORD_TOKEN` from the process environment; `.env` files are not
loaded automatically. In MINGW64, keep the credential out of shell history:

```bash
read -rs -p 'Discord token: ' DISCORD_TOKEN; echo
export DISCORD_TOKEN
./build/mingw64/discord_self_ping.exe
unset DISCORD_TOKEN
```

CTest uses only fake credentials and loopback servers. Manual account examples
are disabled unless `DISCORD_SELF_BUILD_LIVE_EXAMPLES=ON` is set explicitly.

## Commands

```cpp
discord::Client client;
discord::Commands commands(client, "!");
commands.add("ping", [](const discord::Context& ctx) { (void)ctx.reply("pong"); });
commands.add("quit", [&](const discord::Context&) { client.close(); });
client.on_message([&](const discord::Message& message) { (void)commands.process(message); });
client.run(discord::token_from_env());
```

Only the logged-in user's messages invoke commands. Names are case-sensitive.
`ctx.arguments` contains the argument text; `ctx.args` handles quotes and escapes.
Keep the command router alive while its callback can run.

Mentions and reply-author notifications are off by default. IDs use `Snowflake`,
stored as `uint64_t` and serialized as strings. Model values are snapshots;
`edit()` returns a new message. `login()` enables REST but validates credentials
only on the first request. `user()` is populated after Gateway `READY`; use
`fetch_me()` in REST-only applications.

## Documentation

- [API reference](docs/API.md), [architecture](docs/ARCHITECTURE.md) and [documentation index](docs/README.md)
- [Testing](docs/TESTING.md) and [anonymized live findings](docs/LIVE_TESTS.md)
- [Comparison](docs/COMPARISON.md), [roadmap](ROADMAP.md) and [changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md), [security](SECURITY.md) and [release process](docs/RELEASING.md)
- [Package installation](docs/DISTRIBUTION.md) and [GitHub publishing setup](docs/PUBLISHING.md)

## License

Copyright (C) 2026 Cavanshirpro. **LGPL-3.0-or-later**.
See [LICENSE](LICENSE), the incorporated GPL v3 text in [COPYING](COPYING),
the completed [project copyright notice](NOTICE),
[distribution guidance](docs/LICENSING.md) and [third-party notices](THIRD_PARTY.md).
Discord is a trademark of Discord Inc. This project is unofficial and unaffiliated.
