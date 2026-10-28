# Installation and distribution

Release tag: **v0.2.1**. CMake/Conan/vcpkg version: **0.2.1**.
All methods expose the same public CMake target, `discord::self`.

| Method | Available from this repository | External registration |
| --- | --- | --- |
| GitHub Release source | ZIP, TAR.GZ and checksums | None |
| CMake source / FetchContent | Build directly from source | None |
| Conan 2 | Recipe and package consumer test | Optional Conan remote; ConanCenter requires review |
| vcpkg | Commit-pinned overlay asset | Central registry requires review |
| pkg-config | Installed `discord-self.pc` | None |
| MSYS2 | Native packages and a source-building PKGBUILD | Official pacman repository requires review |

There is no central ConanCenter/vcpkg listing claimed by v0.1. GitHub Packages
does not provide a native Conan or vcpkg registry; use GitHub Releases for the
source/overlay and a compatible Conan remote for Conan packages.

## CMake FetchContent

Install the dependencies listed in [Building](BUILDING.md), including libcurl
with TLS and WebSocket support. Then:

```cmake
include(FetchContent)
FetchContent_Declare(discord_self
    GIT_REPOSITORY https://github.com/Cavanshirpro/discord.cpp-self.git
    GIT_TAG v0.2.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(discord_self)
target_link_libraries(my_app PRIVATE discord::self)
```

Examples and tests default to off when used as a subproject. For reproducible
builds, replace the tag with the full release commit and omit `GIT_SHALLOW`.
An installed library uses `find_package(discordSelf 0.1 CONFIG REQUIRED)`.

## Conan 2

The recipe requires Conan 2.21 or newer; CI pins the tested Conan tool version.
It enables WebSockets/TLS in libcurl and builds without account examples or tests.
Run from a compiler-enabled terminal (MSYS2 MINGW64 for the local MinGW setup):

```bash
conan profile detect
conan create . --build=missing -s compiler.cppstd=20
# Optional shared library:
conan create . --build=missing -s compiler.cppstd=20 -o '&:shared=True'
```

On Windows, add `-o 'libcurl/*:with_ssl=schannel'` to use the native certificate
store. The default OpenSSL configuration on other platforms uses the system CA
locations; custom TLS builds must provide a valid trust store.

Install Conan in a Python environment that can see the selected compiler. On
MINGW64, put `C:/msys64/mingw64/bin` on PATH and use Ninja. Dependency builds may
need Autoconf, Automake, libtool and pkg-config; the Conan recipe can also supply
its required build tools. Do not mix MSVC-built and MinGW-built binaries.

After creating the package, add this `conanfile.txt` to your application:

```ini
[requires]
discord-cpp-self/0.2.1
[generators]
CMakeDeps
CMakeToolchain
[layout]
cmake_layout
```

```bash
conan install . --build=missing -s compiler.cppstd=20
cmake --preset conan-release
cmake --build --preset conan-release
```

Your `CMakeLists.txt` uses `find_package(discordSelf CONFIG REQUIRED)` and links
`discord::self`. Multi-configuration generators use Conan's `conan-default`
configure preset and the relevant build preset; inspect `cmake --list-presets`.
Consumers of a remote package must first add the publisher's remote with
`conan remote add <name> <url>`. No remote is assumed to exist yet.

The recipe installs licenses and both CMake/pkg-config metadata. The
`test_package` builds and executes a consumer that constructs the client and
command router without using Discord credentials.

## vcpkg overlay

Download `discord.cpp-self-v0.2.1-vcpkg-overlay.zip` from the GitHub Release and
extract it. The archive contains `vcpkg-overlay/discord-cpp-self`. Then:

```bash
vcpkg install discord-cpp-self --overlay-ports=/path/to/vcpkg-overlay
```

For a manifest-based application:

```json
{
  "name": "my-app",
  "version-string": "0.2.1",
  "dependencies": ["discord-cpp-self"]
}
```

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=/path/to/vcpkg-overlay
cmake --build build
```

Use a compatible triplet: for MSYS2 MINGW64, `x64-mingw-dynamic` or
`x64-mingw-static`, with `VCPKG_TARGET_TRIPLET` and `VCPKG_HOST_TRIPLET` set
consistently. The default Windows `x64-windows` triplet uses MSVC. CI currently
tests the overlay on `x64-linux`; other triplets are not implied to be verified.

The template under `packaging/vcpkg` is for maintainers. Use the generated port
from the release asset; its commit and SHA512 are filled with actual values.

## pkg-config

After installing with CMake, add `<prefix>/lib/pkgconfig` (or your platform's
configured lib directory) to `PKG_CONFIG_PATH`:

```bash
c++ main.cpp $(pkg-config --cflags --libs discord-self) -o my-app
# For a static dependency closure:
c++ main.cpp $(pkg-config --cflags --static --libs discord-self) -o my-app
```

Dependency development packages must provide their own pkg-config files. CMake
is the recommended consumer interface on Windows.

## Verify downloads

For native MSYS2 package installation with `pacman -U`, building a recipe, or
proposing a package to the official repository, see [MSYS2](../packaging/msys2/README.md).
The installed headers retain their [editor hover help](EDITORS.md).

On Linux/MSYS2, run `sha256sum -c SHA256SUMS` beside the downloaded assets. On
PowerShell, use `Get-FileHash <asset> -Algorithm SHA256` and compare it with the
matching line. Keep the LGPL files when redistributing the library; see
[Licensing](LICENSING.md).
