#pragma once

// Session-establishment stage tracing (opt-in via -DCRYPTO_ESTABLISH_TRACE).
//
// Stage events are pushed into a small lock-free ring from ISR or loop
// context and drained in loop() as CRSF FLIGHT_MODE text frames delivered
// through the normal telemetry path (handset port on TX, FC port on RX).
// The bench CRSF logger prints them verbatim, one line per event:
//
//   CT <STAGE> a=<arg> t=<device ms> #<seq>
//
// This exists to make one establishment attempt reconstructable end to end;
// it is not compiled into normal builds.

#if defined(USE_ENCRYPTION) && defined(CRYPTO_ESTABLISH_TRACE)

#include <stdint.h>
#include "targets.h"

typedef enum : uint8_t
{
    CTRACE_BOOT = 0,
    CTRACE_SESSION_RESET,               // arg = ctrace_reset_reason_e
    CTRACE_CONNECTED,
    CTRACE_TENTATIVE,
    CTRACE_DISCONNECTED,
    CTRACE_MARKED_SYNC_QUEUED,          // TX, arg = OTA nonce
    CTRACE_MARKED_SYNC_SENT,            // TX, arg = OTA nonce anchored
    CTRACE_PROPOSAL_CREATED,            // TX, arg = first two public nonce bytes
    CTRACE_PROPOSAL_FRAG_SENT,          // TX, arg = stubborn package index
    CTRACE_PROPOSAL_FRAG_STALL,         // TX, arg = (sends << 8) | package index
    CTRACE_SLOT_RESET_FROM_ACK,         // TX, arg = received 16-bit anchor
    CTRACE_PROPOSAL_FINAL_ACK_RECEIVED, // TX, arg = anchor
    CTRACE_ACTIVATION_BARRIER_STARTED,  // TX
    CTRACE_ACTIVATION_SYNC_TRANSMITTED, // TX, arg = barrier SYNCs remaining
    CTRACE_ENTERED_FULL,                // arg = low 16 bits of crypto slot
    CTRACE_FIRST_ENCRYPTED_PACKET_SENT, // TX, arg = low 16 bits of crypto slot
    CTRACE_FIRST_ENCRYPTED_DOWNLINK,    // TX
    CTRACE_FIRST_VALID_SYNC,            // RX, arg = sync nonce
    CTRACE_MARKED_SYNC_RECEIVED,        // RX, arg = sync nonce
    CTRACE_ACTIVATION_SYNC_RECEIVED,    // RX, arg = count while PROPOSED
    CTRACE_PROPOSAL_FRAG_RECEIVED,      // RX, arg = package index
    CTRACE_PROPOSAL_FRAG_REPEAT,        // RX, arg = (repeats << 8) | package index
    CTRACE_DUP_PROPOSAL_RECEIVED,       // RX
    CTRACE_PROPOSAL_COMPLETE,           // RX
    CTRACE_SESSION_KEY_INSTALLED,       // RX
    CTRACE_FINAL_ACK_SENT,              // RX, arg = 16-bit anchor
    CTRACE_FINAL_ACK_RETX,              // RX, arg = retransmission count
    CTRACE_CONTROL_ACK_SENT,            // RX, arg = fragment-ack count
    CTRACE_FIRST_DECRYPT_ATTEMPT,       // RX
    CTRACE_INITIAL_DECRYPT_OFFSET,      // RX, arg = signed slot offset
    CTRACE_FIRST_RC_DELIVERED,          // RX
    CTRACE_EVENT_COUNT
} ctrace_event_e;

static_assert(CTRACE_EVENT_COUNT <= 32, "once-per-attempt mask is 32 bits");

typedef enum : uint8_t
{
    CTRACE_RESET_PROPOSAL_TIMEOUT = 0,
    CTRACE_RESET_CONFIGURATION_CHANGE,
    CTRACE_RESET_FORCED_RECOVERY_SYNC,
    CTRACE_RESET_PLAINTEXT_RECOVERY_PACKET,
    CTRACE_RESET_CONNECTION_STATE_CHANGE,
    CTRACE_RESET_STALE_PROPOSED_TIMEOUT,
    CTRACE_RESET_BINDING,
    CTRACE_RESET_UART_CONNECTED,
    CTRACE_RESET_INIT_FAILED,
    CTRACE_RESET_REASON_COUNT
} ctrace_reset_reason_e;

class CRSFConnector;

void CryptoTraceInit(CRSFConnector *otaConnector);
void CryptoTraceFlush();
void ICACHE_RAM_ATTR CryptoTraceEvent(ctrace_event_e ev, uint16_t arg = 0);
void ICACHE_RAM_ATTR CryptoTraceEventOnce(ctrace_event_e ev, uint16_t arg = 0);
void ICACHE_RAM_ATTR CryptoTraceAttemptReset(ctrace_reset_reason_e reason);
// Rate-limited repeat trackers, reset by CryptoTraceAttemptReset()
void ICACHE_RAM_ATTR CryptoTraceProposalFragSent(uint8_t packageIndex);
void ICACHE_RAM_ATTR CryptoTraceProposalFragRecv(uint8_t packageIndex);
void ICACHE_RAM_ATTR CryptoTraceActivationSyncRecv();
void ICACHE_RAM_ATTR CryptoTraceControlAckSent();
void ICACHE_RAM_ATTR CryptoTraceFinalAckRetx();

#define CTRACE(ev, ...)         CryptoTraceEvent(CTRACE_##ev, ##__VA_ARGS__)
#define CTRACE_ONCE(ev, ...)    CryptoTraceEventOnce(CTRACE_##ev, ##__VA_ARGS__)
#define CTRACE_RESET(reason)    CryptoTraceAttemptReset(CTRACE_RESET_##reason)
#define CTRACE_INIT(conn)       CryptoTraceInit(conn)
#define CTRACE_FLUSH()          CryptoTraceFlush()
#define CTRACE_FRAG_SENT(idx)   CryptoTraceProposalFragSent(idx)
#define CTRACE_FRAG_RECV(idx)   CryptoTraceProposalFragRecv(idx)
#define CTRACE_ACT_SYNC_RECV()  CryptoTraceActivationSyncRecv()
#define CTRACE_CTRL_ACK()       CryptoTraceControlAckSent()
#define CTRACE_ACK_RETX()       CryptoTraceFinalAckRetx()

#else

#define CTRACE(ev, ...)
#define CTRACE_ONCE(ev, ...)
#define CTRACE_RESET(reason)
#define CTRACE_INIT(conn)
#define CTRACE_FLUSH()
#define CTRACE_FRAG_SENT(idx)
#define CTRACE_FRAG_RECV(idx)
#define CTRACE_ACT_SYNC_RECV()
#define CTRACE_CTRL_ACK()
#define CTRACE_ACK_RETX()

#endif
