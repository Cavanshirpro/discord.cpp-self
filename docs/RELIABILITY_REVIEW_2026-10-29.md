# Reliability review — 2026-10-29

Scope: outgoing message requests, REST retry/error handling, native media output
lifetimes, and the separate local NOVA consumer. No live messages were sent in
this review. The reported password-reset enforcement was not reproduced.

## Confirmed defects addressed

| Finding | Change | Regression evidence |
| --- | --- | --- |
| Outgoing messages had no request identity for resolving ambiguous sends | Default nonce/enforce_nonce; caller can retain the same nonce | Payload tests and unchanged nonce across a loopback HTTP 429 retry |
| Account/CAPTCHA responses carrying HTTP 429 could enter ordinary retries | Check verification/restriction responses before rate-limit handling | Each simulated restriction causes exactly one transport invocation |
| Further requests could continue with a rejected token | Retain a 401 rejection until the credential changes | Same-token login issues no additional HTTP request |
| Exhausted 429 retries discarded the Discord error code | Retain code and original message in HttpError | Error metadata tests |
| NOVA restarted the full media session for camera/Go Live changes | Public set_outputs command changes the current transports | Native lifecycle tests and consumer tests; no live media test in this review |
| A pending stream connection could finish after the stream was disabled | Validate stream generation before attachment; wait for stop acknowledgement | Off/on and stale-generation lifecycle tests |
| A pending voice connection could attach with old output/source preferences | Apply current options when accepting a completed connection | Native code path review |
| A failed Voice constructor leaked raw DAVE session/encryptor handles | Keep temporary RAII owners until construction succeeds | Invalid-endpoint constructor test and ownership review |
| Rapid repeated media commands were silently discarded by the consumer queue | Preserve ordered media control commands | Consumer queue review |
| NOVA could keep accepting sends after account protection | Stop queued/subsequent writes, preserve draft and expose status/code | Local backend and UI restriction fixtures |

## Findings and limits still open

* A password-reset request is Discord account enforcement, not proof that one
  request was duplicated. None of these changes can establish or remove its
  server-side cause. Discord explicitly prohibits user-account automation:
  [Automated User Accounts](https://support.discord.com/hc/en-us/articles/115002192352).
  No client fingerprint spoofing or protection bypass is implemented.
* Rate-limit state is per Client. Independently created Clients/processes using
  one account do not share a scheduler. Applications must coordinate writers.
* VerificationRequired identifies a challenge but does not currently expose the
  full challenge payload or render a human verification interface.
* Nonce deduplication has a server-defined recent-message window; it is not
  durable exactly-once delivery. NOVA retains retry identity in memory, not
  across application restarts.
* Dynamic Go Live still needs Discord to deliver its stop acknowledgement and
  establish the new stream transport. It preserves voice; it cannot guarantee
  zero stream startup latency. A missing acknowledgement is visible as
  stream_stop_pending and does not trigger a reconnect loop.
* Physical devices, network-loss scenarios and remote decoder behavior after
  toggling require a separate live test. Offline state tests do not establish
  remote playback quality.

References: [message nonce semantics](https://github.com/discord/discord-api-docs/blob/main/developers/resources/message.mdx),
[error codes](https://github.com/discord/discord-api-docs/blob/main/developers/topics/opcodes-and-status-codes.mdx),
[invalid request guidance](https://github.com/discord/discord-api-docs/blob/main/developers/topics/rate-limits.mdx).

## Local validation

The native static build and installed package build succeeded on Windows/MinGW.
All three CTest suites passed: 102 unit checks, native crypto/source/recording
tests, and the HTTP/WebSocket loopback scenarios. These use synthetic data and
fake credentials. No new live send or voice/output-toggle claim is made here.

The separate NOVA consumer built against the installed static package. Its
local backend fixture checked 19 requests, including a simulated HTTP 429/40004
restriction followed by blocked message/DM attempts that never reached HTTP.
The model/editor/layout checks also preserve the draft after account protection.

## HTTP session follow-up — 2026-11-01

REST now reuses one libcurl easy handle per credential, retaining normal
connection/session caches and server-issued cookies in memory. Per-request
options are reset on success and failure. Changing the credential discards the
session; separate Clients do not share cookies. Cookie files and browser cookie
stores are not used. User-Agent versioning now follows the CMake project version.

The new localhost HTTP/1.1 `http_session` fixture verifies actual connection
reuse, cookie persistence/path scoping, POST-to-GET option reset, and isolation
between credentials and Clients. Browser/TLS fingerprint impersonation is not
implemented. This follow-up does not establish the cause of an account-security
intervention or claim that message sends are accepted by Discord.

On Windows/MinGW, all four CTest suites passed in both static and shared native
builds. The DLL check also exposed a missing direct libcurl link dependency in
the native test executable; that test target now declares it explicitly. This
does not make libcurl a new public dependency of the shared SDK.
