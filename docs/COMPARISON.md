# Comparison with discord.py-self

This is an independent C++ library, not a source translation or compatibility
layer. Comparison checked on 2026-10-20 against the reference project's
[README](https://github.com/dolfies/discord.py-self),
[message API](https://github.com/dolfies/discord.py-self/blob/master/discord/abc.py)
and [HTTP implementation](https://github.com/dolfies/discord.py-self/blob/master/discord/http.py).

| Area | discord.cpp-self 0.1.0 |
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
| Files/voice/video | Not implemented |
| Read states/settings/interactions/billing | Not implemented as high-level APIs |
| Verification/detection | No CAPTCHA solver, browser impersonation or detection-evasion layer |
| License | LGPL-3.0-or-later; the reference project is MIT |

The Python project covers substantially more of the user API. Our first release
focuses on a tested foundation and a short C++ surface. Raw JSON access does not
turn an unimplemented feature into supported coverage. See [API](API.md),
[testing](TESTING.md) and the [roadmap](../ROADMAP.md).
