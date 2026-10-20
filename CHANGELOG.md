# Changelog

## v0.1 (package 0.1.0) — 2026-10-20

Initial early release under LGPL-3.0-or-later.

- C++20 client with synchronous REST and a separate Gateway thread.
- User-account messaging, history, DMs, profiles, guilds, channels and memberships.
- Relationship listing/request APIs and typed verification failures.
- Gateway reconnect/resume, heartbeat ACK checks and zlib-stream transport.
- Shared-bucket/global rate limits, bounded queues/cache and cancellation.
- Own-message prefix commands, examples and offline HTTP/WebSocket fixtures.
- Installed CMake package, static/shared build support and source release tooling.
- Conan 2 recipe, commit-pinned vcpkg overlay and pkg-config metadata.
- Linux/Windows CI, package checks and automated GitHub/Conan publishing workflows.

Live two-account messaging, DM receipt, profiles and memberships were checked.
Friend-request completion was blocked by CAPTCHA; later account/token failures
are recorded without claiming a cause. Live edit/delete, voice, uploads, coroutine
REST and complete Python-library parity are not part of this release's validation.
