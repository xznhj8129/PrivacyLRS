# PrivacyLRS checkpoint review

## Reviewed state

- Repository: `xznhj8129/PrivacyLRS`
- Branch: `secure_4.0.1`
- HEAD: `2ed936e875007e99885d62edeb5322a8aad0f846` (`checkpoint`)
- Base used for the current change set: `3f3d4c4ba76b7dbbfcae9e83bc30fee31a56de37`
- Git state: clean and aligned with `origin/secure_4.0.1`; the exported review files were the only untracked paths
- Scope from `3f3d4c4b` to `2ed936e8`: 12 files, including session/recovery logic, LR1121 mode handling, diagnostics, binary configuration/flash support, README, and DEVLOG

The checkpoint is no longer accurately described as unbuilt or untested. It has hardware-backed success on several modes, plus one repeatable high-rate transition failure.

## Hardware state recorded on 2026-07-14

Test pair: RadioMaster Nomad X-Band TX and RadioMaster XR1 RX, telemetry 1:2, 75-second transition timeout.

| Operation | Result | Evidence |
| --- | --- | --- |
| Initial 2.4 GHz at 150 Hz | Pass | Link and RC after 21.0 s; 75/75 valid frames in 1.000 s; no measured dropouts |
| 2.4 GHz 150 Hz to 250 Hz | Pass | Link and RC after 11.6 s; 125/125 valid frames in 1.001 s; no measured dropouts |
| 2.4 GHz 250 Hz to 915 MHz 250 Hz | Pass | Link and RC after 12.3 s; 126/126 valid frames in 1.001 s; no measured dropouts |
| 915 MHz 250 Hz to 2.4 GHz | Fail | Band callback selected K1000; no usable link within 75 s; LQ 0; 195 RC frames/s observed against 500 expected |
| K1000 to 2.4 GHz 150 Hz | Pass | Link and RC after 1.6 s; 75/75 valid frames in 1.001 s; no measured dropouts |

## Findings

### 1. High: RF band selection silently selects the fastest destination-band rate

Location: `src/lib/tx-crsf/TXModuleParameters.cpp`, around lines 849 to 877.

The LR1121 band callback scans the packet-rate table from index zero and applies the first supported rate. The code comment explicitly calls this the fastest supported rate. Returning to 2.4 GHz therefore selects K1000 even when the test requested band transitions only.

This is the direct cause of the final sweep failure. `test_rates = none` does not make the operation band-only because the firmware changes the packet rate inside the band callback.

Recommended correction:

1. Preserve the current nominal rate when that rate exists in the destination band.
2. Otherwise use an explicitly defined conservative transition rate, such as 150 or 250 Hz for the tested LR1121 pair.
3. Recalculate and expose the final selected rate after the band change so callers can verify the actual combined mode.
4. Keep high-rate modes such as K1000 as explicit user selections until they pass sustained encrypted RC testing.

### 2. Medium: the full proposal timeout reuses the 10-second short-loss grace constant

Locations:

- `src/include/encryption.h`: `CRYPTO_SHORT_LOSS_GRACE_MS = 10000U`
- `src/src/tx_main.cpp`, around lines 1982 to 1990

The same ten-second value controls RX short-loss retention and the complete TX `PROPOSED` handshake timeout. Successful end-to-end establishment was observed at 11.6, 12.3, and 21.0 seconds. Those measurements include acquisition and mode transition time, so they do not prove that a single proposal itself exceeded ten seconds, but they make repeated proposal timeout/restart cycles plausible.

Use a separate instrumented proposal timeout. Record proposal start, final reliable acknowledgement, activation-barrier completion, and first encrypted proof as separate timestamps before choosing its value.

### 3. Medium: the activation barrier is repeated but not acknowledged

Locations:

- `src/src/tx_main.cpp`, around lines 423 to 438
- `src/src/tx_main.cpp`, around lines 935 to 940
- `src/src/tx_main.cpp`, around lines 1972 to 1979

After the reliable proposal is acknowledged, TX sets `cryptoActivationSyncsRemaining` to 16. It sends 16 marked plaintext SYNC packets and enters `FULL` once the counter reaches zero. These SYNC packets do not open a `PROPOSED` crypto-control response window, so there is no final acknowledged anchor exchange.

The repetition gives RX many chances to receive an anchor, but TX cannot know whether any were received. If RX misses all 16, TX begins encrypted traffic while RX remains provisional. Recovery then depends on later timeout/reset behavior.

The README previously documented an acknowledged final-anchor round trip. The updated README now describes the actual 16-SYNC barrier.

### 4. Medium: temporary diagnostics replace normal ELRS status counters

Location: `src/lib/tx-crsf/TXModuleParameters.cpp`, around lines 399 to 414.

With `USE_ENCRYPTION`, `pktsBad` and `pktsGood` carry StubbornSender state and retry diagnostics instead of CRSF handset packet counts. Reserved status/warning bits also expose `PROPOSED` and `FULL` state.

This explains the repeated `packets_bad=11 packets_good=0` values in the sweep output. It is useful for bench work but makes standard status fields misleading. Guard it behind a dedicated compile-time diagnostic flag or remove it before a release build.

### 5. Medium: K1000 remains a sustained-operation failure

The narrowed decrypt candidate list improved the bounded per-packet workload, but the final current-state sweep still failed at K1000. It observed 195 valid RC frames/s against an expected 500 and never recovered usable LQ within 75 seconds.

Do not advertise K1000 as supported for the current Nomad/XR1 encrypted path. The next investigation should measure RX loop and ISR timing, decrypt candidate count, timer phase offset, missed packet bursts, and whether the 500 expected RC frame rate is correct for the selected K1000 telemetry ratio and packet duplication mode.

### 6. Low: plaintext recovery control remains intentionally disruption-prone

Location: `src/src/rx_main.cpp`, around lines 1319 to 1370.

A valid UID/CRC plaintext recovery SYNC or proposal DATA packet can reset an existing session. This is consistent with the stated passive-observer threat model, but an active transmitter can force disruption because the control path has no MAC.

The README correctly treats this as a deliberate security boundary rather than an implementation accident.

## Protocol state confirmed from current code

1. TX in `NONE` sends marked plaintext SYNC packets and blocks plaintext RC/application data.
2. TX derives a session key locally from the long-term secret and a public 64-bit nonce, then sends the 9-byte proposal through the reliable uplink-data sender.
3. RX derives and installs provisional uplink and per-radio downlink ciphers before acknowledging the completed proposal.
4. The proposal acknowledgement embeds RX's current 16-bit slot anchor in the first two LinkStats bytes.
5. TX aligns to that anchor, sends 16 marked plaintext SYNC packets, then enters `FULL` without a final SYNC acknowledgement.
6. RX enters `FULL` only after an encrypted uplink decrypts and passes the normal OTA type and CRC checks.
7. Decryption tests the persistent receive offset and one adjacent slot on either side: `{0, -1, +1}` relative to the current offset.
8. Uplink and each physical downlink radio use separate nonce domains over a shared RF-slot clock.
9. RC and application data remain blocked until the sending side reaches `FULL`.

## Documentation changes made

- Added the exact reviewed branch, commit, hardware state, pass matrix, and known K1000 failure.
- Replaced the stale acknowledged-final-anchor description with the actual 16-SYNC activation barrier.
- Corrected the decrypt window from two slots on either side to one adjacent slot on either side.
- Explained that LR1121 band selection also changes rate and currently chooses the fastest supported destination rate.
- Replaced the obsolete statement that current recovery changes had not been built or run.
- Appended the current checkpoint, hardware sweep, review findings, and release cleanup items to `DEVLOG.md`.

## Validation performed during this review

- Verified the exported Git state and empty working-tree patch.
- Compared commit `2ed936e8` against `3f3d4c4b` and reviewed the changed implementation files.
- Reconstructed the interrupted Codex history and matched it against the final user-supplied sweep.
- Compiled the changed Python support modules with `python3 -m py_compile` successfully.
- Did not run firmware builds or native C++ tests in this sandbox because the PlatformIO toolchain and hardware are not available here.
