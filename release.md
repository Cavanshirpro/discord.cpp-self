# discord.cpp-self v0.2.2

This update improves HTTP session continuity, message delivery/error handling,
and camera/Go Live controls. Package version: **0.2.2**.

## Changes

- Add `MediaSession::set_outputs(camera, go_live)` to change outputs without
  leaving the voice channel or interrupting incoming media and recording.
- Wait for outgoing stream stop acknowledgement before recreating it, reject
  stale asynchronous stream connections, and apply current source/output
  preferences when a connection finishes preparing.
- Add a fresh `MessageOptions::nonce` and default `enforce_nonce=true`.
  Retain the same options/nonce for the same logical send after an ambiguous
  failure; create new options for an intentionally new message. Deduplication
  remains limited to Discord's recent-message window.
- Classify verification/CAPTCHA and account-restriction errors before ordinary
  HTTP 429 retry handling. Preserve Discord error codes on exhausted rate-limit
  retries. Stop further REST requests with a rejected token until credentials
  change.
- Reuse HTTP connections and server-issued cookies in memory for one credential.
  Reset request options after each transfer and discard the session when the
  token changes. Client instances have separate cookie stores; no browser
  cookies are imported and no cookie file is written.
- Derive the library User-Agent version from CMake metadata.
- Clean up temporary DAVE session/encryptor handles when voice construction
  fails, and declare the native test's direct libcurl dependency for shared builds.

## Validation and limits

Windows/MinGW static and shared native-media builds passed unit, HTTP/Gateway
loopback, native media and HTTP/1.1 session tests. The new session fixture checks
real connection reuse, cookie path scoping, request-method/body reset, and
credential/Client isolation using localhost and fake credentials.
The Gateway fixture also checks heartbeat progress during REST rate-limit waits
with timing headroom for CI scheduling.

No new live message or camera/Go Live toggle test is claimed for these changes.
These fixes do not establish why an account was restricted or guarantee that
Discord will accept account activity. Browser/TLS fingerprint impersonation is
not included. See the [reliability review](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.2.2/docs/RELIABILITY_REVIEW_2026-10-29.md)
for the remaining limits.

Rebuild consumers against matching headers and libraries: `MessageOptions`
gained members and the native media API gained output controls. Runtime remains
C++ with optional native FFmpeg; no Node/Python/Rust runtime dependency is added.
Personal interfaces, credentials, account journals and recordings are excluded.

## Downloads

- [Source ZIP](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/discord.cpp-self-v0.2.2.zip)
- [Source TAR.GZ](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/discord.cpp-self-v0.2.2.tar.gz)
- [vcpkg overlay](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/discord.cpp-self-v0.2.2-vcpkg-overlay.zip)
- [MSYS2 MINGW64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/mingw-w64-x86_64-discord-cpp-self-0.2.2-1-any.pkg.tar.zst)
- [MSYS2 UCRT64 package](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/mingw-w64-ucrt-x86_64-discord-cpp-self-0.2.2-1-any.pkg.tar.zst)
- [MSYS2 recipe](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/discord.cpp-self-v0.2.2-msys2-recipe.zip)
- [Release notes](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/release.md)
- [SHA256SUMS](https://github.com/Cavanshirpro/discord.cpp-self/releases/download/v0.2.2/SHA256SUMS)

Prebuilt MSYS2 packages contain the REST/Gateway core. Build optional native media
from source using the [media guide](https://github.com/Cavanshirpro/discord.cpp-self/blob/v0.2.2/docs/MEDIA_NATIVE.md).

Copyright (C) 2026 Cavanshirpro. **LGPL-3.0-or-later**.
