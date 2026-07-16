# PrivacyLRS development log — `secure_4.0.1`

Timestamps use America/Toronto local time (EDT).

This branch is the **conservative** PrivacyLRS 4.0.1 line. Its goal is to stay close to
sensei's original `secure_01` design: it carries the ExpressLRS 4.0.1 port plus only critical
bug fixes, and deliberately does **not** include the larger session-recovery, slot-clock, and
FHSS rework. That work lives on the `secure_02` branch (with its own DEVLOG).

Entries are chronological.

## 2026-07-12 23:01 EDT — ExpressLRS 4.0.1 base

- Merged the ExpressLRS 4.0.1 base into the secure line (`e733cbf2`). Everything below is a
  deviation from that base.

## 2026-07-16 — branch established as the conservative line

Branched the full feature work off to `secure_02` and rewound this branch to the 4.0.1 merge
plus a small set of critical fixes. Fixes carried on this branch:

- **Master key derived from the full binding phrase (`231671fa`).** `process_build_flag()` in
  `src/python/build_flags.py` reassigned its `define` variable to the `-DMY_UID=...` string
  *before* computing the SHA-256, so the encryption master key was derived from the 6-byte
  MD5-truncated UID instead of the binding phrase. ELRS broadcasts UID4/UID5 in every SYNC
  packet and UID2–5 in bind packets, so only ~16–32 bits of the key were ever secret —
  trivially brute-forceable, and the wrapped session key falls with it. The hash is now taken
  from the full binding-phrase define before the UID substitution (key width stays 128 bits,
  as in `secure_01`). The build log no longer prints the derived master key or UID bytes, and
  the build-flags echo redacts `USE_ENCRYPTION`. OTA-breaking versus the old derivation.
- **ChaCha20 for real (`6df78cd7`).** The earlier "ChaCha20 upgrade" set the constructor to 20
  rounds but `InitCrypto()`/`CryptoSetKeys()` still called `setNumRounds(12)`, silently running
  ChaCha12 on the link. Rounds are now 20 in both paths, with matching test fixtures.
  OTA-breaking versus earlier PrivacyLRS builds.
- **`RandRSSI()` / `Radio.RXnb()` signature (`1a24752c`).** 4.0.1 changed `Radio.RXnb()` to take
  no arguments; the SX1280 and LR1121 variants of `RandRSSI()` still passed the old RX_CONT mode
  constants, breaking compilation of encrypted TX targets on those radios.

Verification on this branch: native suite runs 113 cases with only the 2 by-design failures
(`test_single_packet_loss_desync`, `test_burst_packet_loss_exceeds_resync` — raw-cipher desync
demonstrations, not regressions); `Unified_ESP32_LR1121_TX_via_UART` and
`Unified_ESP32C3_LR1121_RX_via_UART` firmware both build. Confirmed the built TX firmware embeds
the full-phrase-derived key and no longer the UID-derived one, and that the build log carries no
key material.

## Known issues carried over from `secure_01` (for sensei)

These were identified during the `secure_02` work and are **not** fixed on this conservative
branch, because the fixes are entangled with the larger recovery/slot-clock redesign that this
branch intentionally omits. They apply to the `secure_01`-style shared-counter encryption design
that this branch still uses. Flagging them so they are known:

- **Shared OTA counter can cause keystream reuse.** Uplink and downlink share one ChaCha20
  counter/keystream domain. A lost or LBT-denied downlink packet can desync the counter such
  that a keystream position is reused, which is a real confidentiality weakness for a stream
  cipher. `secure_02` fixes this by splitting into independent per-direction (and per-physical-
  radio, for Gemini) nonce domains driven by a shared RF-slot clock. On this branch the
  `{-1,0,+1..+3}` decrypt lookahead papers over short desyncs but does not eliminate the reuse
  window.
- **Gemini / dual-band downlink is unreliable under encryption.** 4.0.1 adds dual-band dual-
  packet transmit and a second-radio receive buffer. With a single shared counter, dual-radio
  arrival order can advance the wrong cipher state and a denied radio still consumes a counter
  position. `secure_02` addresses this with per-radio downlink streams. Single-radio links are
  unaffected.
- **LBT / telemetry-off downlink counter accounting.** When LBT denies a telemetry slot or
  telemetry is forced off, the RX can encrypt (and advance the counter for) a packet that is
  never transmitted, so TX never sees that counter position. Same root cause as the shared-
  counter issue above.
- **Application data can occupy the link before the session is active.** Continuous MAVLink
  uplink (or Airport traffic) can occupy `DataUlSender` before the session proposal is scheduled,
  both starving key establishment and allowing plaintext application DATA during (re)establishment.
  Found by review, not observed on hardware; matters mainly for MAVLink/Airport users. `secure_02`
  adds an explicit pre-session data gate.

## Security boundary (unchanged from `secure_01`)

Packets are encrypted for **passive-observer** privacy only. OTA CRC is not a MAC: there is no
authentication, no replay protection, and no forward secrecy. Active injection, replay, and
session disruption remain possible and are out of scope for this branch.
