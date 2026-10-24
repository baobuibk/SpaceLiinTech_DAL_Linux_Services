/*
 * xtp_seg.h
 *
 * SEG header (variable length, before LEN):
 *   SS  stream_id        : 1 byte  (0..255)
 *   SI  chunk_idx        : LEB128  (uint32_t, unbounded)
 *   ST  total_chunks     : LEB128  (0 = streaming mode)
 *
 * Two modes:
 *   Known-total (total>0) : reassembly into static buf; on_complete fires
 *                           once when all total_chunks received.
 *   Streaming  (total=0)  : each chunk delivered immediately to on_complete;
 *                           no reassembly buffer needed.
 *
 * Max known-total transfer: XTP_SEG_MAX_CHUNKS x XTP_DATA_MAX_LEN bytes.
 * Streaming: unlimited - app controls end-of-stream via its own protocol.
 *                                                  C.H
 */

#ifndef XTP_SEG_H
#define XTP_SEG_H

#include "xtp.h"
#include "xtp_arq.h"

#if XTP_USE_SEG

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Per-stream reassembly state
 * =========================================================================*/
struct xTP_Seg_Stream_s {
    uint8_t   active;
    uint8_t   src_addr;
    uint8_t   stream_id;
    uint8_t   streaming;           /**< 1 = streaming mode (total==0). */
    uint32_t  total_chunks;        /**< 0 = streaming. */
    uint32_t  next_expected_idx;
    uint32_t  bytes_received;      /**< Bytes written to buf so far. */
    uint32_t  last_chunk_ms;
    uint8_t   buf[XTP_SEG_BUF_SIZE]; /**< Reassembly buffer (known-total only). */
    /**
     * Fired on completion:
     *   known-total: once, with full reassembled data.
     *   streaming:   once per chunk, with that chunk's data.
     *     (check stream->streaming to distinguish)
     */
    void (*on_complete)(xTP_Instance_t *inst, uint8_t src_addr,
                         xTP_ID_t id, const uint8_t *data, uint32_t len);
};

/* =========================================================================
 * API
 * =========================================================================*/

/** Attach and initialise stream array.  Call after xTP_Init(). */
void xTP_SEG_Init(xTP_Instance_t *inst, xTP_Seg_Stream_t *streams);

/**
 * Register completion/chunk callback for all streams on this instance.
 * In streaming mode, called once per chunk (each with its own data+len).
 */
void xTP_SEG_SetCallback(xTP_Instance_t *inst,
                          void (*on_complete)(xTP_Instance_t *inst,
                                              uint8_t src_addr,
                                              xTP_ID_t id,
                                              const uint8_t *data,
                                              uint32_t len));

/**
 * Send large payload auto-chunks and uses ARQ per chunk (known-total).
 * Blocks until all chunks ACKed or a chunk fails.
 * Max payload: XTP_SEG_MAX_CHUNKS x XTP_DATA_MAX_LEN.
 */
xTP_Return_t xTP_SEG_Send(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                           const void *data, uint32_t total_len);

/**
 * Send one raw chunk with explicit SEG parameters.
 * Use for streaming (total=0) or manual chunk control.
 */
xTP_Return_t xTP_SEG_SendChunk(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                                uint8_t stream_id, uint32_t chunk_idx,
                                uint32_t total_chunks,
                                const void *data, uint16_t len);

/**
 * Process a received chunk.  Called automatically by xTP_Process().
 * SEG header fields are in inst->data.seg_* (decoded by FSM).
 */
void xTP_SEG_OnChunk(xTP_Instance_t *inst,
                      const uint8_t *app_payload, uint16_t app_len);

/** Call from main loop - times out stale incomplete reassemblies. */
void xTP_SEG_Poll(xTP_Instance_t *inst);

#ifdef __cplusplus
}
#endif

#endif /* XTP_USE_SEG */
#endif /* XTP_SEG_H */
