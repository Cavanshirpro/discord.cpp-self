# Roadmap

## 0.1 foundation

- [x] C++20, static/shared CMake library and installed package
- [x] HTTP errors, shared-bucket/global rate limits, bounded waits and cancellation
- [x] Gateway heartbeat, reconnect/resume and zlib-stream decompression
- [x] User, profile, member, guild, channel, relationship and message models
- [x] Messaging, reactions, history, typing and DMs
- [x] Event listeners, message cache and partial updates
- [x] Own-message prefix commands and quoted arguments
- [x] Offline network fixtures and opt-in live examples
- [x] Two-account live messaging, DM, profile and membership checks
- [x] LGPL licensing, source distribution and release documentation

## Next

- [ ] Coroutine interface, timed event waits and cancellation
- [ ] Connection pooling and concurrent REST scheduling
- [ ] Multipart attachments and message files
- [ ] Presence updates and read-state models
- [ ] Richer role/permission APIs and configurable guild/channel caches
- [ ] Command help, typed arguments and cooldowns
- [ ] Successful new friend-request verification on suitable test accounts
- [ ] Live edit/delete validation in a designated test channel
- [ ] macOS and MSVC runtime validation
- [ ] Shared components for a separate bot library
- [ ] Research voice as an optional module

Keep short examples, explicit lifetimes and predictable errors as the API grows.
Full endpoint parity with the Python project is not a release promise.
