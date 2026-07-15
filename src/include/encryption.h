#pragma once

#ifdef USE_ENCRYPTION

#include <climits>
#include "targets.h"

#define stringify_literal(x) # x
#define stringify_expanded(x) stringify_literal(x)
#define CRYPTO_SHORT_LOSS_GRACE_MS 10000U
#define CRYPTO_CONFIG_TRANSITION_SYNC_MS 500U

// DBGLN_KEY() - Secure logging for cryptographic keys
//
// This macro is disabled by default (production safe).
// Keys are NEVER logged unless explicitly enabled via build flag.
//
// To enable (debugging only):
//   pio run -e <target> -DALLOW_KEY_LOGGING=1
//
// WARNING: NEVER enable in production builds!
// Logged keys can compromise the entire encryption system.
//
// Usage:
//   DBGLN_KEY("session_key = %x %x %x", key[0], key[1], key[2]);
//
#ifdef ALLOW_KEY_LOGGING
  // WARNING: This enables cryptographic key logging for debugging
  // NEVER use in production builds - keys will be visible in logs!
  #define DBGLN_KEY(...) DBGLN(__VA_ARGS__)
  #warning "CRYPTOGRAPHIC KEY LOGGING ENABLED - DO NOT USE IN PRODUCTION!"
#else
  // Production default: keys never logged
  #define DBGLN_KEY(...) ((void)0)
#endif

typedef enum : uint8_t {
	ENCRYPTION_STATE_NONE,
  ENCRYPTION_STATE_PROPOSED,
  ENCRYPTION_STATE_FULL,
	ENCRYPTION_STATE_DISABLED
} encryptionState_e;

typedef struct session_proposal_s
{
    uint8_t nonce[8];  // Public session nonce; the 256-bit key is derived locally.
} session_proposal_t;

bool ICACHE_RAM_ATTR DecryptMsg(uint8_t *input);
void ICACHE_RAM_ATTR EncryptMsg(uint8_t *output, uint8_t *input);
bool ICACHE_RAM_ATTR DecryptMsgForRadio(uint8_t *input, bool radio2);
void ICACHE_RAM_ATTR EncryptMsgForRadio(uint8_t *output, uint8_t *input, bool radio2);
bool DeriveSessionKey(uint8_t const *masterKey, uint8_t const *nonce, uint8_t *sessionKey);
bool InitSessionCiphers(uint8_t const *key, uint8_t const *nonce);
void ICACHE_RAM_ATTR CryptoAdvanceSlot();
void ICACHE_RAM_ATTR CryptoResetSlot(uint64_t slot);
uint64_t ICACHE_RAM_ATTR CryptoGetSlot();
uint8_t ICACHE_RAM_ATTR CryptoGetLastResetNonce();
int32_t ICACHE_RAM_ATTR CryptoGetReceiveOffsetRadio1();

/// in: valid chars are 0-9 + A-F + a-f
/// out_len_max==0: convert until the end of input string, out_len_max>0 only convert this many numbers
/// returns actual out size
int hexStr2Arr(unsigned char* out, const char* in, size_t out_len_max = 0);

#endif
