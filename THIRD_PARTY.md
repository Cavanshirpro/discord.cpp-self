# References and dependencies

This project is an independent C++ implementation. No Python source is vendored.
The API ergonomics and user Gateway protocol were researched using
[dolfies/discord.py-self](https://github.com/dolfies/discord.py-self) (MIT;
Rapptz and contributors). No affiliation or endorsement is implied.

Dependencies are resolved from the system, not bundled:

| Dependency | Purpose | License |
| --- | --- | --- |
| [libcurl](https://curl.se/docs/copyright.html) | HTTPS and WebSocket transport | curl license |
| [nlohmann/json](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT) | JSON | MIT |
| [zlib](https://zlib.net/zlib_license.html) | Gateway stream decompression | zlib |

When distributing dependencies or their DLLs, include their applicable license
notices and the notices for any transitive dependencies in that distribution.
Discord is a trademark of Discord Inc. This is an unofficial project.

## Optional C++ media backend (unreleased)

| Dependency | Purpose | License |
| --- | --- | --- |
| [Discord libdave](https://github.com/discord/libdave) | Official C++ DAVE frame encryption and MLS sessions | MIT |
| [Cisco MLS++](https://github.com/cisco/mlspp) | MLS group protocol used by libdave | BSD-2-Clause |
| [OpenSSL](https://www.openssl.org/source/license.html) | AES-GCM transport and libdave cryptography | Apache-2.0 (OpenSSL 3) |
| [Opus](https://opus-codec.org/license/) | Native received audio decoder | BSD-style; see its patent/license notices |
| [FFmpeg](https://ffmpeg.org/legal.html) | External native encoder and muxer | Build dependent; libx264-enabled builds commonly GPL |

The setup script pins libdave and MLS++ revisions and installs their source
license notices into the ignored native prefix. Include all applicable notices
when distributing their static libraries, DLLs or other transitive dependencies.
FFmpeg is a separate executable and is not bundled by the CMake install.

Public discord.py-self and discord-native-voice implementations (both MIT) were
consulted for undocumented video/Go Live signaling. No Python, Rust or Node source
or runtime is bundled with this project.
