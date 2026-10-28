# API reference — 0.2.0

Include `<discord/discord.hpp>`. Public names live in namespace `discord`.
`Json` is an alias for `nlohmann::json`.

The optional `MediaOptions` and `MediaSession` API is described in the
[native media guide](MEDIA_NATIVE.md), including snapshot/watch/deaf/record
controls plus source/channel changes. It uses C++ RTP/DAVE, native libraries and FFmpeg;
it does not reuse `Client`'s Gateway connection.

## Client lifecycle

```cpp
discord::ClientOptions options;
options.message_cache_size = 500;
discord::Client client(options);
client.login(discord::token_from_env()); // REST only; no network request yet.
auto self = client.fetch_me();
```

`run(token)` opens the Gateway and blocks while dispatching callbacks. Register
handlers first. `close()` requests shutdown; `is_running()` reports an active
`run()` call. Repeated sequential `run()` calls are supported and reset event/cache
state. `user()` returns an optional identity after READY. `latency()` is the latest
heartbeat round-trip in milliseconds, or `-1ms` before an ACK/on reconnection.
Clients are neither copyable nor movable.

### Options

| Field | Default | Meaning |
| --- | --- | --- |
| `api_url` | `https://discord.com/api/v9` | REST base URL |
| `gateway_url` | `wss://gateway.discord.gg/` | Initial Gateway URL |
| `request_timeout` | `30000ms` | HTTP/connect and handshake deadlines |
| `max_rate_limit_wait` | `60000ms` | Maximum automatic rate-limit wait |
| `message_cache_size` | `1000` | Cached messages; zero disables the cache |
| `event_queue_size` | `4096` | Maximum queued dispatch events |
| `max_reconnect_attempts` | `8` | Reconnect budget between established sessions |
| `max_rate_limit_retries` | `5` | Automatic retries following a 429 |

Negative limits, zero timeouts and a zero event queue are rejected. A disconnect
after READY/RESUMED resets the consecutive-failure budget.

## REST operations

| Method | Result / behavior |
| --- | --- |
| `fetch_me()` | `User` for the authenticated account |
| `fetch_user(id)` | `User`; on 403/40001, tries the visible profile endpoint |
| `fetch_profile(id, optional_guild_id)` | `UserProfile`, including extended JSON |
| `fetch_guilds()` | User account's `vector<Guild>` |
| `fetch_guild(id)` | Guild details and optional approximate counts |
| `fetch_channels(guild_id)` | `vector<Channel>` with network methods attached |
| `fetch_channel(id)` | One populated `Channel` |
| `channel(id)` | A handle with no network request |
| `fetch_member(guild_id, user_id)` | `Member` with roles and join information |
| `fetch_relationships()` | `vector<Relationship>` visible to this account |
| `send_friend_request(user_id)` | User relationship PUT; may require verification |
| `create_dm(recipient_id)` | `Channel`; uses the user API's recipients array |
| `send(channel_id, text_or_options)` | New `Message` |
| `request(method, relative_path, body=nullptr)` | JSON escape hatch with the same transport and limits |

The escape hatch accepts GET, POST, PATCH, PUT and DELETE with paths beginning in
one slash. It rejects absolute URLs, control characters and parent traversal.
Use percent encoding for user-defined path components. It is not multipart support
and does not guarantee that arbitrary Discord endpoints work with user tokens.

Friend requests are real writes. Check existing relationships first: the server's
relationship API can behave differently for existing/pending relationships. The
manual test harness preserves friendships and pending/blocked states.

## Models

| Type | Main fields |
| --- | --- |
| `Snowflake` | Exact uint64 ID; `value()`, `str()`, `created_at()` and comparisons |
| `User` | `id`, `name`, `display_name`, `avatar`, `bot`, `discriminator`, `public_flags`, `banner`, `bio`, `accent_color` |
| `Guild` | `id`, `name`, `icon`, `unavailable`, optional owner/description/counts, features and premium tier |
| `Member` | `user`, optional nickname/avatar, roles, `joined_at`, premium date, pending and flags |
| `UserProfile` | `user`, optional bio/pronouns/premium date/friend count, mutual guild IDs, original `data` JSON |
| `Relationship` | `id`, type, optional user and nickname |
| `Channel` | `id`, optional guild/parent/topic, name/type, recipients, position and NSFW |
| `Message` | IDs, author, content, timestamp/edit timestamp, attachments, pinned and TTS |
| `Attachment` | ID, filename, URL and byte size; uploads are not implemented |

String IDs require `Snowflake{"123456789012345678"}`. Integer IDs can use
`Snowflake{123456789012345678ULL}`. JSON serialization uses decimal strings and
never rounds through floating point. `created_at()` returns a UTC time point.
`User::mention()` formats an ID mention; `avatar_url(size=256)` validates a
power-of-two size from 16 through 4096.

Profile fields depend on account visibility. Null/empty/absent values must not be
treated as evidence that private information is available. `UserProfile.data`
preserves returned extension fields, so avoid logging it indiscriminately.

Relationship types: `none=0`, `friend_=1`, `blocked=2`, `incoming_request=3`,
`outgoing_request=4`, `implicit=5`. Unknown numeric values remain representable.

## Channels and messages

```cpp
auto channel = client.channel(channel_id);
auto sent = channel.send("Hello");
auto reply = sent.reply("Reply");
auto edited = sent.edit("Updated");
sent.react("emoji_name:123456789012345678");
sent.unreact("emoji_name:123456789012345678");
auto recent = channel.history(25);
auto exact = channel.fetch_message(sent.id);
```

`history(limit=50, before={})` accepts 1–100 and returns newest first.
`fetch_message()` uses history with `around=id` and validates the returned ID;
missing or neighboring messages produce HTTP 404/code 10008. `typing()` sends a
typing indicator. `Message::remove()` deletes the message. `channel()` creates
a bound handle; `jump_url()` formats a guild/DM message URL.

`MessageOptions` contains `content`, `allow_mentions=false`,
`mention_author=false` and optional `reply_to`. Empty new message text is rejected.
Custom embeds are intentionally absent. A URL's server-generated preview is a
separate Discord behavior. `reply(options)` sets the reference to that message.

## Events

Multiple handlers are supported for all events below. Each runs serially on the
thread that called `run()`.

| Registration | Payload |
| --- | --- |
| `on_ready` | `Ready { user, guilds }` |
| `on_message` | Complete `Message` |
| `on_message_edit` | `MessageUpdate { id, channel_id, before, after, patch }` |
| `on_message_delete` | `MessageDelete { id, channel_id, cached }` |
| `on_resumed` | No arguments |
| `on_raw_event` | `RawEvent { name, data }`, including unknown dispatch names |
| `on_error` | One callback exception handler receiving `const std::exception&` |

Update `before`/`after` are populated only when the original message is cached.
The raw `patch` is always available. Bulk deletes produce one delete event per ID.
`cached_message(id)` returns an optional snapshot. READY starts a fresh cache;
RESUMED continues the current session.

## Commands and errors

`Commands(client, prefix="!").add(name, handler)` registers a command. Call
`process(message)` from an `on_message` handler; it returns whether a registered
command ran. Only the logged-in user's messages qualify. `names()` returns names
in sorted order. Duplicate/invalid names and unfinished argument quotes are errors.
`Context` provides the client, message, name, raw arguments, parsed args, `send()`
and `reply()`. Do not retain a context's message reference past its callback.

All library exceptions derive from `Error`/`std::runtime_error`:

- `HttpError`: numeric `status` and Discord `code`.
- `VerificationRequired`: an `HttpError` subtype for CAPTCHA responses; no solver/replay.
- `GatewayError`: numeric close `code`.
- `TransportError`, `ProtocolError`, `Cancelled`: network, payload and shutdown failures.

Invalid API arguments can throw `std::invalid_argument`; malformed model JSON can
throw nlohmann/json exceptions. Do not log credentials when handling errors.
