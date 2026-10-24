/*
 * xtp_arq.h  Blocking ACK ARQ
 *
 * Design:
 *   xTP_ARQ_Send() transmits one data frame and BLOCKS until ACK is
 *   received, NACK triggers retransmit, or max retries exhausted.
 *
 *   On the RX side, ACK is sent IMMEDIATELY inside xTP_ARQ_OnReceive()
 *   This is "safe" because the
 *   receiver is never inside xTP_ARQ_Send() when it gets a data frame
 *   only the sender blocks
 *
 *   SEQ byte is KEPT in the wire format for debug/logging
 *   Control IDs always 16-bit:
 *   ACK=0xFFFE  NACK=0xFFFD  RST=0xFFFC
 *   Upper byte 0xFF is RESERVED; app IDs must be in 0x0000..0xFEFF.
 *
 * Usage:
 *   1. Declare xTP_ARQ_t arq_state;
 *   2. xTP_ARQ_Init(&inst, &arq_state);
 *   3. Replace xTP_Send() with xTP_ARQ_Send() for reliable frames.
 *      xTP_ARQ_Send() blocks until ACK or failure.
 *   4. Call xTP_Process() from main loop as usual for RX.
 *                                                  C.H
 */

#ifndef XTP_ARQ_H
#define XTP_ARQ_H

#include "xtp.h"

#if XTP_USE_ARQ

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * ARQ state
 * =========================================================================*/
struct xTP_ARQ_s {
    /* ---- TX ------------------------------------------------------------ */
    uint8_t          seq_tx;        /**< SEQ for next outgoing frame (0..127, debug only). */
    uint8_t          ack_received;  /**< Flag set by OnReceive when ACK arrives. */
    uint8_t          nack_received; /**< Flag set by OnReceive when NACK arrives. */

    uint8_t          pend_buf[XTP_DATA_MAX_LEN]; /**< Buffered payload for retransmit. */
    uint16_t         pend_len;
    xTP_ID_t         pend_id;       /**< Command ID of pending data frame. */
    uint8_t          pend_dst;      /**< Destination address. */
    uint8_t          pend_seq;      /**< SEQ header byte used for last send. */
#if XTP_USE_SEG
    uint8_t          pend_seg_stream;
    uint32_t         pend_seg_idx;
    uint32_t         pend_seg_total;
#endif

    /* ---- RX ------------------------------------------------------------ */
    uint8_t          seq_rx;        /**< Next expected SEQ from remote (debug only). */

    /* ---- Callbacks ------------------------------------------------------ */
    void (*on_ack)(xTP_Instance_t *inst, uint8_t seq);
    void (*on_fail)(xTP_Instance_t *inst, uint8_t seq);
};

/* =========================================================================
 * API
 * =========================================================================*/

/** Attach and initialise ARQ state to an instance.  Call after xTP_Init(). */
void xTP_ARQ_Init(xTP_Instance_t *inst, xTP_ARQ_t *arq);

/** Soft-reset: clear pending state, reset seq counters. */
void xTP_ARQ_SoftReset(xTP_Instance_t *inst);

/**
 * Send data via ARQ - BLOCKING.
 *
 * Transmits the frame, then busy-waits (calling xTP_Process internally)
 * until ACK is received or max retries exhausted.
 *
 * Returns:
 *   XTP_OK      - ACK received, data delivered.
 *   XTP_ERR     - Max retries exhausted (on_fail callback fired).
 *   XTP_ERR_MEM - Payload too large.
 *
 * NOTE: This function calls xTP_Process() internally, so the caller
 *       must NOT hold the RX lock when calling this.
 */
xTP_Return_t xTP_ARQ_Send(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                           const void *data, uint16_t len);

/**
 * Process a received frame.  Called automatically by xTP_Process().
 *
 *   - ACK/NACK: sets flags read by the blocking xTP_ARQ_Send() loop.
 *   - RST: resets seq counters.
 *   - Data frames: sends ACK immediately, delivers payload to upper layer.
 *
 * @return 1 frame consumed (ctrl frame).  0 accepted - deliver to app.
 */
int xTP_ARQ_OnReceive(xTP_Instance_t *inst, xTP_ID_t id,
                       const uint8_t *payload, uint16_t len);

/** Send a NACK frame. */
void xTP_ARQ_SendNack(xTP_Instance_t *inst, uint8_t seq_byte);                       

/** Convenience: always returns 1 since there is no pending state machine. */
static inline int xTP_ARQ_IsIdle(const xTP_Instance_t *inst)
{
    (void)inst;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* XTP_USE_ARQ */
#endif /* XTP_ARQ_H */