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
