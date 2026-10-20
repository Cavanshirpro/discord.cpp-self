# Testing

## Offline suite

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

CTest runs unit checks and a Python-standard-library loopback fixture. No Discord
credentials or outbound Discord calls are involved. Tests include exact IDs,
models, commands, cache eviction, rate limits, verification errors, message CRUD,
user-profile fallback, compressed/fragmented Gateway data, reconnect/resume,
invalid sessions, missing ACK/HELLO/READY, callback failures, queue overflow,
cancellation and sequential reuse of one client. See [validation](../tests/VALIDATION.md).

Packaging checks build a standalone `tests/consumer` using the installed
`find_package(discordSelf)` configuration. Both static and shared configurations
run the offline suite in CI.

## Optional connection probe

Configure with `-DDISCORD_SELF_BUILD_LIVE_EXAMPLES=ON`. Set `DISCORD_TOKEN`, then
run `discord_self_check_connection`. It checks REST identity, the guild list,
Gateway READY and a heartbeat ACK, then closes. It does not send messages or
typing indicators. Initial identify uses `unknown`, not an online status request.

On Windows, `scripts/check-connection.ps1 -TokenFile <local-file>` loads the
credential only for that process and restores the previous environment afterward.
Keep any local credential file ignored; `.env` and `test_token*` are ignored by default.

## Optional two-account exercise

This program makes real writes only with `--run`. It requires two distinct user
credentials in a local ignored file and explicitly designated test infrastructure.
Set `DISCORD_TEST_GUILD` and `DISCORD_TEST_CATEGORY`; defaults are `library-tests`
and `integration`. The category must contain exactly one text channel.

```bash
./discord_self_account_tests /path/to/local-credentials
./discord_self_account_tests /path/to/local-credentials --run build/live-tests/new-run.json
```

The first command only discovers the target. The second sends one guild message
and DM per account, verifies reads from the recipient, fetches visible profile and
membership data, checks relationships and attempts a new request only where
appropriate. Existing friendships and pending/blocked states are preserved.
It posts the resulting report to the selected test channel.

The journal is saved before mutations; an existing journal cannot be overwritten
by a new run. Inspect failures before doing anything else. `--complete-report
<journal>` only resumes report delivery from authenticated account B; it does not
replay DMs, friend requests or test messages. A can remain unavailable during
report-only recovery. Account identities and the target are validated against the journal.

No CAPTCHA solving, account restriction bypass or automatic replay of failed
mutations is provided. The [historical findings](LIVE_TESTS.md) include actual
verification and token failures. Do not publish raw live journals or profile JSON.
