# Final user-API review — 2026-10-20

Reviewed the public headers, REST/Gateway implementation, models, caches, command
router, examples, tests, installed package and release documentation.

| Finding | Correction |
| --- | --- |
| Custom outgoing embeds were inappropriate for the user-account API | Removed the builder, outgoing field and example |
| Single-message retrieval used a bot-style route | Use history with `around=id`; reject empty/neighboring results |
| DM creation used `recipient_id` | Use the user API's `recipients` array |
| Guild listing assumed bot pagination | Use the user guild-list route with counts |
| Identify explicitly requested online presence | Send initial `unknown` status |
| Partial USER_UPDATE could erase existing fields | Merge with previous user JSON first |
| Dispatcher shutdown notification could race with waiting | Update stop state under the waiter's mutex |
| Retry exhaustion could forget a 429 deadline | Store deadlines before retry decisions; handle global headers |
| Repeated login with the same token forgot rate limits | Preserve learned deadlines for that credential |
| Embedded NUL/control characters could truncate requests | Reject control characters in tokens and paths |
| POSIX select could exceed FD_SETSIZE | Use poll on POSIX; retain Winsock select on Windows |
| Final reconnect error hid its original cause | Rethrow the last transport/Gateway error |
| Bare user lookup was denied in live account sessions | Fall back to the profile route only for 403/40001 |
| Verification responses looked like generic HTTP errors | Add VerificationRequired and never replay the challenge |
| Live credentials/journals were unsuitable for publication | Ignore credentials/build output; scan the publication manifest |

The final [comparison](COMPARISON.md) distinguishes implemented behavior from
planned work. Reference sources were inspected rather than copied or vendored.
No full parity, detection-evasion or universal compatibility claim is made.

Validation includes offline HTTP/WebSocket fixtures, static/shared builds and an
installed CMake consumer. CI covers Linux and Windows. Live account results are
recorded separately in [LIVE_TESTS.md](LIVE_TESTS.md); publishing checks never
repeat those account operations.
