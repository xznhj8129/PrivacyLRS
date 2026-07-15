# PrivacyLRS development log

Timestamps use America/Toronto local time (EDT). This is a working record for the PrivacyLRS 4.0.1 crypto, recovery, and FHSS work.


Entries are chronological. Statements such as "uncommitted" or "not tested" describe the state at that timestamp and may be superseded by later entries. The current reviewed checkpoint is summarized at the end.

## 2026-07-12 23:01 EDT — ELRS 4.0.1 base

- Merged the ELRS 4.0.1 base into `secure_4.0.1`.
- All changes below are reviewed as deviations from that base.

## 2026-07-12 23:33 to 2026-07-13 10:38 EDT — session crypto foundation

- Added the bundled ChaCha20 implementation and the `USE_ENCRYPTION` build path.
- The configured binding phrase is now the source for the long-term secret; TX generates a fresh 256-bit session key and 64-bit nonce for every connection or recovery.
- Session material is sent through the reliable uplink path, wrapped with the long-term secret. RX acknowledges it with LinkStats before normal traffic becomes encrypted.
- Mixed radio RSSI noise with the SoC random source when creating session material.
- Fixed the cipher configuration to use 20 ChaCha rounds.

## 2026-07-13 10:38 to 16:15 EDT — recovery, directional streams, and FHSS

- Fixed the original handshake failure in which RX accepted a proposal but TX never observed its acknowledgement. RX now sends a plaintext LinkStats acknowledgement before enabling its downlink cipher.
- Added automatic recovery for decryption failure, lost telemetry, one-sided reset, and configuration/rate changes. Recovery sends marked plaintext SYNC packets and a new session proposal; it never intentionally sends plaintext RC.
- Split the OTA cipher state into independent uplink and downlink ChaCha20 streams, with separate nonce domains and counters. This prevents a lost downlink packet from causing uplink keystream reuse.
- Added the `cryptoResync` SYNC bit, replacing the unused OTA SYNC bit.
- Added a keyed FHSS sequence generator in `src/lib/FHSS/`. It uses ChaCha20 output and a Fisher-Yates permutation while preserving the existing regulatory-domain channels and fixed sync-channel position. Primary and dual-band maps use separate domains.
- FHSS derives from the long-term secret, deliberately not the ephemeral session key. This keeps the hop map stable across a reboot or long loss and avoids a fragile hop-map transition during session recovery.
- Corrected a serious build-key derivation bug: the master key had been hashed from the reduced six-byte UID representation rather than the full binding-phrase definition. It now hashes the full phrase definition before deriving the UID.
- Removed build-script output of UID and master-key material; normal build-flag display redacts `USE_ENCRYPTION`.
- Added native tests for deterministic/keyed secure FHSS and directional stream separation. At this point, native FHSS and encryption suites passed 30 tests total; both LR1121 RX and TX firmware builds completed successfully.
- Audited secret handling before commit. The real `src/user_defines.txt` remains skip-worktree and was not committed; `user_defines.txt.default` is the clean committed template.
- Committed and pushed the work as `3f3d4c4b Add resilient private FHSS recovery` after amending out an incorrect README claim about mLRS.

## 2026-07-13 16:53 EDT — pre-session data gate

- Review found that continuous MAVLink uplink could occupy `DataUlSender` before the session proposal was scheduled. That both starved key establishment and allowed plaintext application DATA packets during recovery.
- Changed `src/src/tx_main.cpp` so that, outside explicit ELRS binding mode, the only pre-session DATA packet allowed is the key proposal while `ENCRYPTION_STATE_PROPOSED`.
- MAVLink and RX-WiFi/MSP requests now remain local until `ENCRYPTION_STATE_FULL`; Airport traffic cannot preempt the key proposal.
- Updated the README to state that no application data crosses the link before the session is active.
- Validation: native encryption suite passed (24 tests); Unified ESP32-C3 LR1121 RX and Unified ESP32 LR1121 TX builds passed.
- This entry is currently uncommitted.

## Open review findings

- Packet integrity and replay resistance remain deliberately outside the passive-observer threat model. OTA CRC is not a MAC; active injection, replay, and recovery-SYNC disruption remain possible.

## 2026-07-13 16:55 EDT — LBT counter accounting

- Fixed RX downlink counter accounting when LBT denies a telemetry slot or telemetry is forced off. `HandleSendDataDl()` now encrypts only when at least one radio will transmit, so an unsent packet cannot consume a ChaCha20 counter position that TX never receives.
- Gemini partial-transmit accounting remains part of the separate Gemini ordering issue.

## 2026-07-13 17:03 EDT — Gemini downlink stream separation

- Fixed the Gemini downlink ordering defect. Downlink ChaCha20 state is now separate for radio 1 and radio 2, with distinct nonce domains and 64-bit counters.
- RX encrypts only the buffer actually transmitted by each physical radio. This also completes the partial-LBT case: a denied Gemini radio does not consume its stream counter.
- TX chooses the matching downlink stream from the physical radio that received each buffer, so dual-radio arrival order cannot advance the wrong cipher state.
- Updated the README to describe per-radio downlink streams.
- No build or test was run, per the explicit no-build instruction. These changes are currently uncommitted.

## 2026-07-13 17:19 EDT — crypto control slot and loss-recovery design review

- Added a temporary crypto-control return slot while a proposal is pending. It overrides telemetry-off and RX force-telemetry-off only long enough to return a plaintext LinkStats acknowledgement; normal telemetry and application data remain disabled.
- Rejected and removed an attempted large forward-counter search. With only a 16-bit OTA CRC, searching 8,192 candidate counter positions has an unacceptable false-lock probability on random corruption.
- The remaining work is a deterministic slot-indexed counter plus a two-packet confirmation before a recovered packet is delivered. No build or test was run, per the explicit no-build instruction.

## 2026-07-13 17:55 EDT — ten-second recovery protocol

- Defined short loss as less than ten seconds. TX and RX retain the active session and advance a 64-bit crypto slot counter from the existing ELRS packet timer, whether a packet is transmitted, received, lost, or denied by LBT.
- Uplink, radio-1 downlink, and radio-2 downlink use the same slot epoch with separate nonce domains. A returning packet therefore selects its ChaCha position directly from the current slot rather than searching a large counter range.
- Resume permits only a very small timer-phase tolerance around the expected slot. Two consecutive slot-consistent decryptions are required before recovered traffic is delivered, preventing a single CRC coincidence from moving durable state.
- Telemetry loss alone never expires the TX session or stops encrypted RC: TX cannot infer that RX stopped receiving. After ten seconds without uplink, RX expires its session and waits for recovery discovery. TX periodically supplies a plaintext discovery SYNC and a crypto-only response window while retaining its existing session.
- A live RX ignores discovery and continues using the session. An expired or rebooted RX sends an explicit plaintext recovery request; only then does TX stop application traffic and begin a full handshake. A TX reboot sends a forced-recovery SYNC, which makes a live RX discard the old session.
- Configuration and packet-rate changes remain explicit full-recovery events. No build or test was run; this entry records the implementation design.

## 2026-07-13 18:17 to 18:27 EDT — slot-clock recovery implementation

- Replaced sequential per-direction packet counters with a shared 64-bit RF-slot clock. The existing ELRS packet timer advances it on TX and RX every interval, including receive gaps, skipped TX intervals, and LBT denial. Uplink and both physical downlink radios retain separate nonce domains.
- Decryption now tries only the expected slot and `±2`. It delivers the first valid packet immediately for minimum RC interruption, never changes durable slot state from a CRC match, and never performs a large CRC-guided search. This supersedes the earlier two-packet-confirmation design because confirmation would deliberately discard valid returning RC packets.
- RX retains both `PROPOSED` and `FULL` sessions for ten seconds without valid uplink. TX no longer discards a live session merely because downlink telemetry disappears, so one-way telemetry loss cannot interrupt encrypted RC.
- The grace timer uses the last successfully decrypted uplink, not ELRS's general valid-packet timestamp; periodic plaintext discovery SYNCs therefore cannot keep a broken crypto session alive indefinitely.
- Added periodic plaintext discovery SYNC handling. Every discovery SYNC opens exactly one crypto-only return slot; a live RX answers encrypted, while an expired or rebooted RX answers plaintext to request a new handshake. TX accepts a plaintext request only in that immediately associated window.
- Forced-recovery SYNC anchors the slot clock before TX constructs the next proposal. Packet-rate/configuration changes and TX reset use this full recovery path.
- Hardened proposal completion: RX remains provisional until it decrypts TX's first encrypted packet, lost proposal acknowledgements are retried, and failed cipher initialization cannot advance either side's state.
- Gated pre-session RX responses to LinkStats in explicit crypto-control slots and removed the ordinary debug print of wrapped session bytes. Long-term master-key buffers now use fixed stack storage instead of leaking allocations on cipher setup failure.
- Crypto-control LinkStats now has an explicitly zeroed reliable-data payload, so application telemetry cannot hitchhike in a plaintext proposal acknowledgement. In Gemini mode either valid physical-radio copy can acknowledge the proposal without waiting for both radios.
- Closed the receiver-side plaintext path: before session establishment RX rejects RC and ignores completed reliable DATA messages other than the wrapped key proposal. Airport mode also routes the proposal through reliable crypto setup before accepting application bytes.
- Cleared completed reliable-transfer buffers during recovery and gated TX delivery of buffered downlink application data until `FULL`, preventing stale data from crossing the session boundary. The final proposal acknowledgement now waits until RX has installed its session ciphers.
- TX records a forced slot anchor only when the SYNC actually reaches the radio transmit path; an LBT-denied SYNC cannot prematurely start the proposal.
- No build, native test, firmware flash, or hardware test was run, per the explicit no-build instruction. Static source and whitespace checks only.

## 2026-07-13 18:57 to 19:00 EDT — live rate and band transition recovery

- Hardware testing reported no recovery after a live RF-band switch and unreliable recovery after packet-rate changes, especially at faster rates.
- Found a crypto bootstrap dependency on stock ELRS connection state: TX waited for `connected` before proposing a session, while RX accepted reliable proposal fragments only after reaching `connected`. A valid crypto-control LinkStats response now marks the peer present directly, and tentative RX accepts only the wrapped session proposal during phase-lock acquisition.
- Found the transition race responsible for deterministic one-sided sessions: RX could answer an old-mode transition SYNC, causing TX to start the session proposal before committing the new radio parameters. TX then carried that proposal and obsolete slot anchor across the rate/band change. Configuration changes now have an explicit barrier: old-mode announcement, radio commit, discard old responses/anchors, then a mandatory new-mode SYNC round trip before proposal.
- The clean configuration enables ELRS `LOCK_ON_FIRST_CONNECTION`. If all old-mode transition SYNCs are missed, stock scanning remains locked to a physically inaudible band or incompatible packet rate. After the ten-second encrypted-uplink grace expires, RX now dwells on the last mode and then temporarily scans supported rates and bands. A successful connection restores the configured lock.
- No build, test, or flash was run.

## 2026-07-13 19:16 EDT — hardware regression stop

- A fresh TX/RX flash of the uncommitted slot-clock and transition work completed explicit ELRS binding but immediately returned to no-link flashing. No packet rate or RF band produced a usable initial encrypted session.
- This disproves the source-only bootstrap assessment above. The current uncommitted recovery implementation is hardware-failed and must not be presented as usable firmware.
- Stopped requesting further manual flash/bind cycles. The last hardware-working pushed recovery point is `3f3d4c4b`; no build, flash, Git reset, staging, commit, or push was performed here.

## 2026-07-13 19:22 EDT — initial-session bootstrap fix

- Traced the fresh-flash failure to an unnecessary circular bootstrap dependency: TX required a plaintext discovery LinkStats reply before it would send the wrapped session-key proposal. A missing downlink therefore prevented the session from ever starting, even though RX could hear TX.
- Removed the discovery-reply prerequisite. Once a forced-recovery SYNC has actually reached the radio transmit path and anchored the shared slot clock, TX begins the reliable wrapped-key proposal directly.
- Kept the slot anchor on every pre-session SYNC, including while the reliable proposal is in progress. RX can miss the first discovery packet and still acquire the correct clock without discarding already received proposal fragments; an established RX still discards its old session when it sees a forced-recovery anchor.
- This does not permit plaintext RC, telemetry, MAVLink, Airport, or other application data. The only pre-session DATA remains the session material wrapped with the long-term key.
- Configuration transitions still discard old-mode anchors and cannot propose a session until a new-mode SYNC is transmitted after the radio change.
- No build, test, flash, or Git-state mutation was performed.

## 2026-07-13 19:23 EDT — stock ELRS binding boundary

- Made explicit binding a hard protocol boundary. Entering binding now discards TX and RX crypto session state plus stale reliable-transfer state; exiting binding leaves both sides at `ENCRYPTION_STATE_NONE` for a fresh post-bind session.
- Disabled TX packet encryption and RX packet decryption while `InBindingMode`. The 50 Hz/inverted-IQ discovery and UID exchange therefore follows the stock ELRS 4.0.1 packet path without depending on slot clocks, session proposals, crypto-control telemetry, or previous link state.
- Session establishment starts only after stock binding exits and the first normal-mode forced-recovery SYNC is actually transmitted.
- No build, test, flash, or Git-state mutation was performed.

## 2026-07-13 19:40 EDT — rate-independent session activation anchor

- Hardware testing showed successful operation at 100, 150, 250, and 333 Hz, but a one-sided session loop at 50 and 500 Hz: plaintext crypto-control telemetry appeared intermittently while encrypted RC never became valid.
- Changed TX proposal completion from an immediate `PROPOSED` to `FULL` transition to a two-step activation. After the wrapped proposal is acknowledged, TX transmits a fresh forced-anchor SYNC and waits for its associated crypto-control reply before enabling encrypted RC and application traffic. If either packet is lost, the provisional session repeats the anchor exchange.
- RX keeps the installed provisional session and proposal state across the final anchor while resetting its slot to the transmitted ELRS nonce. The first encrypted packet is therefore tied to an immediately preceding shared RF event instead of main-loop and telemetry timing accumulated during the multi-packet proposal.
- The activation cost is one anchor/control round trip, with a minimum of roughly two RF intervals: about 4 ms at 500 Hz and 40 ms at 50 Hz. It applies only to session establishment or full re-establishment, not recovery from a short dropout while the session is retained.
- No build, test, flash, or Git-state mutation was performed.

## 2026-07-13 19:45 EDT — protocol sequence documentation

- Replaced the abbreviated README session/recovery description with an explicit state-machine reference derived from the current TX and RX paths.
- Documented already-bound startup, stock explicit ELRS binding, wrapped proposal fragmentation and acknowledgement, the reliable final-anchor exchange, first encrypted-packet proof, normal traffic, each one-way/two-way loss case, ten-second expiry, TX/RX reboot, and packet-rate/RF-mode transitions.
- Added symptom interpretation so an observed UID bind, plaintext crypto-control telemetry, and a fully active encrypted RC link are not mistaken for the same milestone.
- No build, test, flash, or Git-state mutation was performed.

## 2026-07-13 21:33 EDT — minimal nonce-only session proposal

- Continued hardware testing showed that stock ELRS produced a usable link immediately while PrivacyLRS frequently completed or exited UID binding without establishing usable encrypted RC. The remaining failure boundary was the PrivacyLRS session exchange after the stock bind packet path.
- Reduced the reliable session proposal from 41 bytes (`MSP_ELRS_INIT_ENCRYPT`, 64-bit nonce, and 256-bit wrapped random key) to 9 bytes (`MSP_ELRS_INIT_ENCRYPT` and a public 64-bit nonce).
- TX and RX now derive the same 256-bit session key locally with ChaCha20 keyed by the long-term secret, using the public nonce as IV and the dedicated `PLRSKDF1` counter domain. The derived key is never transmitted.
- This preserves the passive-observer security model and its existing lack of forward secrecy: knowing the long-term secret still recovers recorded sessions, while not knowing it leaves the session key hidden. A fresh hardware/RSSI-derived 64-bit nonce prevents keystream reuse across normal session establishment and recovery.
- The shorter proposal removes 32 transmitted bytes and most reliable-fragment/control exchanges from initial link establishment. The acknowledged final slot anchor and the plaintext RC/application-data gate remain unchanged.
- Updated the README's exact protocol sequence and source terminology to describe nonce-derived sessions.
- Removed crypto state manipulation from binding entry itself. Crypto packet transforms and crypto-control scheduling are now bypassed under `InBindingMode`, leaving the stock binding transmitter/receiver timing intact; old session state is cleared at binding exit before normal-mode acquisition.
- No build, test, flash, or Git-state mutation was performed.

## 2026-07-14 - automated hardware pipeline and checkpoint validation

- Used the external `elrstest` build, flash, configuration, CRSF-handset, receiver-UART, smoke-test, and RF-sweep pipeline with a RadioMaster Nomad X-Band TX and RadioMaster XR1 RX.
- Added temporary TX status diagnostics for crypto state and reliable proposal transfer. Under `USE_ENCRYPTION`, the ELRS status packet's normal `pktsBad` and `pktsGood` fields currently carry proposal state, package, confirmation polarity, and retry count instead of their stock meanings.
- Reduced the decrypt candidate set from a broad nine-slot search to the current persistent receive offset plus its immediate neighbors: `0`, `-1`, and `+1`. The XR1 firmware build succeeded after this change. This bounded the per-packet ChaCha20 and CRC cost at high packet rates.
- The work was committed and pushed as `2ed936e8` (`checkpoint`) on `secure_4.0.1`. The exported Git state showed the branch clean and aligned with `origin/secure_4.0.1`; the only untracked files were the review export artifacts.

## 2026-07-14 - final recorded RF sweep

Configuration: initial 2.4 GHz at 150 Hz, telemetry 1:2, 75-second transition timeout, one-second measurement dwell, bands 2.4 GHz and 915 MHz, and `test_rates = none`.

- Initial 2.4 GHz `150Hz(-112dBm)` session passed after 21.0 seconds. The measurement received 75 valid RC frames in 1.000 seconds with no RC or link dropout events.
- The 2.4 GHz change from 150 Hz to `250Hz(-108dBm)` passed after 11.6 seconds. The measurement received 125 valid RC frames in 1.001 seconds with no dropout events.
- The change from 2.4 GHz 250 Hz to 915 MHz `250Hz(-111dBm)` passed after 12.3 seconds. The measurement received 126 valid RC frames in 1.001 seconds with no dropout events.
- The return from 915 MHz to 2.4 GHz failed to re-establish within 75 seconds. Band selection automatically changed the rate to `K1000(-103dBm)`; TX reported connected but LQ remained zero. The one-second measurement observed 195 valid RC frames against 500 expected and recorded a link dropout.
- Explicitly selecting `150Hz(-112dBm)` after the failed K1000 transition restored the link in 1.6 seconds. The measurement received 75 valid RC frames in 1.001 seconds with no dropout events.
- `test_rates = none` skips the explicit per-band rate sweep, but the LR1121 band callback itself changes rate. The final failure is therefore a real combined band/rate transition caused by `TXModuleParameters.cpp` choosing the fastest supported rate in the destination band.

## 2026-07-14 - code and documentation review

- Corrected the README to match the current implementation. Proposal completion uses a 16-marked-SYNC activation barrier, not an acknowledged final-SYNC round trip.
- Corrected the documented decrypt window. `common.cpp` tries the current persistent receive offset and one adjacent slot on either side, not two slots on either side.
- Recorded the current hardware pass/fail matrix and stopped describing the checkpoint as unbuilt or untested.
- Open defect: LR1121 band selection silently chooses the fastest supported destination-band rate. On this Nomad/XR1 pair, the 915 MHz to 2.4 GHz return selected K1000 and failed.
- Open reliability risk: the same 10-second constant is used for short-loss session retention and the complete TX `PROPOSED` handshake timeout. Observed end-to-end establishment took 11.6 to 21.0 seconds in successful cases, so proposal-phase timing should be instrumented and given a separate timeout before further optimization.
- Open reliability risk: the 16-SYNC activation barrier is repeated but not acknowledged. TX enters `FULL` after transmitting the barrier even if RX missed every anchor; RX then remains `PROPOSED` until encrypted proof succeeds or its timeout path resets it.
- Cleanup required before release: remove or build-gate the temporary status-field diagnostics so `pktsBad`, `pktsGood`, and reserved warning bits recover their standard meanings.
- Security boundary unchanged: packets are encrypted for passive-observer privacy but not authenticated. Valid plaintext recovery control can still be used by an active attacker to disrupt or reset sessions.

## 2026-07-14 - band-change rate preservation (primary defect fixed)

- Fixed the high-severity band-change defect (CODE_REVIEW finding #1). The `luaRFBand` callback in `src/lib/tx-crsf/TXModuleParameters.cpp` scanned the rate table from index zero and applied the fastest supported rate in the destination band, which selected K1000 on the 915 MHz to 2.4 GHz return and stranded the encrypted link.
- Under `USE_ENCRYPTION`, a band change now preserves the current nominal rate when the destination band offers it (rates share the same `interval`, e.g. 250 Hz == 4000 us in every band), and otherwise falls back to a conservative transition rate of at most 250 Hz. Stock (non-encrypted) behaviour is unchanged behind the `#else` branch.
- Built HEAD + fix onto both units (`fa54be`), re-verified, and re-ran smoke and the committed rf-sweep.
  - Smoke: 8 PASS + 1 SKIP (flash_probe skipped on the shared handset port). Link up 1.6 s, RC delivery 99.9 %, telemetry return OK. No regression from the checkpoint code.
  - rf-sweep (2.4 GHz + 915 MHz, `test_rates = none`): all transitions PASS, exit 0. Initial 2.4 GHz 150 Hz 21.1 s; 2.4 GHz to 915 MHz held 250 Hz, 12.3 s; **915 MHz to 2.4 GHz return now holds 250 Hz and re-establishes in 1.6 s, LQ 100, 100 % RC, zero dropouts** (previously K1000, LQ 0, failed within 75 s); restore to 150 Hz 13.6 s.
- The `packets_bad=11 packets_good=0` in the sweep output is the still-present temporary diagnostic overload (finding #4), not a link failure.
- Still open and untouched this session: temporary status-field diagnostics (finding #4), separate proposal timeout vs the shared 10 s grace constant (finding #2), the unacknowledged 16-SYNC activation barrier (finding #3), and K1000 sustained operation on this bench (finding #5, expected UART limit).

