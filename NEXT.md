# PrivacyLRS — next-session notes

_Updated 2026-07-15 after the establishment-latency work. History in DEVLOG.md._

## Where things stand

- **HEAD:** `0de7c8c1` (docs) on `secure_4.0.1`. Code state = `ff840bb9`:
  band-change rate preservation, fast rate-aware TX proposal retry
  (`CRYPTO_PROPOSAL_RETRY_*`), RX stale-PROPOSED expiry
  (`CRYPTO_RX_PROPOSED_TIMEOUT_*`), diagnostics behind `CRYPTO_BENCH_DIAGNOSTICS`.
- **Flashed:** TX = band fix + fast retry (diagnostics OFF); RX = stale-PROPOSED
  expiry. Both verified working on the bench.
- **Validation:** 2026-07-15 rf-sweep all PASS — initial 8.0 s (cold disconnect +
  RX scan), transitions 1.6–4.3 s, 100 % RC delivery, no dropouts. Smoke 8 PASS +
  1 by-design SKIP (earlier run).
- Establishment latency is FIXED as a symptom (was 11.6–21 s, now ≤ ~4 s). The
  micro-cause of the first-attempt stubborn-transfer wedge (RX validates only
  SYNCs at LQ≈25 after a mode change) is mitigated by the timers, not root-caused.

## Candidate next work (discuss with user first)

1. **Loss/recovery test in elrstest** — the flight-critical path (short fade,
   >10 s outage, one-sided reboot) has no automated coverage. Measure
   time-to-recovered-RC. This was agreed as valuable before the latency work.
2. **Root-cause the first-attempt wedge** — needs RX-side diagnostics (why do
   proposal DATA packets stop validating after a mode change while SYNCs pass?).
   Worst case is now ~2 s, so this is polish, not a dealbreaker.
3. **K1000 sustained delivery** — session establishes (RX FULL, 195/500 CRC-valid
   frames/s decrypted); delivery bottleneck suspected on the test-rig side per
   user. Deferred; do NOT chase as firmware on this bench (460800 RX UART).
4. **16-SYNC barrier ack** (finding #3) — now self-healing in ~1 s via the RX
   expiry; low priority.

## Bench facts (unchanged)

- Ports/envs/domains in `crsf-experiments-lain/elrstest/elrstest.ini`; use
  /dev/serial/by-path names. XR1 cannot receive X-Band. One build at a time.
- `src/user_defines.txt` is skip-worktree with the real binding phrase — never commit.
- Read results from elrstest.log / elrsflash.log / rx.log / tx.log, not scrollback.
- RX LinkStats `mode` field encodes rate_enum | 0x40 (PROPOSED) | 0x80 (FULL) —
  invaluable for establishment debugging.
