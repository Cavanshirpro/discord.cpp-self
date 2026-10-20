# Initial two-account live findings — 2026-10-20

The user explicitly authorized tests between two owned normal accounts in a
designated shared guild/category/channel. Accounts are identified here as A/B;
tokens, profile values, account IDs and message URLs are not copied into source docs.

| Test | Observed result |
| --- | --- |
| Identity and shared guild discovery | Both succeeded |
| Guild/channel details | Both succeeded; 28 members and 88 channels at test time |
| Peer guild membership | Both succeeded: nickname, roles, join time and optional membership fields |
| Guild message send | One test message from each account succeeded |
| History and single-message lookup | Both accounts verified both messages by ID/author/content |
| A → B DM | Sent and read back by B |
| B → A DM | Sent and read back by A |
| Peer profiles | Both directions succeeded |
| Bare user route | Both received 403/40001, while profile access worked |
| fetch_user fallback | Added for this specific denial; verified offline and from B to A |
| Friend request | Accounts were already friends; the request call required CAPTCHA and was not retried |
| Report delivery | One A report message succeeded, then sending returned 403/340013; later A authentication returned 401. B completed delivery; all 11 report messages were read back |

Each profile returned 17 user fields; top-level profile objects contained 15 and
16 fields respectively. Not all fields were populated. Returned information
included names/IDs, avatar/banner/bio/color fields, public flags, guild tags,
decorations/collectibles/styles, badges, premium dates, mutual guild counts and
visible connections. Ten shared guilds were visible; one profile exposed zero
connections and the other six. Null/empty values were not counted as populated data.

These results describe ordinary visibility to the requesting account. They do
not imply access to private contact details or unrestricted profile lookup.
Raw values and report links remain in the ignored local live-test journal and
the user-designated Discord channel. No friendship was removed or accepted.
No verification challenge or account restriction was bypassed, and the cause
of the later 401 was not established.

The published harness now skips an already-existing friendship, journals each
step, and supports report-only recovery without repeating test mutations.
Live edit/delete and successful creation of a new pending friend request remain
unverified. These historical live findings are not part of automated CI.
