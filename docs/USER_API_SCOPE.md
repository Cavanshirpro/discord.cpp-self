# User API scope and research

Research reviewed on **2026-10-28**. The target is a C++ communication layer for a
complete Discord user client. Only capabilities available to the authenticated
user account belong here. A client application should supply its interface,
rendering and device policy while this library handles Discord REST, Gateway,
media signaling, state updates and protocol errors.

This document is a research inventory, **not implemented endpoint coverage**.
The [API reference](API.md) and [release notes](../release.md) describe shipped
features. The [roadmap](../ROADMAP.md) gives implementation order.

## Evidence and eligibility

[Discord's API reference](https://docs.discord.com/developers/intro) and
[voice specification](https://docs.discord.com/developers/topics/voice-connections)
are primary sources for shared protocol behavior. They primarily describe
applications/bots and do not establish that a user token can use every route.
[Discord Userdoccers](https://docs.discord.food/) is the community's original
research into the user API, explicitly unofficial and partly inferred. Its
findings are candidates for tests, not a guarantee of availability. The
[discord.py-self implementation](https://github.com/dolfies/discord.py-self)
provides an additional independently maintained implementation to compare.

A feature is supported only after recording its method/route or Gateway opcode,
request and response schema, event dependencies, permission/entitlement checks,
pagination/rate-limit rules, offline fixtures and validation evidence. Unknown
fields must survive parsing. Endpoint names alone never imply user access.
Account age, locale, region, experiments, subscription, guild permissions and
client/device requirements may change availability. Employee/preview privileges
are outside ordinary account coverage.

## Coverage inventory

| Client area | User-account work to cover | Current release | Research |
| --- | --- | --- | --- |
| Session and verification | Token validation, session state, logout, MFA and reauthentication handoffs | Token login and verification exception; rich handoffs planned | [Authentication](https://docs.discord.food/authentication) |
| CAPTCHA | Challenge delivery, caller-controlled completion and one explicit retry | Raises `VerificationRequired`; payload and completion API planned | [CAPTCHA research](https://docs.discord.food/topics/captcha-handling) |
| Live synchronization | READY/supplemental state, subscriptions, reconnect/resume, cache reconciliation | Core Gateway and raw dispatch work; full state/subscriptions planned | [Gateway events](https://docs.discord.food/gateway/gateway-events) |
| Text and message history | DMs/group DMs, rich received messages, replies, edits, reactions, pins, search, polls and threads | Basic messaging/history/reactions/DMs work; remaining operations planned | [Messages](https://docs.discord.food/resources/message) |
| Uploads and CDN | Multipart/cloud attachments, signed URLs, expiry, cancellation and progress | Planned | [Cloud uploads](https://docs.discord.food/topics/cloud-uploads) |
| People and presence | Profiles, friends/blocks/requests, notes, connections, activities and status | Profile/relationship reads and requests work; complete management planned | [Relationships](https://docs.discord.food/resources/relationships), [presences](https://docs.discord.food/resources/presence) |
| Guilds and membership | Roles, overwrites, members, invitations, onboarding, discovery, templates, moderation and audit logs allowed to this user | Basic guild/channel/member reads work; administration planned | [Guilds](https://docs.discord.food/resources/guild) |
| Personal organization | Settings/protobuf patches, notification overrides, folders, favorites, read acknowledgements and mentions | Planned | [Settings proto](https://docs.discord.food/resources/user-settings-proto), [read state](https://docs.discord.food/topics/read-state) |
| Calls and media | Guild and private calls, stages, cameras, Go Live, viewers, media controls and soundboard | Optional guild C++ media backend works; private calls/stages/soundboard planned | [Voice](https://docs.discord.com/developers/topics/voice-connections), [user voice research](https://docs.discord.food/topics/voice-connections) |
| Expression and events | Emoji, stickers, scheduled events and their permissions/subscriptions | Planned beyond received JSON | [User API resource index](https://docs.discord.food/) |
| Applications and activities | Directory, installation/authorization handoffs, invoking commands, buttons, selects, modals and activities as a user | Planned | [Application commands](https://docs.discord.food/interactions/application-commands) |
| Shop and inventory | Store listings/SKUs, collectibles, prices/offers, owned items, profile effects and avatar decoration | Planned | [Store](https://docs.discord.food/resources/store), [collectibles](https://docs.discord.food/resources/collectibles) |
| Nitro, gifts and subscriptions | Entitlements, subscriptions, boosts, promotions, gift inspection/redemption and purchase confirmation state | Planned | [Entitlements](https://docs.discord.food/resources/entitlement), [subscriptions](https://docs.discord.food/resources/subscription) |
| Billing and checkout | Eligible payment sources, invoices, purchase previews and server-approved purchase/cancel/confirm calls | Planned; separate optional domain | [Billing](https://docs.discord.food/resources/billing) |
| Quests and rewards | Available/claimed quests, acceptance, genuine activity progress, eligibility, completion state and reward retrieval | Planned | [Quest research](https://docs.discord.food/resources/quests), [Discord Quests FAQ](https://support.discord.com/hc/en-us/articles/22225719947543-Discord-Quests-FAQ) |
| Safety and account services | User-visible reports, safety/verification state, consent, linked services and notification delivery | Research backlog; include only ordinary user-accessible operations | [User API resource index](https://docs.discord.food/) |

The inventory deliberately groups related behavior. New Discord features enter
the relevant domain after user access is demonstrated; they do not justify a new
framework, a duplicate transport, or exposing internal/staff routes.

## CAPTCHA and verification contract

The next verification API should expose a typed challenge containing the service,
dynamic site key, service errors, session ID, request data/token, invisible flag
and unknown fields. Its default exception text and logs must omit challenge
secrets. Recognition must be transport-wide, rather than tied to friend requests
or invite endpoints. Current `VerificationRequired` does **not** yet retain these
fields; applications cannot use this proposed contract in v0.2.0.

The developer presents the provider's challenge in their application, using the
challenge data returned by Discord. The library must not own a browser/UI or
solve the test. After the user completes it, the developer explicitly supplies
the result. The library resumes the original request with the corresponding
CAPTCHA headers, preserving the session/request token and request body. Rejection,
expiration and cancellation return a new typed result; no infinite retry loop or
cross-request solution cache is appropriate. Request context should remain
opaque, bound to the original client and single-use.

Email/phone/MFA, OAuth consent and payment authentication need analogous pending
operation/completion contracts where those flows are required by a logged-in
user action. They must remain separate challenge types. Password-based account
creation, credential harvesting and token-generation services are not the goal.

## Commerce and quests boundaries

The store, inventory, entitlements, subscriptions and billing are distinct
domains. Catalog browsing must not trigger a purchase. A purchase workflow needs
an explicit amount/currency/offer preview, caller-supplied confirmation and a
result covering provider authentication, rejection and entitlement updates.
The developer owns checkout UI and any external payment-provider integration;
the library owns the Discord portion. Do not collect raw card details merely
because a Discord route refers to a payment-source token.

Quests need normal client workflows, not simulated completion: expose current
tasks and eligibility, accept when requested, report real application/player
activity, synchronize status events and retrieve rewards after server approval.
The activity source/player is supplied by the application. Preview completion,
employee reset routes, fabricated progress and spoofed activity are not normal
user-client functionality. Progress reporting remains gated on the task's actual
platform and activity requirements.

## Architectural acceptance criteria

- C++20 runtime; media remains optional. No Node, Python or Rust runtime bridges.
- One transport implementation per protocol; typed domain modules reuse shared
  authentication, errors, cancellation, rate limits and request scheduling.
- Models preserve missing versus null values, unknown enums/fields and exact IDs.
- Actions requiring extra credentials, permissions or third-party UI surface
  those requirements to the caller instead of implying token-only success.
- Raw events are an extension path, not a claim of high-level feature support.
- Each shipped domain has small examples, offline contract fixtures and explicit
  live-test limits. Personal launchers, credentials and captures stay external.
- No bot-token mode, bot-only application management, server implementation,
  UI toolkit, command-framework expansion, CAPTCHA solver or obsolete endpoint
  collection. Existing small examples may demonstrate the API without expanding
  the library's product scope.
