# Building and installation

| Component | Requirement |
| --- | --- |
| Compiler | C++20 including `std::jthread` |
| CMake | 3.25+ |
| libcurl | 7.86+, TLS and ws/wss enabled |
| nlohmann/json | 3.11+ |
| zlib | System development package |
| Python | 3.9+, when tests are enabled |

MSYS2 MINGW64 is the primary Windows toolchain. Install the packages in the
[README](../README.md), and keep its matching `bin` directory on `PATH` at runtime.
Do not copy runtime DLLs between MINGW64 and UCRT64 installations.

## Configure explicitly

In a shell configured for your compiler:

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure
```

For a custom Windows location, use `scripts/build.ps1 -MsysRoot D:\msys64`.
The `mingw64` preset expects `C:/msys64`; the `default` preset uses the shell's compiler.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `BUILD_SHARED_LIBS` | `OFF` | DLL/shared object instead of a static archive |
| `DISCORD_SELF_BUILD_EXAMPLES` | Top-level only | Ping, commands, REST and auto-reply examples |
| `DISCORD_SELF_BUILD_TESTS` | Top-level only | Offline tests; Python is required |
| `DISCORD_SELF_BUILD_LIVE_EXAMPLES` | `OFF` | Manual programs that can contact real accounts |

## Shared builds and installation

Use a separate build directory when switching linkage:

```bash
cmake -S . -B build/shared -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build/shared
ctest --test-dir build/shared --output-on-failure
cmake --install build/shared --prefix /path/to/install
```

The package installs headers, the library, imported CMake targets, version
metadata and license notices. Dependencies are resolved from the system; their
DLLs are not bundled. Set the consumer's `CMAKE_PREFIX_PATH` to the install prefix.

```cmake
cmake_minimum_required(VERSION 3.25)
project(example LANGUAGES CXX)
find_package(discordSelf 0.1 CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example PRIVATE discord::self)
```

For a Windows DLL installation, add both its `bin` directory and the matching
MSYS2 runtime directory to `PATH`. Use compatible compiler/runtime ABIs.
See [licensing](LICENSING.md) before distributing a linked application.

Prebuilt MSYS2 packages and a source-building recipe are described in the
[MSYS2 guide](../packaging/msys2/README.md). Hover documentation and editor
configuration are covered in [Editors](EDITORS.md).
