# PrivacyLRS stabilization and release-readiness report

**Review date:** 2026-07-16  
**Reviewed branch:** `secure_4.0.1`  
**Reviewed checkpoint:** commit `0bb335d7`, together with the current uncommitted
RX diagnostic gating and `CryptoTrace` instrumentation  
**Base:** ExpressLRS 4.0.1  
**Purpose of this report:** establish the shortest credible path to make
PrivacyLRS usable, reliable, and secure within its stated scope, without adding
unverified features, expanding the protocol unnecessarily, or entering open-ended
debugging work.

**Evidence basis:** source review of the PrivacyLRS changes relative to
ExpressLRS 4.0.1, the repository's native-test configuration and CI workflow,
and the dated Nomad/XR1 results recorded in the project documentation. The
hardware results below are existing recorded evidence, not a new test run made
while preparing this report.

## Executive decision

PrivacyLRS is a promising second-generation privacy fork, not a release-ready
control link.

The current architecture is a substantial improvement over the older
`secure_01` design:

- The session key is derived independently by TX and RX and is never transmitted.
- The full configured secret feeds a 256-bit long-term key.
- Uplink and each physical downlink radio use distinct ChaCha20 domains.
- A shared RF-slot clock advances independently of packet delivery.
- RC and application data are blocked until the crypto session is active.
- The FHSS sequence is derived from the secret with an unbiased keyed shuffle.
- Explicit ELRS binding remains on the stock path.
- The documentation records failed experiments and hardware regressions honestly.

Three release blockers prevent release:

1. RX parses raw ciphertext as possible plaintext recovery DATA before attempting
   decryption. A legitimate encrypted OTA4 packet can therefore tear down a live
   session with probability approximately 1 in 65,536 uplink packets.
2. The 64-bit crypto slot and receive offsets are shared across interrupt and
   non-interrupt contexts without synchronization. This is a C++ data race and
   can select the wrong ChaCha keystream.
3. The native encryption suite excludes the production crypto and state paths,
   substitutes obsolete helper logic, and assumes CRC success. CI consequently
   provides a false-green signal rather than validating an encrypted PrivacyLRS
   build.

Two additional inexpensive defects belong in the same stabilization phase:

- The committed checkpoint still permits RX LinkStats fields to be overwritten
  by bench diagnostics; the correct `CRYPTO_BENCH_DIAGNOSTICS` gate exists only
  in the working tree.
- Malformed `USE_ENCRYPTION` input can produce partially uninitialized master-key
  material because the runtime hex parser silently truncates or accepts invalid
  input and callers ignore its result.

The correct response is a narrow stabilization program, not a protocol rewrite.
Fix these defects, test production code, validate one declared hardware/rate
surface, package one explicitly limited release candidate, and stop.

## Scope and security model

PrivacyLRS protects the contents of RC commands, CRSF telemetry, MAVLink
telemetry, and related application data from a passive RF observer who does not
know the configured secret.

It does not hide that a radio link exists. A passive observer can still observe
RF activity, timing, cadence, selected band and rate, plaintext acquisition and
recovery SYNC packets, and the fixed sync-channel structure.

It does not currently resist an active attacker:

- Packets are encrypted but not cryptographically authenticated.
- The OTA CRC is error detection, not a MAC.
- An active transmitter can jam, replay, manipulate, or disrupt recovery.
- Recovery SYNC and session-control traffic are not authenticated.
- There is no forward secrecy. Compromise of the long-term secret can expose
  recorded sessions.

These limitations are acceptable only because the release claim remains
strictly passive-observer confidentiality. Authentication, active-attack
resistance, and low probability of intercept are explicitly outside this
checkpoint.

Users must use a generated high-entropy secret. SHA-256 produces a 256-bit
output but does not add entropy to a predictable human phrase; recorded session
traffic permits offline guessing of weak phrases.

## Current architecture

### Long-term and session keys

The build hashes the complete binding-phrase definition with SHA-256 to produce
the 256-bit long-term secret. The reduced six-byte ELRS UID remains a separate
stock compatibility value and is no longer the source of the master key.

For each session, TX creates a public 64-bit nonce. TX and RX independently
derive the same 256-bit session key using ChaCha20 keyed by the long-term secret,
with the public nonce as IV and a dedicated `PLRSKDF1` counter domain. Only the
nine-byte proposal—an internal opcode plus the public nonce—crosses the air. The
session key itself is never sent.

The construction is a custom ChaCha-based PRF, not HKDF. It appears properly
domain-separated for the current purpose, but the exact construction and exact
ChaCha variant must be documented accurately. Replacing it with HKDF is not
required for the stabilization checkpoint.

### Traffic streams and slot clock

Normal traffic uses three independently domain-separated streams:

- uplink;
- downlink radio 1;
- downlink radio 2.

All three use one shared 64-bit RF-slot number. The slot advances on every ELRS
packet-timer interval, including packet loss, skipped transmission, telemetry
intervals, and LBT denial. This avoids the arrival-driven counter
desynchronization and cross-direction stream reuse present in the older design.

A receiver tests the current persistent slot offset and its immediate neighbors:
`{0, -1, +1}`. The window is deliberately narrow because a broad search against
a 14- or 16-bit OTA CRC would have an unacceptable false-lock rate.

### Session states and application-data gate

TX and RX expose three crypto states:

- `NONE`: no active session; plaintext acquisition/recovery SYNC and the public
  nonce proposal are allowed, but RC and application data are blocked.
- `PROPOSED`: session material is installed provisionally and proposal
  acknowledgement/activation is in progress; application data remains blocked.
- `FULL`: ordinary RC and application traffic is encrypted and may be delivered.

RX reaches `FULL` only after successfully decrypting an uplink packet with the
provisional session. TX reaches `FULL` after proposal acknowledgement and a
16-packet plaintext marked-SYNC activation barrier.

The application-data gates cover RC, CRSF, MAVLink, Airport, and receiver
Wi-Fi/MSP paths on both ends. This is a major security improvement: no ordinary
application byte is intended to cross before the session is active.

### FHSS and binding

The non-sync FHSS order is derived from the long-term secret using ChaCha output,
separate primary/dual-band domains, rejection sampling, and Fisher-Yates. The
regulatory channel set and fixed sync-channel position are preserved.

FHSS is deliberately keyed by the stable long-term secret rather than the
session key. A reboot or rekey therefore does not require a fragile hop-map
transition.

Explicit ELRS binding bypasses crypto transformations and crypto-control
scheduling. The stock 50 Hz/inverted-IQ UID exchange remains intact. After
binding, both ends still need the same PrivacyLRS secret and compatible
PrivacyLRS firmware to establish the encrypted session.

### Implementation map

The primary implementation and verification surfaces are:

| Area | Primary files |
| --- | --- |
| Crypto states, constants, and public crypto API | `src/include/encryption.h` |
| Slot-derived counters, stream domains, KDF, cipher initialization, key conversion | `src/src/common.cpp` |
| TX proposal, activation, encryption, recovery, and configuration barriers | `src/src/tx_main.cpp` |
| RX plaintext/decrypt classification, proposal handling, downlink crypto, expiry, and LinkStats | `src/src/rx_main.cpp` |
| Keyed FHSS generation | `src/lib/FHSS/` |
| OTA packet layout, CRC behavior, and `cryptoResync` bit | `src/lib/OTA/` |
| Build-secret derivation and flag redaction | `src/python/build_flags.py` |
| Band/rate behavior and TX status diagnostics | `src/lib/tx-crsf/TXModuleParameters.cpp` |
| Native-test source selection | `src/platformio.ini` |
| Historical encryption tests | `src/test/test_encryption/` |
| CI jobs and firmware matrix | `.github/workflows/build.yml` |
| Current design, evidence, and known limitations | `README.md`, `DEVLOG.md`, `NEXT.md` |

## What is already sound

The following decisions should be preserved during stabilization:

1. **Never transmit the session key.** The public-nonce proposal is smaller and
   removes the older wrapped-key exposure.
2. **Use the complete configured secret.** The earlier effective 48-bit master
   key was a serious defect and has been corrected.
3. **Keep directional and per-radio domains separate.** Loss on one path must
   not advance or reuse another path's keystream.
4. **Keep the slot window narrow.** Do not restore broad CRC-guided counter
   searches.
5. **Block all application data before `FULL`.**
6. **Keep binding as a stock protocol boundary.**
7. **Keep the keyed FHSS map stable across session recovery.**
8. **Preserve conservative rate selection on encrypted band changes.**
9. **Retain the bounded TX proposal retry and RX provisional-session expiry.**
   They reduce an unresolved first-attempt proposal wedge from ten-second stalls
   to bounded retries.
10. **Continue distinguishing designed, built, hardware-tested, and failed
    states in the development log.**

## Release-blocking findings

### 1. Ciphertext is interpreted as plaintext recovery DATA before decryption

In RX `ProcessRFPacket()`, while the crypto state is `FULL` or `PROPOSED`, the
raw radio buffer is copied and parsed as a plaintext OTA packet. Plaintext SYNC
and DATA branches are evaluated before `DecryptMsg()` is called.

For OTA4 ciphertext:

- random low type bits equal `PACKET_TYPE_DATA` with probability about `1/4`;
- a random packet satisfies the 14-bit OTA CRC with probability about `1/2^14`;
- the combined probability is approximately `1/65,536` per encrypted uplink
  packet.

This is not an RF corruption scenario. Correctly received ChaCha ciphertext is
supposed to look random. When the collision occurs in `FULL`, RX clears the live
session and routes the ciphertext into the plaintext reliable-DATA path.

At the observed 250 Hz / telemetry 1:2 configuration, approximately 125 valid RC
uplinks per second were delivered. The old branch therefore has a mean event
interval of roughly 8.7 minutes. Short hardware sweeps are unlikely to diagnose
the failure reliably, but normal operation can encounter it.

Required narrow correction:

- In `FULL`, accept plaintext proposal DATA only inside a short recovery window
  opened by a structurally valid marked recovery SYNC.
- Remove the convenience path that accepts proposal DATA when the preceding
  recovery SYNC was missed.
- Never destroy a live session because one raw packet merely resembles
  plaintext DATA.
- Add a deterministic production-path regression containing ciphertext that
  satisfies the plaintext DATA/CRC predicate and prove that the encrypted path
  wins.

Do not add authenticated recovery or redesign the OTA format in this phase.

### 2. Crypto slot and receive offsets have cross-context data races

`cryptoSlot` is a plain `uint64_t`. Timer callbacks increment it while packet
processing and loop/state transitions read or reset it. Receive offsets are also
read and updated across the crypto packet paths without a defined synchronization
model.

Unsynchronized conflicting access is a C++ data race. On 32-bit targets, a
64-bit read, write, or increment can also tear. A torn or lost slot value selects
the wrong ChaCha counter, so the consequence is packet rejection, false offset
movement, or session recovery—not harmless diagnostic noise.

Required narrow correction:

- Use the existing platform interrupt critical-section mechanism around slot
  snapshot, increment, and reset.
- Protect receive-offset reads and updates under the same ownership rule.
- Construct each packet counter from one protected local slot snapshot.
- Add a stress test for discontinuities and torn upper/lower words.

Do not introduce a general concurrency framework.

### 3. Native encryption tests do not exercise production encryption

The native PlatformIO environment excludes `common.*`, `tx_*.cpp`, and
`rx_*.cpp`. The encryption test defines its own simplified
`EncryptMsg()`/`DecryptMsg()` using the older eight-bit OTA-nonce counter model,
a five-entry search window, and an unconditional “assume CRC always passes”
shortcut.

It therefore does not test:

- the production 64-bit slot clock;
- the three stream domains;
- real OTA CRC candidate selection;
- plaintext/ciphertext classification;
- durable receive offsets;
- `NONE`/`PROPOSED`/`FULL` transitions;
- recovery-window behavior.

The existing CI does run the native test job, but it does not inject an
encryption secret and does not build representative encrypted firmware.
Automating the existing substitute test more often would preserve a false-green
signal.

Required correction:

- Compile the real production crypto in `common.cpp` into the native environment
  with minimal target stubs.
- Add only the smallest testable seam needed for the RX classification rule.
- Put each correctness regression in the same commit as its fix.
- Quarantine the historical `secure_01` vulnerability demonstrations so current
  pass counts describe current production behavior.
- Make CI build one encrypted TX target and one encrypted RX target with a fixed,
  public, CI-only test secret.
- Run the production-linked native suite in CI.

### 4. Release LinkStats can be overwritten by bench diagnostics

The committed checkpoint overwrites ordinary LinkStats fields with crypto state,
slot, nonce, and offset values under encrypted builds. This produces bogus RSSI,
SNR, antenna, power, and link-quality telemetry.

The correct `CRYPTO_BENCH_DIAGNOSTICS` guard already exists in the working tree.
It must be included in the release baseline, and the documentation must stop
claiming that the fix is complete before it is committed.

Bench diagnostics must remain opt-in and must explicitly warn that they replace
real link statistics.

### 5. Malformed key input can create undefined master-key material

The current runtime hex conversion:

- silently truncates odd-length strings;
- does not strictly reject non-hex characters;
- returns a converted length that callers ignore;
- can leave part of a 32-byte stack key uninitialized.

The normal build path generates valid input, but direct or malformed
`USE_ENCRYPTION` definitions can create nondeterministic keys.

Preferred correction:

- Have the build emit an exact 32-byte key initializer so no runtime hex parsing
  is involved in the key path.

Acceptable fallback:

- Require exactly 64 hexadecimal characters, validate them strictly, consume
  exactly 32 bytes, and test valid and invalid cases.

Do not build a general validation framework.

## Important bounded risks

These findings matter, but they do not justify expanding the first stabilization
phase unless a focused gate fails.

### CRC-guided adjacent-slot acceptance

When the correct keystream candidate is absent, decrypted candidates appear
random. For OTA4, testing three candidates with two accepted packet types and a
14-bit CRC gives an approximate false-acceptance probability:

```text
3 × (2 / 4) × (1 / 2^14) ≈ 1 / 10,923
```

For OTA8 the corresponding value is approximately 1 in 43,691.

This probability is conditional on operating without the correct candidate; it
does not apply to every healthy packet where the correct candidate succeeds
first. It matters during wrong-key traffic, sustained clock disagreement, or
other recovery faults. A false match currently may update the persistent receive
offset and may promote `PROPOSED` to `FULL`.

The stabilization work must quantify this behavior with the production code.
If testing shows a practical gate failure, separate “plausible candidate” from
“allowed to mutate durable state,” for example by requiring confirmation before
an adjacent-slot match changes the offset or proves a provisional session. Do not
change OTA framing or add a MAC unless this specific gate demonstrates that a
smaller guard is insufficient.

### Unacknowledged activation barrier

After proposal acknowledgement, TX sends 16 marked plaintext SYNC packets and
enters `FULL` without knowing whether RX received an anchor. RX provisional
expiry makes the failure self-healing, but activation remains open-loop.

Retain the current mechanism for the bounded checkpoint. Revisit it only if
hardware validation shows unsafe intermediate behavior or recovery outside the
declared proposal-phase limit.

### First-attempt proposal wedge

After some rate or band changes, the first reliable proposal attempt can wedge
while SYNC packets continue to validate. The micro-cause is not understood.
Separate TX retry and RX provisional-expiry timers bound the symptom and have
reduced observed establishment time substantially.

Do not begin an open-ended root-cause session before the release gates. Record
whether the wedge occurs during every disruptive hardware trial. It may remain a
documented, bounded limitation if:

- there is no unsafe intermediate state;
- proposal-start to first encrypted RC remains within the declared limit;
- no old ten-second proposal stall returns;
- no more than one proposal retry cycle is needed.

### Phase-detector input before full packet validation

RX now calls `PFDloop.extEvent()` for each hardware-CRC-valid arrival before
decryption and OTA-CRC validation. This keeps crypto processing time out of the
phase measurement but allows an undecryptable or invalid packet to perturb the
timing loop.

Document the tradeoff and observe it during recovery tests. Do not move it again
without timing evidence; it is in an ISR-sensitive path and can create another
hardware-only regression.

### Raw LinkStats slot-anchor contract

The final proposal acknowledgement stores a 16-bit slot anchor in the first two
bytes of `OTA_LinkStats_s` through raw byte casts. TX reads it the same way.
There is no named shared definition of this wire contract.

Give the anchor a shared named representation when touching this code, provided
the change remains mechanical and does not alter OTA layout. Otherwise document
the exact byte contract for the release candidate.

### Plaintext-build and artifact-identity ambiguity

If the encryption secret is absent, the current build can silently produce
ordinary plaintext ELRS while retaining an ELRS 4.0.1 identity. Different
PrivacyLRS checkpoints can also present the same visible version even when their
wire behavior is incompatible. This creates an operational failure mode in which
the wrong artifact is flashed or a matched pair fails without a useful
compatibility indication.

Do not add capability negotiation for this checkpoint. Phase 3 must instead
provide visible PrivacyLRS firmware and protocol identities, clearly named
encrypted TX/RX environments, and a separately named plaintext development
environment. A release artifact must make its encrypted or plaintext status
obvious before flashing.

### Deferred key and nonce hygiene

The 64-bit public session nonce relies on random uniqueness; a repeated nonce
under the same long-term secret repeats the derived session key and traffic
keystream domains. The birthday bound is around `2^32` sessions, so this is not a
practical blocker for the bounded release candidate, but it is a
protocol-lifetime constraint that must remain documented.

Long-term key material is currently supplied through compiler definitions.
Custom output redaction helps, but verbose compiler commands, process listings,
build caches, and artifacts can still retain it. Temporary master/session key
buffers are not wiped, and the unused key-logging escape hatch is safer removed
than retained.

For this checkpoint:

- document the 64-bit nonce lifetime limitation and lack of forward secrecy;
- minimize exposure and retention of the build secret;
- do not redesign nonce size, secret provisioning, or the KDF unless a release
  gate specifically requires it.

Removing the dead key-logging capability and wiping temporary key buffers are
reasonable local hygiene changes if they are mechanically reviewable while the
same code is already being touched. They are not independent release gates.

### K1000

K1000 reaches crypto `FULL` on the tested Nomad/XR1 pair but sustained delivery
does not reach the expected packet rate. The band-change fix prevents K1000 from
being selected implicitly.

K1000 is unsupported for the release candidate. Do not investigate it during
the stabilization program.

### CryptoTrace

The instrumentation is useful but not ready to become release code:

- the header calls the ring lock-free while the implementation uses critical
  sections;
- the ESP8266 interrupt disable/enable pair does not preserve the prior state;
- repeat-tracker state is updated outside the critical section;
- trace frames use normal telemetry routing and perturb the timing being measured.

Use tracing only after a hardware gate fails. It either lands with these defects
corrected or remains uncommitted.

## Hardware evidence at the reviewed checkpoint

The latest recorded Nomad X-Band TX / XR1 RX sweep used telemetry 1:2 and showed:

| Operation | Latest recorded result |
| --- | --- |
| Cold initial 2.4 GHz 150 Hz session | Pass in 8.0 s, including RX scan/acquisition |
| 2.4 GHz 150 to 250 Hz | Pass in 2.6 s |
| 2.4 GHz 250 Hz to 915 MHz 250 Hz | Pass in 4.3 s |
| 915 MHz 250 Hz to 2.4 GHz 250 Hz | Pass in 1.6 s |
| 2.4 GHz 250 to 150 Hz | Pass in 1.6 s |

The short measurement windows delivered 100% of expected RC frames with no
reported dropouts. This is useful evidence for acquisition and transitions, but
it does not cover the newly identified probabilistic parser defect, concurrency,
one-sided reboot, short/long outage recovery, or sustained operation.

## Agreed execution plan

### Phase 1 — Firmware correctness

Use six small, independently reviewable changes in this order. Each behavioral
fix carries its pinning tests in the same change.

1. **Commit the diagnostic gate.**
   Include the working-tree RX `getRFlinkInfo()` gating behind
   `CRYPTO_BENCH_DIAGNOSTICS`. Correct README and DEVLOG statements that
   currently overstate the cleanup.

2. **Fix ciphertext/plaintext classification.**
   In `FULL`, accept plaintext proposal DATA only within the bounded recovery
   window opened by a structurally valid marked recovery SYNC. Never tear down a
   live session on one plaintext-looking packet. Add the deterministic
   production-path collision regression.

3. **Synchronize crypto slot state.**
   Protect slot snapshot/increment/reset and receive-offset updates with platform
   critical sections. Build each packet counter from one protected snapshot.
   Add the discontinuity/torn-value stress test.

4. **Make key material exact.**
   Prefer a build-generated 32-byte initializer. Otherwise use strict
   64-hex-character parsing with a checked 32-byte result. Add parsing/build
   contract tests.

5. **Make native tests representative.**
   Compile real `common.cpp` crypto with minimal stubs. Add KDF/domain-constant
   tests and a production-code Monte Carlo measurement of the `{0,-1,+1}`
   false-accept behavior. Quarantine obsolete `secure_01` demonstrations.

6. **Make CI representative.**
   Build one encrypted TX and one encrypted RX target with a fixed public test
   secret and run the production-linked suite.

Phase 1 completion requires:

- all production-linked native tests passing;
- both representative encrypted firmware targets building;
- no bench diagnostic fields present without the explicit diagnostic flag;
- exact deterministic key material;
- the F-01 collision regression proving decryption takes precedence.

### Phase 2 — Bounded hardware validation

Declare and test only this surface:

- RadioMaster Nomad X-Band TX;
- RadioMaster XR1 RX;
- 2.4 GHz at 150 and 250 Hz;
- 915 MHz at 250 Hz;
- telemetry ratio 1:2.

K1000, Gemini, other hardware targets, and other rates are not part of this
release candidate.

Run these disruptive cases three times each:

1. cold start;
2. interruption shorter than ten seconds;
3. outage longer than ten seconds;
4. TX-only reboot;
5. RX-only reboot;
6. 150 ↔ 250 Hz rate change;
7. 2.4 ↔ 915 MHz band round trip.

For every trial record:

- total event-to-valid-RC time;
- RF reacquisition time where distinguishable;
- proposal-start to first encrypted valid RC;
- whether the first-attempt proposal wedge occurred;
- proposal retry count;
- whether any plaintext application data crossed;
- whether LinkStats retained their ordinary meanings.

Acceptance:

- proposal-start to first encrypted valid RC is no more than 4 seconds after RF
  reacquisition;
- no more than one proposal retry cycle;
- no reappearance of the old ten-second proposal stall;
- no unsafe intermediate state;
- no plaintext RC or application delivery;
- cold scan/acquisition is reported separately and remains within the existing
  10-second checkpoint envelope;
- no deterministic or safety-relevant failure across the three repeats.

Do not enable `CryptoTrace` initially. If a gate fails, enable only the minimum
instrumentation needed to investigate that gate.

After the recovery matrix passes, run one 60-minute soak at 2.4 GHz 250 Hz,
telemetry 1:2. At the measured 125 valid RC uplinks per second, this covers about
6.9 former F-01 mean collision intervals. Under the old bug, the probability of
zero events over that duration would be approximately 0.1%.

Soak acceptance:

- zero crypto-session resets or re-establishments;
- zero plaintext application delivery;
- continuous valid RC at the measured expected cadence;
- ordinary LinkStats for the complete run.

The deterministic regression is the proof of the F-01 correction. The soak is
integration confidence.

### Phase 3 — Release packaging only

After Phases 1 and 2 pass, make no additional protocol changes. Package one
release candidate with:

- a visible PrivacyLRS firmware identity;
- a compile-time PrivacyLRS protocol revision string that makes mismatched
  checkpoints diagnosable;
- named encrypted build environments;
- a separately and explicitly named plaintext development environment;
- unmistakable encrypted/plaintext identity in release artifact names and
  displayed firmware information;
- an exact support table for the tested hardware, bands, rates, and telemetry
  ratio;
- explicit unsupported status for K1000, Gemini, active-attack resistance,
  seamless transitions, and untested hardware/rates;
- accurate documentation of the ChaCha-based KDF and ChaCha variant;
- documentation of the 64-bit session-nonce lifetime constraint, compiler/build
  secret exposure, and lack of forward secrecy;
- a requirement for a generated high-entropy secret;
- a known-limitations entry for the bounded first-attempt proposal wedge,
  including observed occurrence data;
- corrected test counts and descriptions;
- removal of dangling references and committed workstation-specific absolute
  paths;
- `CryptoTrace` omitted unless its synchronization and documentation defects are
  fixed;
- one tagged release candidate built from the validated checkpoint.

## Frozen scope

The following work is frozen unless a specific release gate fails and evidence
shows that the item is necessary to correct that failure:

- AEAD or per-packet/group MAC;
- HKDF replacement;
- capability negotiation;
- generalized crypto state-machine restructuring;
- K1000 support;
- Gemini support;
- password-KDF stretching;
- seamless live band/rate transitions;
- broad new hardware or rate support;
- unrelated cleanup or feature work in ISR-sensitive paths.

This freeze is intentional. The project history shows that large recovery and
timing changes can look correct in source and fail immediately on hardware.

## Stop rule

If every gate passes:

1. package and tag the release candidate;
2. publish the exact support and limitation table;
3. stop.

If a gate fails:

1. investigate only that gate;
2. make the smallest correction that directly addresses the evidence;
3. rerun the affected gate and any immediately dependent checks;
4. do not begin adjacent feature work.

## Final assessment

PrivacyLRS does not need more features to become useful. It needs fewer ambiguous
paths, deterministic shared state, tests that execute production code, and a
small hardware claim supported by repeatable evidence.

The existing cryptographic direction is appropriate for passive-observer
privacy. The release risk comes primarily from packet classification,
cross-context state, misleading test coverage, and operational identity—not
from a need to invent a more elaborate protocol.

Following the three phases above produces a credible and explicitly limited
PrivacyLRS release candidate while minimizing new code, hardware-debugging time,
and opportunities for another unverified recovery redesign.
