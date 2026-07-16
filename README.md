
<center>

# PrivacyLRS
PrivacyLRS is a privacy-protecting fork of the excellent [ExpressLRS (ELRS)](https://www.expresslrs.org/) long-range RC system.
PrivacyLRS is for those who want to use telemetry, but not broadcast their GPS location and other telemetry for 
other people to read.

With *standard* ELRS, it is inconvenient for others to read your telemetry link - they would need to work for it a little bit.
PrivacyLRS uses strong encryption to make it *impossible* for other people to read your telemetry and get your location
that way, among other information.

RC commands are also encrypted.

## FAQ
### How do the performance and features compare?  
  Performance and features of PrivacyLRS are identical for the same version number of ELRS, because PrivacyLRS is the exact same code as ELRS - just with the packets encrypted.
  The encryption is much, much faster than the radio link, so there is no effectively no delay.
  To put that into hard numbers, at at 250 Hz, that's a minimum of 4 milliseconds between packets (without encryption)
  ● Time between packets at 250 Hz:
  - Frequency = 250 packets/second
  - Time per packet = 1 / 250 = 4 milliseconds

  ChaCha20 encryption Encryption time: 0.00352 milliseconds
  - Percentage: (0.00352 / 4) × 100 = 0.088%

  So you have 4 milliseconds between each packet, and ChaCha20 encryption only uses 0.00352 milliseconds of that time, which is 0.088% (less
  than 1/10th of 1 percent).


### How do I use PrivacyLRS?  
  Download the [zip file](https://github.com/sensei-hacker/PrivacyLRS/archive/refs/heads/secure_01.zip) of the secure branch.
  Unzip it, then flash using ELRS Configurator by choosing "Local" as shown in this screenshot:
  ![image of local tab in Configurator](https://raw.githubusercontent.com/sensei-hacker/PrivacyLRS/secure_01/src/privacylrs/screenshot_choose_local.png)

### Is this a fork because somebody got mad?  
  The maintainer of PrivacyLRS has nothing but love for ExpressLRS and the ELRS maintainers. I simply wanted a version that protects my privacy.

## Status of the project and testing
PrivacyLRS is currently in beta - testers needed. What needs testing is weird corner cases that could possibly break the
link, by making the encryption get out of sync.  Performance (range, refresh rate, etc) is exactly the same as standard ELRS,
so there is no point in testing that. Though you are welcome to if you wish.
\* If standard ELRS has latency of 6.522ms at a certain packet rate, PrivacyLRS will have latency of around 6.525ms - the
the difference probably being too small to measure.
If you choose to test PrivacyLRS, be sure to set up your failsafe carefully. Bugs could cause the link to fail.

## Differences between ELRS and PrivacyLRS
The ELRS documentation makes clear that the binding phrase is *not* a security feature in standard ELRS.
In standard ELRS, the binding phrase is to prevent *accidental* conflicts between two aircraft.
This is different in PrivacyLRS. In PrivacyLRS, the bind phrase is used as a small part of the security.
For this reason, it is recommended to make your bind phrase three or four words long - four words that other people are unlikely
to guess.
PrivacyLRS uses strong cryptographic keys which are randomly generated, but the bind phrase also plays small part in
security.


## About ExpressLRS


ExpressLRS is an open source Radio Link for Radio Control applications. Designed to be the best FPV Racing link, it is based on the fantastic Semtech **SX127x**/**SX1280** LoRa hardware combined with an Espressif or STM32 Processor. Using LoRa modulation as well as reduced packet size it achieves best in class range and latency. It achieves this using a highly optimized over-the-air packet structure, giving simultaneous range and latency advantages. It supports both 900 MHz and 2.4 GHz links [also 433 and 868], each with their own benefits. 900 MHz supports a maximum of 200 Hz packet rate, with higher penetration. 2.4 GHz supports a blistering fast 1000 Hz on [EdgeTX](http://edgetx.org/). With over 60 different hardware targets and 13 hardware manufacturers, the choice of hardware is ever growing, with different hardware suited to different requirements.


## Configurator
To configure your ExpressLRS / PrivacyLRS hardware, the ExpressLRS Configurator can be used, which is found here:

https://github.com/ExpressLRS/ExpressLRS-Configurator/releases/


## Changelog — ExpressLRS 4.0.1 port (branch `secure_4.0.1`, 2026-07-16)

This branch rebases PrivacyLRS from its previous ExpressLRS 3.5.3 base onto upstream
**ExpressLRS 4.0.1**. It intentionally stays close to the original `secure_01` design:
beyond the port itself, only critical bug fixes are included. It is
**not over-the-air compatible with any previous PrivacyLRS build** — reflash both TX and RX.

### Port to ExpressLRS 4.0.1
- Merged upstream tag `4.0.1` (527 files changed upstream, including removal of STM32 support
  and a redesigned telemetry/uplink data path).
- Adapted the encryption hooks to renamed 4.0.1 internals: `MspSender`→`DataUlSender`,
  `MspData`→`DataUlBuffer`, `HandleSendTelemetryResponse`→`HandleSendDataDl`,
  `ProcessTLMpacket`→`ProcessDownlinkPacket`, `ELRS_MSP_BUFFER`→`ELRS_DATA_UL_BUFFER`.
- New in 4.0.1 and now covered by encryption: Gemini/dual-band dual-packet transmit
  (both packets encrypted) and the second-radio receive buffer used by true-diversity and
  dual-band receivers (decrypted before CRC validation on both TX and RX sides).
- Verified on the Radiomaster XR1 Dual Band RX target (`Unified_ESP32C3_LR1121_RX`) plus
  ESP32 LR1121/2400/900 TX targets.

### Bug fixes
- **ChaCha20 for real:** the previous "ChaCha20 upgrade" set the constructor to 20 rounds
  but `InitCrypto()`/`CryptoSetKeys()` still called `setNumRounds(12)`, silently downgrading
  the link to ChaCha12 at runtime. Now genuinely RFC 8439 ChaCha20. This is the change that
  breaks OTA compatibility with earlier PrivacyLRS builds.
- `DecryptMsg()` validated full-resolution packets through an **uninitialized pointer**
  (only the OTA4 branch set it) — would have crashed LR1121 full-res rates with encryption on.
- `RandRSSI()` on SX1280/LR1121 used the old `Radio.RXnb(mode)` signature, which no longer
  exists in 4.0.1 — broke all encrypted TX builds on those radios (RX-only builds never
  compile that path).

### Notes
- Removed dead code: unused `GetRandomBytes()`/`GetRandom32t()`, the unused
  `ELRS_TELEMETRY_TYPE_ENCRYPTION` define, and STM32-era `ICACHE_RAM_ATTR1/2` shims.
- Known/expected: 2 of 24 native encryption tests (`test_single_packet_loss_desync`,
  `test_burst_packet_loss_exceeds_resync`) fail by design — they demonstrate the raw
  stream-cipher desync vulnerability without the resync logic; the integration tests that
  exercise the actual recovery path all pass. The native suite silently skips (reported as
  SIGHUP/ERRORED) unless a `MY_BINDING_PHRASE` is set, e.g. in `src/super_defines.txt`.
- Further encryption hardening and protocol work continues on the `secure_02` branch.

