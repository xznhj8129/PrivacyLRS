# PrivacyLRS

PrivacyLRS is a privacy-protecting fork of [ExpressLRS (ELRS)](https://www.expresslrs.org/), the long-range RC link. It protects RC commands, CRSF telemetry, and MAVLink telemetry—including GPS location—from passive RF observation.

## Release identity

This README documents **PrivacyLRS 1.0**, based on **ExpressLRS 4.0.1**. It is not over-the-air compatible with earlier PrivacyLRS builds: reflash both TX and RX together.

PrivacyLRS has no independent release tags or firmware version field yet. The repository inherits ExpressLRS tags, this branch is `secure_4.0.1`, and the firmware identifies itself with the upstream ELRS version. “PrivacyLRS 1.0” is therefore the release identity used by this documentation, not a second version string shown by the firmware.

## Purpose and security model

The goal is privacy: someone who records RF traffic but does not know the configured secret should not be able to read RC activity or telemetry. PrivacyLRS preserves ELRS packet sizes, packet rates, latency, radio behavior, and CRSF purpose. It is not intended to be an electronic-attack-resistant control link.

Use a generated secret or a high-entropy Diceware-style binding phrase. A memorable or predictable phrase permits offline guessing against recorded session setup traffic.

## Crypto

### Session establishment

The configured binding phrase is hashed in full with SHA-256 to form the long-term secret. On every connection and recovery, the TX generates a fresh 256-bit session key and 64-bit nonce. Entropy combines radio RSSI noise with the SoC hardware random-number generator.

The TX sends the nonce and a ChaCha20-encrypted session key through the existing reliable uplink-data path. The RX decrypts that key with the configured secret and acknowledges the proposal with its first LinkStats response. Normal encrypted traffic begins only after that acknowledgement.

### Cryptographic frequency hopping

The hop sequence is a keyed ChaCha20 CSPRNG output, derived from the long-term secret with a dedicated FHSS nonce domain. Primary and dual-band sequences have different domain values. Each frequency block is a Fisher-Yates permutation: every regulatory-domain channel occurs once, while the existing sync-channel position remains fixed at the start of each block.

This hides the non-sync hop schedule from a passive observer while preserving ELRS acquisition and recovery behavior. The hop map is deliberately stable across session rekeys and resets; using the ephemeral session key here would make cold recovery depend on a fragile schedule-transition protocol.

### Normal traffic

RC/uplink and telemetry/downlink use independent ChaCha20 streams. Each stream has its own nonce domain and its own 64-bit counter:

- Uplink uses the session nonce with one domain value; downlink uses the same session nonce with a different domain value.
- A packet advances only its own direction's counter.
- A receiver accepts its expected counter or a small forward window to tolerate lost packets. A larger gap starts recovery.

This prevents a lost telemetry packet from making a later RC packet reuse its ChaCha20 keystream. The OTA CRC remains in the encrypted packet: it detects accidental corruption and identifies a plausible counter position, but it is not a cryptographic authenticator.

### Recovery

A packet-rate/configuration change, failed decryption, crypto counter gap, telemetry timeout, or reset tears down the active session. During recovery:

1. The TX sends marked plaintext recovery SYNC packets and the encrypted session-key proposal. It never sends plaintext RC.
2. The RX accepts the binding-checked recovery SYNC, resets reliable-transfer state, and sends only plaintext LinkStats until it has acknowledged the proposal.
3. Both sides enable the new directional ChaCha20 streams only after the acknowledgement.

No application telemetry crosses the link until the session is active again.

### Deliberate compromises

- Packets are encrypted but not cryptographically authenticated. CRC is error detection, not a MAC.
- An active attacker can jam, disrupt, replay, or potentially manipulate traffic. Recovery SYNC is an acquisition signal, not authenticated control.
- There is no forward secrecy: compromise of the long-term secret can expose recorded session setup and traffic.
- The fixed ELRS OTA frame has no space for an AEAD tag. Authenticated control would require a new on-air protocol.

These are intentional boundaries. PrivacyLRS is for passive-observer privacy, not resistance to an active RF adversary.

## Improvements from PrivacyLRS 1.0

There are none in this initial release. Future releases should list their changes from PrivacyLRS 1.0 in this section.

## Improvements from ELRS

The intentional divergence from ExpressLRS 4.0.1 is the crypto layer described above:

- 256-bit ChaCha20 session encryption for normal RC and telemetry traffic.
- A 256-bit long-term key derived from the configured secret, fresh session keys, and mixed hardware/RSSI entropy.
- ChaCha20-keyed FHSS permutations with separate primary/dual-band domains and the existing fixed sync-channel structure.
- Independent uplink and downlink nonce domains and counters.
- Automatic encrypted-session recovery without plaintext RC or application telemetry.

Apart from that layer, PrivacyLRS 1.0 deliberately carries ELRS 4.0.1 behavior forward. There are no additional flight features, radio-mode changes, CRSF extensions, or live cross-band-handoff changes in this fork.

## Compatibility and testing

Build and configure TX and RX with the same secret. Their cryptographic hop maps make this build intentionally incompatible with stock ELRS or a PrivacyLRS device built with another secret. Use a carefully configured flight-controller failsafe while testing: a firmware defect or any RF outage can still cause loss of control.

The priority test matrix is recovery after single and burst packet loss, one-sided reset, telemetry-slot loss, packet-rate change, and prolonged outage. Verify that RC and application telemetry remain absent until the encrypted session is active. The native encryption and FHSS suites currently have 30 passing tests; hardware recovery tests remain essential.

## Building and configuring

Use ExpressLRS Configurator with the **Local** source option and select this repository's `src` directory. Choose the correct TX/RX targets, regulatory domain, and matching secret before flashing both devices.

### Lua script when building locally

For a Local source, Configurator's **Download Lua** action opens the download menu but does not save the Lua file. Copy [elrs.lua](src/lua/elrs.lua) to `SCRIPTS/TOOLS` on the handset manually.

## About ExpressLRS

PrivacyLRS is based on ExpressLRS 4.0.1. ExpressLRS supplies the radio link, target support, packet formats, telemetry transport, and configuration system; PrivacyLRS is grateful to its maintainers and contributors.
