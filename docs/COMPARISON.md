# Comparison with discord.py-self

This is an independent C++ library, not a source translation or compatibility
layer. Comparison checked on 2026-10-20 against the reference project's
[README](https://github.com/dolfies/discord.py-self),
[message API](https://github.com/dolfies/discord.py-self/blob/master/discord/abc.py)
and [HTTP implementation](https://github.com/dolfies/discord.py-self/blob/master/discord/http.py).

The table describes v0.2.0, including its optional
[camera/microphone/Go Live backend](MEDIA.md) through C++ RTP/DAVE and native FFmpeg.
The future user-client scope is tracked in the [research inventory](USER_API_SCOPE.md).

| Area | discord.cpp-self 0.2.0 |
| --- | --- |
| Language/API | C++20 values, callbacks and synchronous REST; no Python API compatibility |
| Async model | Dedicated Gateway thread; coroutine REST is planned |
| Authentication | Raw user tokens; separate bot support is outside this project |
| Messaging | Send/reply/edit/delete/reactions/history/typing/DM implemented |
| User-specific routes | Around-based single-message lookup, recipients-array DM creation, user guild listing |
| Profiles/members | Visible user/profile/guild-member data and extended profile JSON |
| Relationships | List and request operations; no broad relationship-management surface |
| Gateway | Heartbeat/ACK, reconnect/resume, invalid sessions, fragmentation and zlib-stream |
| Rate limits | Serialized per client; route/shared-bucket/global deadlines with bounded retry |
| Cache/events | Bounded message cache, patches and raw events; no comprehensive guild/member cache |
| Commands | Small own-message prefix router; no full command extension framework |
| Custom embeds | No custom embed sender; incoming/link previews do not imply send support |
| Files | Uploads planned |
| Voice/video | Optional C++ guild camera/audio/Go Live, receiving, watching and recording |
| Read states/settings/interactions/billing | Not implemented as high-level APIs |
| Verification | VerificationRequired exception; challenge payload/completion API planned; no solver |
| License | LGPL-3.0-or-later; the reference project is MIT |

The Python project covers substantially more of the user API. Our first release
focuses on a tested foundation and a short C++ surface. Raw JSON access does not
turn an unimplemented feature into supported coverage. See [API](API.md),
[testing](TESTING.md) and the [roadmap](../ROADMAP.md).
