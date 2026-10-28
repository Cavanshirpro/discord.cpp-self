# Roadmap

The goal is a C++20 communication layer from which a developer can build a
complete Discord **user client**. The library owns Discord REST, Gateway,
voice/video transport and protocol state. The application owns its interface,
rendering, device integration and user decisions. Only APIs usable by the
authenticated user account belong in this project.

The [research inventory](docs/USER_API_SCOPE.md) records the scope, sources and
eligibility limits. A planned item is not an implemented API or a promise that
Discord will expose it to every account. Features unavailable to ordinary users
remain outside coverage even when an internal endpoint exists.

## Shipped foundation: 0.1 and 0.2

- [x] C++20 static/shared library, installed CMake package, Conan/vcpkg recipes.
- [x] User-token authentication, bounded HTTP retries and route/global rate limits.
- [x] Gateway heartbeat, reconnect/resume, compression and raw dispatch events.
- [x] Basic messages, replies, edits, deletion, reactions, history and DMs.
- [x] Basic users, profiles, guilds, channels, members and relationships.
- [x] Optional C++ DAVE/RTP media: guild voice, camera, file audio and Go Live.
- [x] Receive/listen/record remote speech, cameras and watched Go Live streams.
- [x] Source/channel controls, participant/viewer snapshots and transport metrics.
- [x] Offline HTTP/Gateway/media tests and anonymized live validation records.

## Next: transport and ordinary client state

- [ ] Typed CAPTCHA challenge data and explicit caller-supplied completion retry.
  The developer displays the test; the library transports the challenge/result.
- [ ] Structured API errors and pending verification/MFA operations, preserving
  unknown fields without leaking secrets into exception text or logs.
- [ ] Attachments/cloud uploads with progress, cancellation and signed-URL expiry.
- [ ] Message search, pins, forwarding, polls and full thread/forum operations.
- [ ] Read state, acknowledgements, mention counts and notification settings.
- [ ] Presence/status updates; complete friend/block/request and group-DM actions.
- [ ] READY/supplemental state, guild subscriptions, member lists and cache patches.
- [ ] A shared cancellable REST scheduler and asynchronous adapter where needed
  by client workloads; preserve the small synchronous entry points.

## Complete client domains

- [ ] User/profile editing, notes, connections and settings/protobuf patches.
- [ ] Guild/channel creation and editing, roles, permission overwrites, members,
  invitations, onboarding, discovery, moderation and audit logs as permitted.
- [ ] Emoji, stickers, scheduled events, soundboard, stage and private/group calls.
- [ ] User-side application discovery, installation/consent, slash/context commands,
  component interactions, modals and activity session communication.
- [ ] User-visible safety/reporting, consent, account/session services and push
  notification protocol hooks when accessible to an ordinary user.
- [ ] Protocol coverage for remaining user-facing resources after research and
  fixture validation, rather than mirroring staff/bot endpoint catalogs.

## Shop, subscriptions and quests

- [ ] Shop/store catalogs, SKUs, collectibles, prices/offers and owned inventory.
- [ ] Profile effects/avatar decorations, entitlements, Nitro, boosts, gifts,
  subscriptions and promotions available to the account.
- [ ] Discord checkout/payment-source/invoice operations with explicit purchase
  previews, caller confirmation and external authentication handoffs.
- [ ] Quest discovery, acceptance, eligibility, real activity/video progress,
  status events, approved reward claims and redemption-code retrieval.
- [ ] Preserve account/region/platform/entitlement requirements in typed outcomes.
  No fabricated quest progress, previewer/staff completion endpoints or invented
  eligibility fallbacks.

## Completion criteria and boundaries

Every domain needs a documented request/event contract, permission requirements,
pagination, rate-limit and cancellation behavior, unknown-field preservation,
offline regression fixtures and explicit live-test evidence or limitations.
Maintain a coverage matrix so a raw JSON escape hatch is never presented as
finished support. Add platforms/codecs through measured compatibility work;
MSVC/macOS media and sustained remote playback quality still need validation.

Keep protocol concerns in reusable C++ domain modules. Do not grow a bot library,
UI toolkit, large command framework, telemetry platform, CAPTCHA solver or
generic automation framework here. Personal test interfaces, recordings and
account data are separate from the published library. C++ runtime independence
and clear ownership/lifetimes remain release requirements.
