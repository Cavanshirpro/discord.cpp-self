# Local validation

Validated on 2026-10-20 with Windows, MSYS2 MINGW64, GCC 16.2.0,
CMake 4.4.3, libcurl 8.16.0, nlohmann/json 3.12.0 and zlib 1.3.1.

Run from PowerShell:

```powershell
.\scripts\build.ps1
```

Result after the final review: both CTest suites pass in static Debug and shared
Release builds. Unit suite: 82 checks. Loopback suite:

1. REST send/reply/edit/delete/reactions/history/DM, quoted commands, partial
   updates, bulk deletion, callback error handling, compressed and fragmented
   WebSocket frames, interleaved ping, heartbeat during HTTP 429, reconnect/resume.
   Also validates the user-specific DM body, guild route, around-based message
   lookup (including nearby/missing IDs), partial user updates and absence of
   outgoing custom embeds. Profile/member/relationship parsing, friend request
   route/body, typed CAPTCHA failures and the 403/40001 profile fallback are covered.
2. Invalid session discards resume state and identifies again.
3. Fatal Gateway close code 4004 reaches the caller.
4. Missing heartbeat ACK terminates after the configured reconnect budget.
5. Callback exceptions reach the caller and shut down the network worker.
6. Event queue overflow produces an error without silently dropping events.
7. Closing the client interrupts a long rate limit wait.
8. The same client runs again after closing its previous run.
9. Missing READY/RESUMED times out even while heartbeat ACKs arrive.
10. Missing HELLO times out.
11. The auto-reply example ignores other channels, bot/own messages and unknown
    triggers; normalizes ASCII triggers; applies global/per-user cooldowns;
    suppresses replayed IDs; obeys owner pause/resume/status/quit; and keeps heartbeats alive.

The install/export path was tested separately for static and shared libraries:
install, configure `tests/consumer` with `find_package(discordSelf)`, build, then
execute the consumer. Both print the package version and exit successfully.

All integration fixtures bind to `127.0.0.1` on ephemeral ports, use a fake token,
and depend only on Python's standard library. They do not contact Discord.
GitHub Actions provides Linux and Windows static/shared checks, installed-package
consumers, and separate Conan/vcpkg package checks. Consult the
[build runs](https://github.com/Cavanshirpro/discord.cpp-self/actions/workflows/ci.yml)
and [package runs](https://github.com/Cavanshirpro/discord.cpp-self/actions/workflows/packages.yml)
for results for a specific commit. macOS and MSVC runtime validation remain
outside the initial local validation.

An explicitly requested live connection check was also run on 2026-10-20 using
the user's local credential file. It passed REST account verification, the user
guild list, authenticated Gateway READY and heartbeat acknowledgement. No token,
account identity or message contents were printed or written to reports. No
messages, reactions or typing indicators were sent. The file is Git-ignored;
a source-file scan found zero copies of its token.

An additional explicitly authorized two-account live run verified both directions
of guild message sends/history reads and DM sends/recipient reads, guild/channel
details, memberships and profiles. The friend request encountered a CAPTCHA;
both accounts were already friends. A later A report send returned 403/340013,
and A authentication subsequently returned 401. Reporting was completed by B;
all 11 report messages were read back. No friendship was removed or accepted,
and the challenged request was not retried. See docs/LIVE_TESTS.md. Message
edits/deletes and full API compatibility remain unverified.
