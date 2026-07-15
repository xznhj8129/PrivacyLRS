# PrivacyLRS

PrivacyLRS is a privacy-protecting fork of [ExpressLRS (ELRS)](https://www.expresslrs.org/), the long-range RC link. It protects RC commands, CRSF telemetry, and MAVLink telemetry—including GPS location—from passive RF observation.

## Release identity

This README documents the current **PrivacyLRS development checkpoint** on branch `secure_4.0.1`, based on **ExpressLRS 4.0.1**. It is not a release: there are no independent release tags, no PrivacyLRS version field in the firmware, and known open defects are listed below. It is not over-the-air compatible with earlier PrivacyLRS builds: reflash both TX and RX together.

The repository inherits ExpressLRS tags and the firmware identifies itself with the upstream ELRS version. A “PrivacyLRS 1.0” identity should only be claimed once a tagged build passes the priority test matrix.


## Current implementation status

This document matches `secure_4.0.1` as of 2026-07-15, which adds three hardware-validated fixes on top of commit `2ed936e8` (`checkpoint`): band changes preserve the current packet rate under encryption instead of selecting the fastest destination rate, the TX proposal retry timeout is fast and rate-aware instead of borrowing the 10-second loss grace, and an RX provisional session that never sees an encrypted packet expires within about a second.

Hardware validation used a RadioMaster Nomad X-Band TX and RadioMaster XR1 RX with a 1:2 telemetry ratio. The 2026-07-15 sweep passed every step: initial encrypted RC on 2.4 GHz at 150 Hz in 8.0 s from a cold disconnect, 150 to 250 Hz in 2.6 s, 2.4 GHz to 915 MHz in 4.3 s, 915 MHz back to 2.4 GHz in 1.6 s, and 250 back to 150 Hz in 1.6 s, all with 100 % measured RC delivery and no dropouts.

This is development firmware. K1000 establishes an encrypted session on this Nomad/XR1 pair but does not sustain full packet delivery (see the test matrix), and full re-establishment takes seconds rather than being seamless.

## Purpose and security model

The goal is privacy: someone who records RF traffic but does not know the configured secret should not be able to read RC command contents or telemetry payloads, including GPS position. A passive observer can still see that a link exists: RF activity, transmission timing and cadence, plaintext acquisition SYNCs, and the selected band and rate remain observable. PrivacyLRS preserves ELRS OTA frame sizes, nominal steady-state packet rates, and CRSF purpose; it changes the FHSS sequence, session acquisition and recovery behavior, and re-establishment latency. It is not intended to be an electronic-attack-resistant or low-probability-of-intercept control link.

Use a generated secret or a high-entropy Diceware-style binding phrase. A memorable or predictable phrase permits offline guessing against recorded session setup traffic.

## Crypto

### Protocol states

TX and RX each maintain a separate crypto state. The state is not the same as the ELRS radio connection state or the handset's telemetry indicator.

| State | TX behavior | RX behavior |
| --- | --- | --- |
| `NONE` | Sends plaintext marked recovery SYNC packets and may send only the public session-nonce proposal. It sends no RC or application data. | Accepts valid plaintext SYNC and session-nonce proposal fragments. It rejects plaintext RC and application data. |
| `PROPOSED` | Reliably sends or finishes the nonce proposal, then transmits a 16-SYNC plaintext activation barrier before enabling encrypted traffic. | Has derived and installed the proposed key provisionally, acknowledges proposal fragments in plaintext, follows marked SYNC anchors, and accepts no application traffic until an encrypted uplink proves the session. |
| `FULL` | Encrypts RC and application uplink packets. Plaintext periodic SYNC remains available for acquisition and recovery. | Decrypts uplink, delivers valid RC/application data, and encrypts normal downlink telemetry. |

The configured binding phrase is hashed in full with SHA-256 to form the 256-bit long-term secret. TX and RX must be built with the same secret. The secret is never sent by ELRS binding. It is used to derive session keys and the private FHSS map.

### Exact initial binding and session sequence

There are two distinct procedures: stock ELRS binding establishes the ELRS UID, then PrivacyLRS session establishment enables private RC and telemetry. Completing the first does not imply that the second completed.

#### A. Startup with an already matching UID and secret

No explicit ELRS binding exchange is required when both devices already contain the matching UID and long-term secret.

1. TX starts on its configured normal RF mode. RX starts on its saved mode or scans modes using the existing ELRS acquisition machinery.
2. Both crypto states start at `NONE`. No plaintext RC, CRSF telemetry, MAVLink, Airport data, or other application data is permitted.
3. TX sends a plaintext SYNC with `cryptoResync=1`. The SYNC carries the ordinary ELRS acquisition fields: OTA nonce, FHSS index, RF rate, switch mode, telemetry ratio, Gemini mode, OTA protocol, and UID/model-match fields.
4. Immediately before transmitting that marked SYNC, TX sets the shared crypto slot to the SYNC's eight-bit ELRS OTA nonce. The anchor is counted only if the packet reaches the radio transmit path; an LBT-denied packet is not treated as sent.
5. RX validates the normal ELRS UID/model-match fields and OTA CRC. It learns the advertised ELRS mode and aligns its OTA nonce, FHSS index, timer, and crypto slot. A marked recovery SYNC does not itself require a downlink reply.
6. TX does not require that first downlink to begin. Once a marked anchor SYNC has actually been transmitted and the reliable uplink sender is free, TX generates a fresh public 64-bit session nonce. Entropy combines radio RSSI noise with the SoC hardware random-number generator.
7. TX derives the 256-bit session key locally by running ChaCha20 with the long-term secret, the public session nonce, and the dedicated `PLRSKDF1` counter domain. It initializes its provisional traffic streams, places `MSP_ELRS_INIT_ENCRYPT` plus the eight-byte nonce into the existing ELRS reliable uplink-data sender, then enters `PROPOSED`. The complete proposal is nine bytes including the opcode; no session key is transmitted.
8. TX sends the nonce proposal as ordinary reliable DATA fragments. These fragments are not RC or application data. Between proposal fragments, TX continues sending marked SYNC packets; every received marked SYNC re-anchors RX without discarding proposal fragments already received.
9. RX accepts proposal fragments while the ELRS connection is tentative or connected. Each fragment opens a crypto-only return slot. RX replies with plaintext LinkStats containing the reliable-transfer acknowledgement and no application telemetry.
10. When the complete proposal is assembled, RX derives the same session key from its copy of the long-term secret and the received nonce. It initializes its three provisional streams—uplink, radio-1 downlink, and radio-2 downlink—and enters `PROPOSED`. RX does not acknowledge the final fragment until derivation and cipher initialization have completed successfully.
11. TX retransmits any proposal fragment whose acknowledgement is lost. TX remains `PROPOSED` until the complete reliable payload is acknowledged.
12. The final proposal acknowledgement carries the RX's current 16-bit crypto-slot anchor in the first two LinkStats bytes. TX aligns its provisional slot to that value and starts a 16-packet activation barrier.
13. While the barrier is active, every non-proposal uplink packet is a marked plaintext SYNC. Each actually transmitted SYNC anchors TX to its current eight-bit OTA nonce; each received marked SYNC re-anchors RX while preserving the provisional key. These barrier SYNC packets are repeated for delivery probability and are not individually acknowledged.
14. After all 16 barrier SYNC packets have been transmitted and the reliable proposal sender is idle, TX enters `FULL` and sends its first encrypted uplink. RX tests its current receive offset and one adjacent slot on either side. A valid decrypted packet changes RX from `PROPOSED` to `FULL`; an RC packet is delivered only after this succeeds.
15. Once RX is `FULL`, normal downlink LinkStats and application telemetry are encrypted. The handset telemetry indicator may therefore appear briefly during plaintext proposal acknowledgements even though encrypted RC has not yet become active.

#### B. Explicit ELRS binding

Explicit binding is intentionally kept on the stock ELRS 4.0.1 path. It proves that the binding radios can exchange the UID; it does not negotiate a PrivacyLRS session and does not exchange or verify the long-term secret.

1. While binding mode is active, both packet encryption/decryption and crypto-control slot scheduling are bypassed. Any old crypto state is inert and cannot change the stock binding RF timing.
2. TX stops its normal timer, queues the stock `MSP_ELRS_BIND` payload containing UID bytes 2 through 5, sets the OTA CRC initializer to `OTA_VERSION_ID`, fixes the OTA nonce at zero, and enters the standard 50 Hz/inverted-IQ binding mode.
3. On LR1121 hardware, TX sends the first half of its binding repetitions on the 900 MHz binding mode and the second half on the 2.4 GHz binding mode. An LR1121 RX in binding mode alternates between those modes every 125 ms. The packet-rate selection shown before pressing Bind does not alter this binding exchange.
4. Binding DATA is neither encrypted nor passed through the PrivacyLRS session state machine. TX repeats the UID payload using the stock ELRS reliable sender.
5. RX accepts the binding payload, stores the four received UID bytes with the two leading UID bytes cleared as ELRS specifies, and exits binding mode after the configuration changes.
6. RX commits the UID, restores the UID-derived OTA CRC initializer, initializes the private FHSS map, clears the old session state, and returns to normal acquisition in crypto state `NONE`.
7. TX exits after its binding repetitions, restores the UID-derived OTA CRC initializer, clears the old session state, and returns to the configured normal RF mode in crypto state `NONE`.
8. The separate PrivacyLRS session sequence in section A now begins at step 3. If RX exits its binding indication but RC never starts, the UID exchange succeeded and the failure is in normal-mode acquisition or crypto-session establishment—not in the stock bind packet exchange.

Because the secure FHSS map and session-key derivation use the compiled long-term secret, explicit ELRS binding cannot make mismatched builds communicate after binding. Both devices still require the same secret, compatible regulatory configuration, and compatible PrivacyLRS firmware.

### Implementation source anchors

This sequence documents the current implementation, not a separate aspirational protocol. When the implementation changes, update this sequence and `DEVLOG.md` in the same change. The primary source locations are:

- `src/include/encryption.h`: crypto states, session proposal layout, and ten-second short-loss constant.
- `src/src/tx_main.cpp`: `EnterBindingMode()`, `ExitBindingMode()`, `GenerateSyncPacketData()`, `SendRCdataToRF()`, `ProcessDownlinkPacket()`, `SetSyncSpam()`, and the TX `loop()` session transitions.
- `src/src/rx_main.cpp`: `EnterBindingMode()`, `ExitBindingMode()`, `ProcessRfPacket_SYNC()`, `ProcessRFPacket()`, `ProcessRfPacket_DataUl()`, `HandleSendDataDl()`, `DataUlReceiveComplete()`, and the RX `loop()` expiry path.
- `src/src/common.cpp`: slot-derived ChaCha20 counters, direction/radio nonce domains, narrow decrypt window, and session-cipher initialization.
- `src/lib/FHSS/`: long-term-secret-derived FHSS generation and domain separation.
- `src/lib/OTA/`: ELRS packet serialization and UID/OTA-nonce-bound CRC behavior used to validate plaintext acquisition and decrypted packets.

### Cryptographic frequency hopping

The hop sequence is a keyed ChaCha20 CSPRNG output, derived from the long-term secret with a dedicated FHSS nonce domain. Primary and dual-band sequences have different domain values. Each frequency block is a Fisher-Yates permutation: every regulatory-domain channel occurs once, while the existing sync-channel position remains fixed at the start of each block.

This hides the non-sync hop schedule from a passive observer while preserving ELRS acquisition and recovery behavior. The hop map is deliberately stable across session rekeys and resets; using the ephemeral session key here would make cold recovery depend on a fragile schedule-transition protocol.

### Normal traffic

RC/uplink and telemetry/downlink use independent ChaCha20 streams. Downlink has one independently domain-separated stream per physical radio. All streams use a shared 64-bit RF-slot number with separate nonce domains:

- Uplink uses the session nonce with one domain value; each physical downlink radio uses a different domain value.
- The RF-slot number advances from the existing ELRS packet timer every interval, including a lost, skipped, or LBT-denied transmission.
- A receiver tries its current persistent receive offset, then one adjacent slot below and above it. A successful adjacent candidate updates the receive offset. It never performs a broad counter search.

This prevents loss in either direction from desynchronizing the other direction or causing a later packet to reuse its ChaCha20 keystream. The OTA CRC remains in the encrypted packet: it detects accidental corruption and identifies a plausible slot in the narrow recovery window, but it is not a cryptographic authenticator.

### Exact normal-link and recovery sequence

#### Normal `FULL` operation

1. Non-SYNC uplink packets—including RC, CRSF, MAVLink, and Airport data—are encrypted with the uplink stream.
2. Normal downlink LinkStats and application telemetry are encrypted with the stream for the physical radio that transmits them. Radio 1 and radio 2 have separate nonce domains.
3. The shared 64-bit crypto slot advances on every ELRS RF timer interval on both devices, including an RF packet loss, a telemetry receive interval, a skipped transmission, or an LBT-denied transmission.
4. Periodic ELRS SYNC remains plaintext because a receiver that rebooted cannot decrypt a discovery packet. In `FULL`, TX sends it with `cryptoResync=0`; it contains no RC or application payload and opens exactly one crypto-control return slot.
5. A live `FULL` RX answers that discovery slot with encrypted LinkStats and retains the session. A random plaintext packet outside the immediately associated control window is not accepted by TX as a recovery request.
6. A missing packet or failed decrypt is dropped. The decryptor tests the current persistent receive offset, then `-1` and `+1` relative to that offset. The offset changes only after a packet decrypts and passes the normal OTA type and CRC checks.

#### Downlink-only telemetry loss

1. TX may report telemetry lost because it stops receiving encrypted downlink packets.
2. TX cannot infer from that event that RX stopped receiving uplink, so TX remains `FULL` and continues transmitting encrypted RC.
3. RX remains `FULL` as long as it continues decrypting uplink. Loss of the downlink alone does not rekey, stop RC, or permit plaintext data.
4. When downlink reception returns, the per-radio downlink stream uses the current shared slot. A missing downlink packet does not advance or desynchronize the uplink stream.

#### Uplink-only or simultaneous loss shorter than ten seconds

1. RX delivers no missing or invalid RC packet. The flight controller sees the normal ELRS loss/failsafe behavior for those intervals.
2. RX retains its provisional/full session, timer, FHSS position, and crypto slot for up to `CRYPTO_SHORT_LOSS_GRACE_MS`, currently 10,000 ms, measured from the last successfully decrypted uplink.
3. TX retains its session and continues its timer. If only its downlink is missing, it continues encrypted RC as described above.
4. When RF returns, RX accepts traffic immediately only if the packet decrypts at the current receive offset or one adjacent slot and the OTA type and CRC are valid. A valid packet refreshes the ten-second timer. There is no large CRC-guided counter search.
5. If the clocks have moved outside that narrow window, packets remain rejected until full recovery occurs. A plaintext periodic SYNC does not falsely refresh the last-encrypted-uplink timer.

#### RX reboot or ten-second uplink expiry while TX remains running

1. A rebooted RX starts at `NONE`. An RX whose last valid encrypted uplink becomes older than ten seconds clears its session and reliable-transfer state, unlocks the configured first-connection RF-mode lock, and returns to ELRS acquisition/scanning.
2. TX may still be `FULL` because it cannot distinguish an RX failure from downlink-only loss. It continues encrypted uplink and periodically sends plaintext discovery SYNC with `cryptoResync=0`.
3. When the `NONE` RX hears a valid discovery SYNC, it aligns ordinary ELRS acquisition state and sends plaintext LinkStats in that SYNC's reserved crypto-control return slot.
4. A `FULL` TX accepts that plaintext LinkStats only in the expected return window. It then changes to `NONE`, clears application/reliable-transfer state, and schedules marked recovery SYNC packets.
5. TX and RX perform the complete session sequence from section A, steps 3 through 15. No old key is resumed after the ten-second expiry.
6. If RX cannot hear the TX's current RF mode, its unlocked ELRS scanner cycles supported rates/bands. This is acquisition scanning, not a simultaneous cross-band handshake, and it does not guarantee a seamless live band transition.

#### TX reboot while RX retains an old session

1. TX restarts at `NONE` with no old session key and sends marked plaintext SYNC packets with `cryptoResync=1`.
2. A `FULL` RX validates the marked SYNC using the UID-derived OTA CRC, discards its old session and reliable-transfer state, resets to `NONE`, and anchors to the advertised slot.
3. Both sides perform the complete fresh session sequence. Old encrypted RC or telemetry is never sent as plaintext during the transition.

#### Packet-rate, RF-mode, or configuration change

1. When TX accepts a link configuration change, it immediately leaves `FULL`, clears the old session's reliable-transfer state, and sets a configuration-transition barrier. Plaintext RC/application data remains forbidden.
2. TX sends the stock ELRS pre-change SYNC spam needed to announce the new settings. The transition barrier prevents an acknowledgement or slot anchor received on the old mode from starting a proposal.
3. TX commits the configuration and reconfigures the radio. It discards all old-mode anchor results, clears the barrier, and requires a marked SYNC actually transmitted on the new mode.
4. RX applies a supported advertised rate through the existing ELRS rate-change path. If it misses the transition, it may need to lose the old connection and reacquire by scanning.
5. Once both radios meet on the new mode, they perform a complete fresh session proposal and 16-SYNC activation barrier. There is no key or slot continuity across a deliberate rate/mode change.
6. Live cross-band recovery still depends on the underlying ELRS transition/acquisition behavior. PrivacyLRS does not add a parallel old-band/new-band negotiation channel.

At every recovery boundary, the allowed plaintext is limited to ELRS binding packets, acquisition/recovery SYNC, the public session-nonce proposal, and crypto-control LinkStats acknowledgements. The session key is never transmitted. TX begins encrypted RC and application traffic after the proposal acknowledgement and after transmitting the unacknowledged 16-SYNC activation barrier; RX delivers application traffic only after it successfully decrypts that traffic. A provisional RX session that never receives a decryptable packet expires within about a second and waits for a fresh proposal.

### Interpreting visible symptoms

- RX leaves binding mode, then reports no link: the stock UID exchange succeeded; normal-mode RF acquisition or session establishment failed afterward.
- Handset briefly receives telemetry but RX delivers no RC: plaintext crypto-control LinkStats may be crossing while one or both sides are still `NONE`/`PROPOSED`; this is not proof that the encrypted session reached `FULL`.
- Handset reports telemetry lost while RC continues: this can be a downlink-only loss and does not by itself reset the session.
- Both sides work again after returning to the previous packet rate or band: the encrypted application path is healthy on that mode, but the ELRS mode transition or new-mode session establishment did not complete.


### Band-change rate selection

Stock ELRS 4.0.1 selects the fastest supported packet rate when the LR1121 RF band changes. Under `USE_ENCRYPTION` this fork instead preserves the current nominal rate when the destination band offers it, and otherwise falls back to a conservative transition rate of at most 250 Hz; non-encrypted builds keep stock behavior. Before this fix, returning from 915 MHz to 2.4 GHz silently selected K1000, which established an encrypted session but could not sustain full packet delivery. High-rate modes such as K1000 remain explicit user selections.

### Deliberate compromises

- Packets are encrypted but not cryptographically authenticated. CRC is error detection, not a MAC.
- An active attacker can jam, disrupt, replay, or potentially manipulate traffic. Recovery SYNC is an acquisition signal, not authenticated control.
- There is no forward secrecy: compromise of the long-term secret can expose recorded session setup and traffic.
- The fixed ELRS OTA frame has no unused space for a conventional per-packet AEAD tag at the current payload capacity. Authentication is achievable in principle by reducing payload, repurposing fields, or authenticating groups of packets, but any of those changes OTA semantics; this checkpoint deliberately does none of them.

These are intentional boundaries. PrivacyLRS is for passive-observer privacy, not resistance to an active RF adversary.

## Changes within this checkpoint

On top of the reviewed `2ed936e8` checkpoint (2026-07-15, all hardware-validated):

- Band changes preserve the current packet rate under encryption (stock selected the fastest destination rate, K1000).
- The TX proposal retry timeout is rate-aware (160 slots, 2 s floor) instead of the 10-second loss grace; a wedged first attempt now costs about 2 s instead of 10 s.
- An RX provisional session that never decrypts a packet expires after 64 slots (1 s floor), so a fresh proposal always lands on a clean receiver state.
- The bench status-field diagnostics require an explicit `CRYPTO_BENCH_DIAGNOSTICS` build flag; release builds keep stock `pktsBad`/`pktsGood` meanings.

## Improvements from ELRS

The intentional divergence from ExpressLRS 4.0.1 is the crypto layer described above:

- 256-bit ChaCha20 session encryption for normal RC and telemetry traffic.
- A 256-bit long-term key derived from the configured secret, fresh nonce-derived session keys, and hardware/RSSI nonce entropy.
- ChaCha20-keyed FHSS permutations with separate primary/dual-band domains and the existing fixed sync-channel structure.
- Independent uplink and per-radio downlink nonce domains driven by a shared RF-slot clock.
- Encrypted-session recovery paths that keep RC and application telemetry blocked until the new session becomes active.

Apart from that layer, PrivacyLRS deliberately carries ELRS 4.0.1 behavior forward. There are no additional flight features, radio-mode changes, CRSF extensions, or live cross-band-handoff changes in this fork.

## Compatibility and testing

Build and configure TX and RX with the same secret. Their cryptographic hop maps make this build intentionally incompatible with stock ELRS or a PrivacyLRS device built with another secret. Use a carefully configured flight-controller failsafe while testing: a firmware defect or any RF outage can still cause loss of control.

The priority test matrix remains recovery after single and burst packet loss, one-sided reset, telemetry-slot loss, packet-rate change, band change, and prolonged outage. Verify that RC and application telemetry remain absent until the encrypted session is active. Earlier native encryption and FHSS suites reached 30 passing tests; the 2026-07-15 fixes were validated end-to-end on hardware rather than by rerunning the native suite.

Hardware sweep results (2.4 GHz + 915 MHz, `test_rates = none`, 1:2 telemetry, Nomad/XR1):

| Test | 2026-07-14 checkpoint | 2026-07-15 with fixes |
| --- | --- | --- |
| Initial 2.4 GHz, `150Hz(-112dBm)` | Pass, 21.0 s | Pass, 8.0 s (from cold disconnect, includes RX scan cycle) |
| 2.4 GHz, 150 Hz to `250Hz(-108dBm)` | Pass, 11.6 s | Pass, 2.6 s |
| 2.4 GHz 250 Hz to 915 MHz `250Hz(-111dBm)` | Pass, 12.3 s | Pass, 4.3 s |
| 915 MHz 250 Hz to 2.4 GHz | Fail (selected K1000) | Pass, 1.6 s (rate preserved at 250 Hz) |
| 2.4 GHz 250 Hz to `150Hz(-112dBm)` | Pass, 13.6 s | Pass, 1.6 s |

All 2026-07-15 measurements delivered 100 % of expected RC frames with no RC or link dropout events.

On the 2026-07-14 K1000 failure, the diagnostics showed the RX crypto state reached `FULL` with 195 CRC-valid decrypted RC frames per second against 500 expected and zero RX-UART CRC errors. Session establishment succeeds at K1000; sustained packet delivery does not keep up. The suspected bottleneck is on the test-rig side and is a separate open investigation; with rate preservation, K1000 can no longer be selected implicitly by a band change.

## Building and configuring

Use ExpressLRS Configurator with the **Local** source option and select this repository's `src` directory. Choose the correct TX/RX targets, regulatory domain, and matching secret before flashing both devices.

### Lua script when building locally

For a Local source, Configurator's **Download Lua** action opens the download menu but does not save the Lua file. Copy [elrs.lua](src/lua/elrs.lua) to `SCRIPTS/TOOLS` on the handset manually.

## About ExpressLRS

PrivacyLRS is based on ExpressLRS 4.0.1. ExpressLRS supplies the radio link, target support, packet formats, telemetry transport, and configuration system; PrivacyLRS is grateful to its maintainers and contributors.
