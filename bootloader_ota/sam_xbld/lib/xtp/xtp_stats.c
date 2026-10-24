/*
 * xtp_stats.c
 *                                                  C.H
 */

#include "xtp_stats.h"
#include "xtp.h"      /* xTP_Instance_t, xTP_Log */
#include <string.h>

/* =========================================================================
 * API
 * =========================================================================*/

void xTP_Stats_Attach(xTP_Instance_t *inst, xTP_Stats_t *stats)
{
    if (inst == NULL || stats == NULL) { return; }
    memset(stats, 0, sizeof(*stats));
    inst->stats = stats;
}

void xTP_Stats_Reset(xTP_Stats_t *stats)
{
    if (stats == NULL) { return; }
    memset(stats, 0, sizeof(*stats));
}

void xTP_Stats_Print(const xTP_Stats_t *s)
{
    if (s == NULL) {
        xTP_Log("[STATS] not attached");
        return;
    }

    xTP_Log("========== xTP Channel Statistics ==========");

    /* TX */
    xTP_Log("[TX] Frames     : %lu", (unsigned long)s->tx_frames);
    xTP_Log("[TX] Bytes      : %lu", (unsigned long)s->tx_bytes);

    /* RX */
    xTP_Log("[RX] Frames     : %lu", (unsigned long)s->rx_frames);
    xTP_Log("[RX] Bytes      : %lu", (unsigned long)s->rx_bytes);
    xTP_Log("[RX] ECC fixed  : %lu", (unsigned long)s->rx_ecc_corrections);
    xTP_Log("[RX] CRC err    : %lu", (unsigned long)s->rx_crc_errors);
    xTP_Log("[RX] Frame err  : %lu", (unsigned long)s->rx_frame_errors);
    xTP_Log("[RX] Timeouts   : %lu", (unsigned long)s->rx_timeouts);

    /* ARQ */
    xTP_Log("[ARQ] ACK TX    : %lu", (unsigned long)s->arq_ack_tx);
    xTP_Log("[ARQ] ACK RX    : %lu", (unsigned long)s->arq_ack_rx);
    xTP_Log("[ARQ] NACK TX   : %lu", (unsigned long)s->arq_nack_tx);
    xTP_Log("[ARQ] NACK RX   : %lu", (unsigned long)s->arq_nack_rx);
    xTP_Log("[ARQ] Timeouts  : %lu", (unsigned long)s->arq_timeouts);
    xTP_Log("[ARQ] Retransmit: %lu", (unsigned long)s->arq_retransmits);
    xTP_Log("[ARQ] Dup RX    : %lu", (unsigned long)s->arq_dup_rx);
    xTP_Log("[ARQ] Failures  : %lu", (unsigned long)s->arq_failures);

    /* SEG */
    xTP_Log("[SEG] Reassembl : %lu", (unsigned long)s->seg_reassembled);
    xTP_Log("[SEG] Timeouts  : %lu", (unsigned long)s->seg_timeouts);

    xTP_Log("=============================================");
}
