#include "CryptoTrace.h"

#if defined(USE_ENCRYPTION) && defined(CRYPTO_ESTABLISH_TRACE)

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include "CRSFRouter.h"

// ESP32-C3 is RV32IMC: no atomic ISA, so __atomic builtins become flash-resident
// libatomic calls that are not ISR-safe. Serialize the ring with a critical
// section instead; it is a handful of cycles and safe from ISR and task context.
#if defined(PLATFORM_ESP8266)
#define CTRACE_LOCK()   noInterrupts()
#define CTRACE_UNLOCK() interrupts()
#else
static portMUX_TYPE ctraceMux = portMUX_INITIALIZER_UNLOCKED;
#define CTRACE_LOCK()   portENTER_CRITICAL_SAFE(&ctraceMux)
#define CTRACE_UNLOCK() portEXIT_CRITICAL_SAFE(&ctraceMux)
#endif

static const char *const eventNames[CTRACE_EVENT_COUNT] = {
    "BOOT",
    "SESSION_RESET",
    "CONNECTED",
    "TENTATIVE",
    "DISCONNECTED",
    "MARKED_SYNC_QUEUED",
    "MARKED_SYNC_SENT",
    "PROPOSAL_CREATED",
    "PROPOSAL_FRAG_SENT",
    "PROPOSAL_FRAG_STALL",
    "SLOT_RESET_FROM_ACK",
    "PROPOSAL_FINAL_ACK_RECEIVED",
    "ACTIVATION_BARRIER_STARTED",
    "ACTIVATION_SYNC_TRANSMITTED",
    "ENTERED_FULL",
    "FIRST_ENCRYPTED_PACKET_SENT",
    "FIRST_ENCRYPTED_DOWNLINK",
    "FIRST_VALID_SYNC",
    "MARKED_SYNC_RECEIVED",
    "ACTIVATION_SYNC_RECEIVED",
    "PROPOSAL_FRAG_RECEIVED",
    "PROPOSAL_FRAG_REPEAT",
    "DUP_PROPOSAL_RECEIVED",
    "PROPOSAL_COMPLETE",
    "SESSION_KEY_INSTALLED",
    "FINAL_ACK_SENT",
    "FINAL_ACK_RETX",
    "CONTROL_ACK_SENT",
    "FIRST_DECRYPT_ATTEMPT",
    "INITIAL_DECRYPT_OFFSET",
    "FIRST_RC_DELIVERED",
};

static const char *const resetReasonNames[CTRACE_RESET_REASON_COUNT] = {
    "PROPOSAL_TIMEOUT",
    "CONFIGURATION_CHANGE",
    "FORCED_RECOVERY_SYNC",
    "PLAINTEXT_RECOVERY_PACKET",
    "CONNECTION_STATE_CHANGE",
    "STALE_PROPOSED_TIMEOUT",
    "BINDING",
    "UART_CONNECTED",
    "INIT_FAILED",
};

typedef struct
{
    uint32_t ms;
    uint16_t arg;
    uint8_t ev;
} ctrace_entry_t;

constexpr uint32_t CTRACE_RING_SIZE = 96;
constexpr uint8_t CTRACE_FLUSH_MAX = 4; // frames per loop() call
constexpr uint16_t CTRACE_STALL_PERIOD = 64;

static ctrace_entry_t ring[CTRACE_RING_SIZE];
static volatile uint32_t ringHead;
static uint32_t ringTail;
static uint32_t ringLost;
static volatile uint32_t onceMask;
static CRSFConnector *traceOrigin;

// Repeat-tracker state, cleared on CryptoTraceAttemptReset()
static volatile uint8_t fragSentLastIdx = 0xFF;
static volatile uint16_t fragSentCount;
static volatile uint8_t fragRecvLastIdx = 0xFF;
static volatile uint16_t fragRecvCount;
static volatile uint16_t actSyncRecvCount;
static volatile uint16_t ctrlAckCount;
static volatile uint16_t finalAckRetxCount;

void ICACHE_RAM_ATTR CryptoTraceEvent(ctrace_event_e ev, uint16_t arg)
{
    const uint32_t ms = millis();
    CTRACE_LOCK();
    const uint32_t slot = ringHead;
    ringHead = slot + 1;
    ctrace_entry_t *entry = &ring[slot % CTRACE_RING_SIZE];
    entry->ms = ms;
    entry->arg = arg;
    entry->ev = ev;
    CTRACE_UNLOCK();
}

void ICACHE_RAM_ATTR CryptoTraceEventOnce(ctrace_event_e ev, uint16_t arg)
{
    const uint32_t bit = 1U << ev;
    bool first;
    CTRACE_LOCK();
    first = (onceMask & bit) == 0;
    onceMask |= bit;
    CTRACE_UNLOCK();
    if (first)
    {
        CryptoTraceEvent(ev, arg);
    }
}

void ICACHE_RAM_ATTR CryptoTraceAttemptReset(ctrace_reset_reason_e reason)
{
    CTRACE_LOCK();
    onceMask = 0;
    CTRACE_UNLOCK();
    fragSentLastIdx = 0xFF;
    fragSentCount = 0;
    fragRecvLastIdx = 0xFF;
    fragRecvCount = 0;
    actSyncRecvCount = 0;
    ctrlAckCount = 0;
    finalAckRetxCount = 0;
    CryptoTraceEvent(CTRACE_SESSION_RESET, reason);
}

void ICACHE_RAM_ATTR CryptoTraceProposalFragSent(uint8_t packageIndex)
{
    if (packageIndex != fragSentLastIdx)
    {
        fragSentLastIdx = packageIndex;
        fragSentCount = 1;
        CryptoTraceEvent(CTRACE_PROPOSAL_FRAG_SENT, packageIndex);
        return;
    }
    fragSentCount = fragSentCount + 1;
    if (fragSentCount % CTRACE_STALL_PERIOD == 0)
    {
        const uint16_t sends = fragSentCount > 255 ? 255 : fragSentCount;
        CryptoTraceEvent(CTRACE_PROPOSAL_FRAG_STALL, (sends << 8) | packageIndex);
    }
}

void ICACHE_RAM_ATTR CryptoTraceProposalFragRecv(uint8_t packageIndex)
{
    if (packageIndex != fragRecvLastIdx)
    {
        fragRecvLastIdx = packageIndex;
        fragRecvCount = 1;
        CryptoTraceEvent(CTRACE_PROPOSAL_FRAG_RECEIVED, packageIndex);
        return;
    }
    fragRecvCount = fragRecvCount + 1;
    if (fragRecvCount % CTRACE_STALL_PERIOD == 0)
    {
        const uint16_t repeats = fragRecvCount > 255 ? 255 : fragRecvCount;
        CryptoTraceEvent(CTRACE_PROPOSAL_FRAG_REPEAT, (repeats << 8) | packageIndex);
    }
}

void ICACHE_RAM_ATTR CryptoTraceActivationSyncRecv()
{
    const uint16_t count = actSyncRecvCount + 1;
    actSyncRecvCount = count;
    if (count <= 32)
    {
        CryptoTraceEvent(CTRACE_ACTIVATION_SYNC_RECEIVED, count);
    }
}

void ICACHE_RAM_ATTR CryptoTraceControlAckSent()
{
    const uint16_t count = ctrlAckCount + 1;
    ctrlAckCount = count;
    if (count == 1 || count % CTRACE_STALL_PERIOD == 0)
    {
        CryptoTraceEvent(CTRACE_CONTROL_ACK_SENT, count);
    }
}

void ICACHE_RAM_ATTR CryptoTraceFinalAckRetx()
{
    const uint16_t count = finalAckRetxCount + 1;
    finalAckRetxCount = count;
    if (count <= 16 || count % CTRACE_STALL_PERIOD == 0)
    {
        CryptoTraceEvent(CTRACE_FINAL_ACK_RETX, count);
    }
}

void CryptoTraceInit(CRSFConnector *otaConnector)
{
    traceOrigin = otaConnector;
    CryptoTraceEvent(CTRACE_BOOT);
}

static void sendTraceFrame(const char *text)
{
    constexpr size_t maxText = 58; // frame_size is type + payload + crc <= 60
    uint8_t frame[CRSF_FRAME_NOT_COUNTED_BYTES + CRSF_FRAME_SIZE(maxText)];
    size_t len = strlen(text) + 1;
    if (len > maxText)
        len = maxText;
    memcpy(&frame[sizeof(crsf_header_t)], text, len);
    frame[sizeof(crsf_header_t) + len - 1] = '\0';
    crsfRouter.SetHeaderAndCrc((crsf_header_t *)frame, CRSF_FRAMETYPE_FLIGHT_MODE,
                               CRSF_FRAME_SIZE(len));
    crsfRouter.deliverMessage(traceOrigin, (crsf_header_t *)frame);
}

void CryptoTraceFlush()
{
    if (traceOrigin == nullptr)
        return;

    char text[64];
    for (uint8_t sent = 0; sent < CTRACE_FLUSH_MAX; sent++)
    {
        ctrace_entry_t entry;
        CTRACE_LOCK();
        const uint32_t head = ringHead;
        if (head - ringTail > CTRACE_RING_SIZE)
        {
            ringLost += head - ringTail - CTRACE_RING_SIZE;
            ringTail = head - CTRACE_RING_SIZE;
        }
        const bool have = ringTail != head;
        if (have)
        {
            entry = ring[ringTail % CTRACE_RING_SIZE];
            ringTail++;
        }
        CTRACE_UNLOCK();
        if (!have)
            break;

        if (ringLost)
        {
            snprintf(text, sizeof(text), "CT TRACE_LOST a=%u", (unsigned)ringLost);
            sendTraceFrame(text);
            ringLost = 0;
        }

        if (entry.ev == CTRACE_SESSION_RESET && entry.arg < CTRACE_RESET_REASON_COUNT)
        {
            snprintf(text, sizeof(text), "CT SESSION_RESET %s t=%u",
                     resetReasonNames[entry.arg], (unsigned)entry.ms);
        }
        else
        {
            const char *name = entry.ev < CTRACE_EVENT_COUNT ? eventNames[entry.ev] : "?";
            snprintf(text, sizeof(text), "CT %s a=%d t=%u",
                     name, (int)(int16_t)entry.arg, (unsigned)entry.ms);
        }
        sendTraceFrame(text);
    }
}

#endif
