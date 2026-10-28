# Architecture and lifetimes

`Client` owns the REST transport, message cache, handlers and event queue.
libcurl handles HTTPS/WebSockets, nlohmann/json handles payloads, and a persistent
zlib inflater decodes the Gateway stream.

The optional `MediaSession` owns an independent account Gateway and
C++ RTP/DAVE transports. libdave, OpenSSL and Opus supply native cryptography and
audio decoding. FFmpeg processes encode/mux media, supervised with Windows jobs
or POSIX process groups. There is no Node/Python/Rust runtime. No media packets
pass through `Client`'s Gateway. See
[native media architecture and setup](MEDIA_NATIVE.md).

## Threads

`run(token)` dispatches callbacks in registration order on its calling thread.
Another thread owns Gateway I/O, heartbeat scheduling, ACK checks and reconnects.
Blocking REST calls in callbacks therefore do not block heartbeats. REST requests
through one client are serialized, including rate-limit waits.

Register handlers and commands before `run()`. Do not mutate registrations or
call `login()` concurrently with execution. `close()` works from a callback or
another thread. It signals the dispatcher and cancels REST/connection waits;
it cannot forcibly interrupt user code already running in a callback.

The network thread joins before `run()` returns. Join an externally started
`run()` before destroying the client. Never destroy it inside its callback.
Models hold weak transport references. Treat them as belonging to their client's
authentication context; do not reuse old handles after switching accounts.

## Gateway and events

The user identify uses API v9 with initial presence `unknown`, without requesting
online status. HELLO and READY/RESUMED have timeouts. Session ID and sequence are
retained for resume where appropriate. Fatal close codes stop reconnection.

Fragmented WebSocket messages and zlib-stream payloads are reassembled with
16 MiB limits. Unknown dispatch events reach `on_raw_event`; unknown opcodes
are ignored. A full event queue stops the client with an error instead of silently
dropping events. Keep callbacks short or use your own work queue.

The bounded cache uses insertion order. Partial message updates retain missing
fields and apply explicit nulls. Uncached updates expose their JSON patch without
inventing a complete message. Model values are snapshots, not live references.

## HTTP and errors

Requests have timeouts and bounded response sizes. Redirects are not followed;
TLS verification uses libcurl's trust configuration. A custom endpoint receives
the credential, so configure one only deliberately.

429 responses establish route/shared-bucket/global deadlines before retry
decisions. Reauthenticating with the same credential retains those deadlines.
A successful mutation remains successful even if its response establishes a long
future wait. Network errors and 5xx responses are not replayed automatically;
delivery may already have happened. Limits are per client, not coordinated across processes.

CAPTCHA responses raise `VerificationRequired`; challenges are not solved or
replayed. Callback exceptions go to `on_error` when registered; otherwise they
unwind `run()` after shutdown. A throwing error handler also unwinds it.
Terminal network/protocol failures always reach the caller.
