/*
 * xtp_stats.h
 *
 * Optional telemetry layer.  Attach a stats block to an instance with
 * xTP_Stats_Attach(); counters are updated automatically by the core,
 * ARQ and SEG layers.  Call xTP_Stats_Print() (or read fields directly)
 * to inspect.  Set inst->stats = NULL (or never call Attach) to disable..
 *                                                  C.H
 */

#ifndef XTP_STATS_H
#define XTP_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Stats block one per xTP instance
 * =========================================================================*/
typedef struct {
    /* ----- TX ---------------------------------------------------------- */
    uint32_t tx_frames;          /**< Valid frames transmitted.            */
    uint32_t tx_bytes;           /**< Total payload bytes transmitted.     */

    /* ----- RX ---------------------------------------------------------- */
    uint32_t rx_frames;          /**< Valid frames received and dispatched.*/
    uint32_t rx_bytes;           /**< Total payload bytes received.        */
    uint32_t rx_ecc_corrections; /**< ECC single-bit corrections applied.  */
    uint32_t rx_crc_errors;      /**< CRC mismatches (uncorrectable).      */
    uint32_t rx_frame_errors;    /**< Framing / STOP-byte errors.          */
    uint32_t rx_timeouts;        /**< Frame-level inter-byte timeouts.     */

    /* ----- ARQ --------------------------------------------------------- */
    uint32_t arq_timeouts;       /**< ACK timeout expirations.             */
    uint32_t arq_retransmits;    /**< Retransmit attempts.                 */
    uint32_t arq_ack_tx;         /**< ACK control frames sent.             */
    uint32_t arq_ack_rx;         /**< ACK control frames received.         */
    uint32_t arq_nack_tx;        /**< NACK control frames sent.            */
    uint32_t arq_nack_rx;        /**< NACK control frames received.        */
    uint32_t arq_dup_rx;         /**< Duplicate frames (re-ACKed, dropped).*/
    uint32_t arq_failures;       /**< Sequences that exhausted retries.    */

    /* ----- SEG --------------------------------------------------------- */
    uint32_t seg_reassembled;    /**< Complete streams reassembled.        */
    uint32_t seg_timeouts;       /**< SEG reassembly timeouts.             */
} xTP_Stats_t;

struct xTP_Instance_s;

/* =========================================================================
 * API
 * =========================================================================*/

/** Attach stats block to instance.  Clears the block before attaching. */
void xTP_Stats_Attach(struct xTP_Instance_s *inst, xTP_Stats_t *stats);

/** Zero all counters. */
void xTP_Stats_Reset(xTP_Stats_t *stats);

/** Print all counters via xTP_Log(). */
void xTP_Stats_Print(const xTP_Stats_t *stats);

/* =========================================================================
 * Internal increment macros (used by xtp.c / xtp_arq.c / xtp_seg.c)
 * =========================================================================*/
#define XTP_STAT_INC(inst, field) \
    do { if ((inst) != NULL && (inst)->stats != NULL) \
             { (inst)->stats->field++; } } while(0)

#define XTP_STAT_ADD(inst, field, n) \
    do { if ((inst) != NULL && (inst)->stats != NULL) \
             { (inst)->stats->field += (uint32_t)(n); } } while(0)

#ifdef __cplusplus
}
#endif
#endif /* XTP_STATS_H */
