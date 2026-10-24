/*
 * xtp_arq.c 
 *
 * xTP_ARQ_Send() is blocking: send frame -> busy-wait xTP_Process() -> ACK/retry/fail.
 * xTP_ARQ_OnReceive() sends ACK immediately.
 * SEQ for debug
 * V1.1.0
 *    - Fix logic ARQ NAK send
 *                                                  C.H
 */

#include "xtp_arq.h"
#include <string.h>

#if XTP_USE_ARQ

/* -------------------------------------------------------------------------
 * Internal: transmit a ctrl frame (ACK/NACK).
 * Called directly from OnReceive - "safe" because only the RX side calls this
 * and the TX lock is not held by a blocking xTP_ARQ_Send on this node.
 * -------------------------------------------------------------------------*/
static void arq_send_ctrl(xTP_Instance_t *inst, xTP_ID_t ctrl_id, uint8_t seq)
{
    uint8_t pl = seq & XTP_SEQ_NUM_MASK;
    xTP_SendEx(inst, ctrl_id, 0xFFU,
               0x00U,               /* seq_hdr = 0 for ctrl frames */
               0x00U, 0x00U, 0x01U, /* seg: stream=0, idx=0, total=1 */
               &pl, 1U);
    if (ctrl_id == XTP_ARQ_ID_ACK) {
        xTP_Log("[ARQ] TX ACK Seq=%u", (unsigned)seq);
    } else {
        xTP_Log("[ARQ] TX NACK Seq=%u", (unsigned)seq);
    }
}

/* Internal: transmit the pending data frame. */
static void arq_do_send(xTP_Instance_t *inst)
{
    xTP_ARQ_t *a = inst->arq;
    xTP_SendEx(inst, a->pend_id, a->pend_dst,
               a->pend_seq,
#if XTP_USE_SEG
               a->pend_seg_stream, a->pend_seg_idx, a->pend_seg_total,
#else
               0x00U, 0x00U, 0x01U,
#endif
               a->pend_buf, a->pend_len);
}

/* =========================================================================
 * Public API
 * =========================================================================*/

void xTP_ARQ_Init(xTP_Instance_t *inst, xTP_ARQ_t *arq)
{
    if (inst == NULL || arq == NULL) { return; }
    memset(arq, 0, sizeof(*arq));
    arq->seq_tx        = 0U;
    arq->seq_rx        = 0U;
    arq->ack_received  = 0U;
    arq->nack_received = 0U;
    arq->pend_len      = 0U;
    inst->arq          = arq;
    xTP_Log("[ARQ] Init to use");
}

void xTP_ARQ_SoftReset(xTP_Instance_t *inst)
{
    xTP_ARQ_t *a;
    if (inst == NULL || inst->arq == NULL) { return; }
    a = inst->arq;
    a->seq_tx        = 0U;
    a->seq_rx        = 0U;
    a->ack_received  = 0U;
    a->nack_received = 0U;
    a->pend_len      = 0U;
    xTP_Log("[ARQ] Soft reset");
}

xTP_Return_t xTP_ARQ_Send(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                           const void *data, uint16_t len)
{
    xTP_ARQ_t *a;
    uint8_t    seq_byte;
    uint8_t    retries;
    uint32_t   sent_at;
    uint32_t   now;

    if (inst == NULL || inst->arq == NULL) { return XTP_ERR; }
    if (data == NULL && len > 0U)          { return XTP_ERR; }
    if (len > XTP_DATA_MAX_LEN)            { return XTP_ERR_MEM; }
    a = inst->arq;

    /* Build SEQ header byte. */
    seq_byte = a->seq_tx & XTP_SEQ_NUM_MASK;

    /* Buffer payload for potential retransmit. */
    a->pend_seq = seq_byte;
    if (len > 0U) { memcpy(a->pend_buf, data, len); }
    a->pend_len = len;
    a->pend_id  = id;
    a->pend_dst = dst;
#if XTP_USE_SEG
    a->pend_seg_stream = inst->data.seg_stream;
    a->pend_seg_idx    = inst->data.seg_idx;
    a->pend_seg_total  = inst->data.seg_total;
#endif

    xTP_Log("[ARQ] SEND seq=%u id=0x%02X len=%u",
            (unsigned)seq_byte, (unsigned)id, (unsigned)len);

    /* ------------------------------------------------------------------ */
    /* Blocking send-and-wait loop.                                        */
    /* ------------------------------------------------------------------ */
    for (retries = 0U; retries <= (uint8_t)XTP_ARQ_MAX_RETRIES; retries++) {

        /* Clear flags before transmit. */
        a->ack_received  = 0U;
        a->nack_received = 0U;

        /* Transmit (or retransmit). */
        arq_do_send(inst);
        sent_at = (inst->port.get_tick != NULL) ? inst->port.get_tick() : 0U;

        if (retries > 0U) {
            xTP_Log("[ARQ] RETRANSMIT seq=%u retry=%u",
                    (unsigned)seq_byte, (unsigned)retries);
            XTP_STAT_INC(inst, arq_retransmits);
        }

        /* Busy-wait for ACK/NACK or timeout. */
        for (;;) {
            /* Pump the RX FSM to process incoming bytes. */
            xTP_Process(inst);

            /* ACK received? - success. */
            if (a->ack_received) {
                xTP_Log("[ARQ] ACK received seq=%u", (unsigned)seq_byte);
                // XTP_STAT_INC(inst, arq_ack_rx);
                a->seq_tx = (uint8_t)((a->seq_tx + 1U) & XTP_SEQ_NUM_MASK);
                if (a->on_ack) { a->on_ack(inst, seq_byte); }
                return XTP_OK;
            }

            /* NACK received? break to retry immediately. */
            if (a->nack_received) {
                xTP_Log("[ARQ] NACK received seq=%u", (unsigned)seq_byte);
                XTP_STAT_INC(inst, arq_nack_rx);
                break;  /* next retry iteration */
            }

            /* Timeout? break to retry. */
            now = (inst->port.get_tick != NULL) ? inst->port.get_tick() : 0U;
            if ((now - sent_at) >= (uint32_t)XTP_ARQ_TIMEOUT_MS) {
                xTP_Log("[ARQ] TIMEOUT seq=%u", (unsigned)seq_byte);
                XTP_STAT_INC(inst, arq_timeouts);
                break;  /* next retry iteration */
            }
        }
    }

    /* Exhausted all retries failure. */
    xTP_Log("[ARQ] FAILED seq=%u after %u retries",
            (unsigned)seq_byte, (unsigned)XTP_ARQ_MAX_RETRIES);
    XTP_STAT_INC(inst, arq_failures);
    if (a->on_fail) { a->on_fail(inst, seq_byte); }
    a->seq_tx = (uint8_t)((a->seq_tx + 1U) & XTP_SEQ_NUM_MASK);
    return XTP_ERR;
}

int xTP_ARQ_OnReceive(xTP_Instance_t *inst, xTP_ID_t id,
                       const uint8_t *payload, uint16_t len)
{
    xTP_ARQ_t *a;

    if (inst == NULL || inst->arq == NULL) { return 0; }
    a = inst->arq;

    /* --- ACK control frame ---------------------------------------------- */
    if (id == XTP_ARQ_ID_ACK) {
        if (len >= 1U) {
            xTP_Log("[ARQ] RX ACK seq=%u", (unsigned)(payload[0] & XTP_SEQ_NUM_MASK));
            XTP_STAT_INC(inst, arq_ack_rx);
            a->ack_received = 1U;   /* Signal the blocking Send loop. */
        }
        return 1;
    }

    /* --- NACK control frame --------------------------------------------- */
    if (id == XTP_ARQ_ID_NACK) {
        if (len >= 1U) {
            xTP_Log("[ARQ] RX NACK seq=%u", (unsigned)(payload[0] & XTP_SEQ_NUM_MASK));
            XTP_STAT_INC(inst, arq_nack_rx);
            a->nack_received = 1U;  /* Signal the blocking Send loop. */
        }
        return 1;
    }

    /* --- RST control frame ---------------------------------------------- */
    if (id == XTP_ARQ_ID_RST) {
        xTP_Log("[ARQ] RST from remote > soft-reset");
        a->seq_rx        = 0U;
        a->seq_tx        = 0U;
        a->ack_received  = 0U;
        a->nack_received = 0U;
        a->pend_len      = 0U;
        return 1;
    }

    /* --- Data frame: SEQ is in the frame HEADER field ------------------- */
    /*
     * Every data frame is accepted and ACKed.
     * SEQ is read from header for debugy.
     *
     * ACK is sent IMMEDIATELY here.
     * AD:
     *   - In the RX path (xTP_Process -> OnReceive).
     *   - The sender (if on this same MCU) would only call xTP_ARQ_Send
     *     which drives xTP_Process internally - not concurrent.
     */
    {
        uint8_t rx_seq = inst->data.seq_byte & XTP_SEQ_NUM_MASK;

        xTP_Log("[ARQ] Seq=%u -> ACK", (unsigned)rx_seq);

        /* Send ACK immediately. */
        arq_send_ctrl(inst, XTP_ARQ_ID_ACK, rx_seq);
        XTP_STAT_INC(inst, arq_ack_tx);

        /* Update debug seq counter. */
        a->seq_rx = (uint8_t)((rx_seq + 1U) & XTP_SEQ_NUM_MASK);

        return 0;   /* deliver pure app payload to upper layer */
    }
}

void xTP_ARQ_SendNack(xTP_Instance_t *inst, uint8_t seq_byte)
{
    if (inst == NULL || inst->arq == NULL) { return; }
    arq_send_ctrl(inst, XTP_ARQ_ID_NACK, seq_byte & XTP_SEQ_NUM_MASK);
    XTP_STAT_INC(inst, arq_nack_tx);
}

#endif /* XTP_USE_ARQ */