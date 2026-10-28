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

## Optional media sender check, 2026-10-27 (working tree)

The explicitly designated account and guild voice channel were used for a
310-second camera/microphone/Go Live sender test. Local paths, IDs and the token
are omitted here. A 30.016-second H.264 video and a 297.864-second MP3 were
looped independently and encoded once at 1280x720, 30 FPS, H.264/Opus. The
video's embedded audio was excluded in favor of the separate MP3.

The final run exited normally with 9,300 camera video frames and 15,499 voice
audio packets submitted to ready encrypted connections. Go Live submitted
6,747 video frames and 11,246 audio packets; its MLS group also spent time
pending, during which that connection's frames were suppressed. Voice/camera
continued. Samples at 300, 302, 304, 306 and 308 seconds showed continued
camera, microphone and Go Live traffic after the MP3's first loop boundary.
All 154 sampled DAVE audio/video encryption failure counters were zero.

Earlier attempts exposed a Go Live connection timeout and incorrect treatment
of a pending MLS group as a failed connection. The worker now distinguishes
transport readiness from DAVE readiness, waits for encryption before sending,
clears a stale local stream at startup, and allows graceful Gateway cleanup
after an error before the parent terminates its process tree.

These are sender-side observations. Receiving/decoding/rendering and audio
playout in a second Discord client were not independently observed. Physical
microphone/camera capture was not used in this test. The separate visible demo
terminal uses the same file sources, loops indefinitely, and supports Ctrl+C
or window-close shutdown. See [media setup](MEDIA.md).

The user subsequently reported repeated receiver-side 2012 failures and frozen
low-FPS output with that earlier Node backend. Its sender counters did not
establish receiver quality. That experimental runtime has now been removed from the source project.

## Native media validation, 2026-10-27 (working tree)

The designated account joined a second user-designated guild voice channel
with `self_deaf=false`. Three other camera sources and their audio packets
were received. The native backend exposed actual Gateway viewer IDs and
RTCP receiver reports, including nonzero received RTP sequence numbers.
One live sample showed approximately 33.4 ms mean outbound video intervals
for both camera and Go Live, p95 intervals near 35 ms, and zero packet loss
in the sampled server reports. These are transport observations; they do not
establish every viewer's UI behavior or eliminate all possible 2012 causes.

The final camera capture produced three decodable H.264 MP4s, including a
remote H.265 source transcoded to H.264. Durations were 9.929, 9.999 and 10.012
seconds, within one source frame of the requested interval. FFmpeg decoded
all three with exit code zero and no error output. The mixed WAV was exactly
20 seconds at 48 kHz, stereo, 16-bit. The final audio window was quiet but
contained nonzero audio; an earlier 20-second capture contained more speech.

An earlier capture had two mux timeouts. Recording now excludes unstable
RTCP wall-clock mappings, limits FFmpeg decoder/encoder threads, disables
interactive FFmpeg stdin and preserves the private encoded input for
diagnostics. H.264 and H.265 recording/decode fixtures cover both codecs.
FFplay's removed `-ac` option was replaced with `-ch_layout stereo` for the
installed version; the corrected local output process was validated.

All source media, captures, logs, names/IDs and detailed snapshots remain in
ignored local build folders. No capture or live log was committed or uploaded.

Two remote Go Live sessions were subsequently watched successfully, one using
H.264 and the other H.265. Both delivered decrypted video frames; remote stream
audio was also observed. RTX repairs and a NACK recovery were observed in the
live receive statistics. Uncached stream discovery now uses the channel's
announced streaming voice states before detailed stream metadata is available.
Recording retains separately transmitted parameter sets and requests fresh
keyframes, so starting a recording after the first video keyframe is supported.


## C++ media follow-up (2026-10-27/28)

The final architecture uses C++20, official C++ libdave, OpenSSL and Opus with
native FFmpeg, without Node/Python/Rust processes. A real account joined
undeafened, sent file video/audio to both camera and Go Live, received an H264
camera and a remote H265 Go Live stream, and wrote two decodable approximately
ten-second MP4s plus an exact twenty-second WAV and a manifest with no mux errors.
That particular WAV was silent (only two received Opus packets); it is not
presented as a successful spoken-conversation sample. Earlier separately
validated speech and three-camera samples are preserved locally with provenance.
Initial missing-key packets were observed before the MLS group became ready;
subsequent frames decrypted and the test completed without a transport close.
Private user IDs, media, logs and live control scripts remain excluded from Git.

Offline C++ checks cover AES-GCM tamper rejection and unique nonces, RTP sequence
wrap/reordering/repair, native Opus decoding with a fixed timeline, source caching
and decoding a generated ten-second camera recording. Sending statistics do not
prove remote client playback quality or guarantee absence of receiver error 2012.

On 2026-10-28, live TUI controls were exercised for audio/video replacements
(including Unicode paths), recording start/early stop, channel changes, deaf
state and normal exit. The C++ backend recorded a camera and a watched H264
stream into approximately ten-second MP4s; both decoded without errors. Its
early twenty-second WAVs were silent. One attempt occurred with the account
deafened; later undeafened windows exposed a receive-subscription defect. The
receiver now explicitly requests mapped Opus SSRCs together with selected video
layers, including audio-only receive operation, and advertises Opus decoding.
An offline regression test covers the catch-all `any: 0` filter, own/RTX layer
exclusion, audio-only mode and disabled audio.

The subsequent C++ live capture received 1,352 audio packets and wrote an exact
20.000-second stereo WAV with RMS approximately 6,330 (16-bit samples). A camera
clip measured 9.965 seconds and a watched stream clip 10.033 seconds. All three
files decoded without errors; live transport DAVE errors were zero. A private
verification manifest contains hashes and decoder results. This supersedes the
earlier silent-audio test limitation.

A Windows high-resolution waitable timer reduced measured sender pacing from
roughly 46.8 ms at the 95th percentile to approximately 33.9 ms, with a 33.33 ms
mean (30 FPS). This measures sending, not the remote client's rendered FPS.
Static and DLL offline suites, an installed CMake consumer and publication
credential/link checks passed. Live data and the personal test application remain outside the source distribution.

## Audio control and echo follow-up, 2026-10-29

A user reported that joining the test account caused repeated conversation
audio even after using the personal TUI's deaf control. Review found that the
Gateway flag did not disable local waveOut playback or watched-stream audio.
The personal tool also enabled local monitoring alongside the regular client.
Received frames had no direct path into the outgoing file/device encoder.

The fix applies deaf state to every audio transport and the local monitor,
flushes queued output when disabled, and separates local playback from outgoing
mute. Outgoing file audio now defaults to the channel only; stream audio is an
explicit separate destination. The updated personal TUI starts local output off.

In the designated channel, voice and watched Go Live audio/video were received.
Timed deaf/playback/mute changes were checked against native snapshots. Deaf
windows did not increase received audio counters, requested playback remained
inactive while deaf, muted windows did not increase transmitted audio counters,
and the outgoing Go Live audio counter stayed zero with channel-only routing.
The user then explicitly confirmed that the echo had stopped. This is a live
confirmation for that setup, not a universal acoustic echo-cancellation claim.
Private controls, full snapshots and verification reports remain external.
