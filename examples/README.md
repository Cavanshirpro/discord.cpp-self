# Examples

All examples use a normal Discord user account. Their code comments explain the
event flow, object lifetimes and the important API choices. No token is included.

| Example | What it demonstrates |
| --- | --- |
| [ping.cpp](ping.cpp) | Your own `!ping` command, READY and message replies |
| [commands.cpp](commands.cpp) | The prefix router, `!echo`, errors and `!quit` |
| [rest.cpp](rest.cpp) | Synchronous REST without a Gateway connection |
| [auto_reply.cpp](auto_reply.cpp) | A responder that keeps listening in one channel |
| [auto_reply_handler.hpp](auto_reply_handler.hpp) | Editable reply rules, owner controls, cooldowns and replay filtering |
| [check_connection.cpp](check_connection.cpp) | An opt-in read-only connection/heartbeat probe |
| [account_tests.cpp](account_tests.cpp) | Advanced opt-in two-account tests and a local result journal |
| [media.cpp](media.cpp) | Opt-in camera, microphone and Go Live; see [setup](../docs/MEDIA.md) |

## Build and run

In **MSYS2 MINGW64**, from the repository root:

```bash
cmake --preset mingw64
cmake --build --preset mingw64
read -rs -p 'Discord token: ' DISCORD_TOKEN; echo
export DISCORD_TOKEN
./build/mingw64/discord_self_commands.exe
unset DISCORD_TOKEN
```

On Linux, use the `default` preset and omit `.exe`. Follow
[Building](../docs/BUILDING.md) for dependencies. Building does not connect to
Discord; running an example with a real token does. `.env` is not loaded automatically.

## Continuous auto-reply

The program stays connected until you stop it. `Client::run()` receives messages
while a separate Gateway thread maintains heartbeats and reconnect/resume.
There is no busy loop or sleep inside a message callback.

1. Enable Discord Developer Mode and copy the ID of the channel you want to answer
   in. The account must have access to read and reply there. An existing DM channel
   also works; the example does not create a new DM itself.
2. Set `DISCORD_TOKEN` as above, then set the channel and run:

```bash
export DISCORD_REPLY_CHANNEL='123456789012345678'
./build/mingw64/discord_self_auto_reply.exe
```

3. Send a trigger from another account in that channel:

   | Incoming text | Automatic reply |
   | --- | --- |
   | `hello` or `hi` | A greeting and a hint to use `!help` |
   | `!ping` | `Pong!` |
   | `!help` | The supported triggers |
   | `!about` | A short description of the example |

4. From your logged-in account, use these commands in the same channel:

   | Owner command | Effect |
   | --- | --- |
   | `!autoreply off` | Pause automatic replies and keep listening |
   | `!autoreply on` | Enable replies again |
   | `!autoreply status` | Print the current state in the terminal |
   | `!autoreply quit` | Close the client and exit |

`Ctrl+C` also stops it. Owner controls print status locally and do not send an
extra message. The responder ignores other channels, bot messages, its own
messages and unknown triggers. ASCII triggers ignore surrounding whitespace and
letter case; this is not full Unicode case folding.

Edit `response()` in `auto_reply_handler.hpp` to change the trigger/reply table.
The cooldown constants are nearby: at most one reply every two seconds globally
and one per user every 30 seconds. Messages received during a cooldown are skipped,
not queued. The last 512 recognized message IDs are remembered to suppress replays;
the per-user cooldown map expires old entries. Mentions and reply pings stay off.

The program runs while its terminal/process is alive. It recovers temporary
disconnects within the client's reconnect budget. Authentication, access,
verification, fatal Gateway failures or an exhausted budget end the process;
an error exit is visible to a service supervisor. This example does not install
an operating-system service or promise recovery from every account restriction.

## Manual account probes

`check_connection` and `account_tests` are not built by default. They require
`-DDISCORD_SELF_BUILD_LIVE_EXAMPLES=ON`. The account harness's `--run` flag sends
real messages/DMs and may request friendship; discovery without an action flag is
read-only. Read [Testing](../docs/TESTING.md) before running either probe.

The offline suite tests the responder's actual handler using fake credentials and
local HTTP/WebSocket fixtures. It verifies filtering, normalization, cooldowns,
replay suppression, owner controls and heartbeat progress without Discord access.
