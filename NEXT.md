# PrivacyLRS — next-session action plan

_Written 2026-07-14 by Claude (Fable 5). Handoff after Codex's checkpoint work._

## Where things stand

- **Repo HEAD:** `fa54be4b review` (docs: CODE_REVIEW.md, DEVLOG.md, README) on top of
  `2ed936e8 checkpoint` (the actual code: shared 64-bit RF-slot clock recovery,
  decrypt window narrowed to `{0,-1,+1}`, per-radio Gemini downlink streams,
  nonce-only 9-byte session proposal, temporary TX status-field diagnostics).
- **Flashed on hardware RIGHT NOW: `3f3d4c` on BOTH units** (Nomad + XR1), i.e.
  `3f3d4c4b Add resilient private FHSS recovery` — one code-commit BEHIND HEAD.
  Verified this session via `elrsflash.py verify nomad|xr1`.
- **A `flash xr1` was interrupted by the usage cutoff. First action next session is to flash it it**

## Hardware / bench facts (from elrstest.ini + README)

- Nomad TX: `/dev/serial/by-path/pci-0000:02:00.0-usb-0:8:1.0-port0` (ttyUSB0),
  USB-C is a working CRSF handset port via a littlefs `/hardware.json` pin override
  (serial_rx:3/serial_tx:1). Override survives app flashes (elrsflash never writes 0x3d0000).
- XR1 RX: `/dev/serial/by-path/pci-0000:02:00.0-usb-0:9:1.0-port0` (ttyUSB1), 460800 CRSF.
- Both CP2102s report serial "0001" → always use by-path names, never by-id / ttyUSBn.
- Domain must be FCC915 on BOTH or 915MHz/X-Band never link. XR1 has ONE LR1121 →
  cannot receive X-Band/Dual-Band; test_bands excludes X-Band deliberately.
- Sequential builds only (resource-constrained machine) — never two pio runs at once.

## Plan (do in order, defer to user between expensive phases)

### Phase 0 — confirm hardware state (cheap, no build)
1. `python3 elrsflash.py verify xr1` and `verify nomad`. Reconcile against intent.
   Goal state to TEST is current HEAD code = `2ed936e8`. If both show `3f3d4c`,
   that's the known-good baseline; HEAD adds the checkpoint code that needs testing.

### Phase 1 — get HEAD onto hardware and establish a baseline
2. Build + flash current HEAD to both, ONE build at a time:
   `python3 elrsflash.py build nomad` → `build xr1` → `flash nomad` → `flash xr1`
   (or `pipeline all`, but confirm it serializes the two pio builds first).
3. `python3 elrsflash.py verify nomad|xr1` — expect hash matching HEAD (`2ed936e8`/`fa54be4b`).
4. `python3 elrstest.py smoke` — must get all 9 PASS. This is the baseline gate.
   If smoke regresses vs 3f3d4c, STOP and compare: the checkpoint code may be worse
   than 3f3d4c on basic link (Codex's 19:16 entry warned an earlier uncommitted
   recovery build was "hardware-failed"; make sure 2ed936e8 isn't a regression).

### Phase 2 — reproduce the known open defect
5. `python3 elrstest.py rf-sweep` with the committed `elrstest.ini`
   (2.4GHz+915MHz, test_rates=none). Expected reproduction: the **915MHz→2.4GHz
   return fails** because the LR1121 band callback jumps to **K1000**, which the
   encrypted link + 460800 RX UART cannot sustain (LQ stays 0, ~195/500 RC frames).

## Primary target bug — band change selects fastest (K1000)

- **File:** `src/lib/tx-crsf/TXModuleParameters.cpp`, the `luaRFBand` callback
  (~lines 849-878). Comment: "Choose the fastest supported packet rate in this RF band."
- **This is STOCK ELRS 4.0.1 behavior** (confirmed: same code in `git show 4.0.1:...`),
  NOT a PrivacyLRS change. Stock tolerates it; the encrypted link does not.
- **Fix direction (Codex's rec #1, sound):** on band change, prefer preserving the
  current nominal packet rate if it exists in the destination band; else fall back to
  a conservative transition rate (150/250Hz), NOT index-0/fastest. Then
  `recalculatePacketRateOptions(...)` and expose the actually-selected rate.
- Keep the change minimal and behind `#ifdef USE_ENCRYPTION` if it would alter stock
  behavior in a way upstream wouldn't want — but preserving current rate is arguably a
  strict UX improvement worth doing unconditionally. Decide with the user.
- **Caveat:** part of the K1000 failure is the bench's 460800 RX UART being too slow
  for 1000Hz RC (260kbit/s) — a wiring limit, not firmware. Separate "can't select
  K1000 cleanly" (fixable) from "K1000 itself won't run on this bench" (expected).
  Do NOT chase K1000 sustained operation as a firmware bug on this bench UART.

## Secondary issues to review after the sweep passes (from CODE_REVIEW.md + my own review)

- **Temporary diagnostics MUST be removed/build-gated before any release:**
  `TXModuleParameters.cpp` ~399-414 overloads `pktsBad`/`pktsGood` and status/warning
  bits with crypto state instead of real CRSF packet counts. This is why sweep output
  shows `packets_bad=11 packets_good=0`. Gate behind a dedicated diagnostic flag.
- **Shared 10s constant** (`CRYPTO_SHORT_LOSS_GRACE_MS` in `include/encryption.h`) is
  reused for both RX short-loss retention AND the full TX PROPOSED handshake timeout.
  Give the proposal its own instrumented timeout; establishment took 11.6-21.0s.
- **16-SYNC activation barrier is unacknowledged** (tx_main ~423-438, ~935-940,
  ~1972-1979): TX enters FULL after sending 16 marked SYNCs even if RX missed all of
  them; RX stays PROPOSED until an encrypted packet decrypts. Consider a real ack.
- **My earlier (pre-Codex) review finding — re-check if still live:** `InitCrypto()` /
  `CollectEntropy` / `RandRSSI` did a blocking `delay(1)`-per-bit loop
  (~320ms for 40 bytes) + forced the radio to RX mode, called from loop() while
  connected. Codex rewrote much of this (slot clock); confirm entropy collection is no
  longer a multi-hundred-ms main-loop stall on connect/recovery. If it still blocks,
  that's an RC-glitch bug worth fixing (collect entropy incrementally / non-blocking).
- **Plaintext recovery control has no MAC** (rx_main ~1319-1370): active attacker can
  reset a session. DELIBERATE per threat model (passive-observer only) — document, do
  not "fix".

## Working style reminders
- One build/test at a time. Defer to the user between expensive phases; don't run the
  whole loop autonomously.
- `src/user_defines.txt` is skip-worktree with the real binding phrase — never commit it.
- Read results from `elrstest.log` / `elrsflash.log` / `rx.log` / `tx.log` (truncated per run),
  not scrollback.
