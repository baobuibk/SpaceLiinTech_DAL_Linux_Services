/*
 * xtp_seg.c  Segmentation layer implementation
 *
 * Two modes:
 *   Known-total (seg_total > 0): reassembly into stream->buf; on_complete
 *                                called once with full data.
 *   Streaming   (seg_total == 0): on_complete called per-chunk immediately,
 *                                 no buffering.  Unlimited chunk count.
 *                                                  C.H
 */

#include "xtp_seg.h"
#include <string.h>

#if XTP_USE_SEG

static void (*s_seg_on_complete)(xTP_Instance_t *, uint8_t,
                                  xTP_ID_t, const uint8_t *, uint32_t) = NULL;

/* -------------------------------------------------------------------------
 * Stream slot helpers
 * -------------------------------------------------------------------------*/
static xTP_Seg_Stream_t *seg_find(xTP_Instance_t *inst, uint8_t src, uint8_t sid)
{
    xTP_Seg_Stream_t *s = inst->seg_streams;
    uint8_t i;
    for (i = 0U; i < XTP_SEG_MAX_STREAMS; i++) {
        if (s[i].active && s[i].src_addr == src && s[i].stream_id == sid)
            return &s[i];
    }
    return NULL;
}

static xTP_Seg_Stream_t *seg_alloc(xTP_Instance_t *inst, uint8_t src,
                                    uint8_t sid, uint32_t total, uint32_t now)
{
    xTP_Seg_Stream_t *s = inst->seg_streams;
    uint8_t i;
    for (i = 0U; i < XTP_SEG_MAX_STREAMS; i++) {
        if (!s[i].active) {
            memset(&s[i], 0, sizeof(s[i]));
            s[i].active            = 1U;
            s[i].src_addr          = src;
            s[i].stream_id         = sid;
            s[i].total_chunks      = total;
            s[i].streaming         = (total == 0U) ? 1U : 0U;
            s[i].next_expected_idx = 0U;
            s[i].bytes_received    = 0U;
            s[i].last_chunk_ms     = now;
            s[i].on_complete       = s_seg_on_complete;
            return &s[i];
        }
    }
    xTP_Log("[SEG] no free stream slot");
    return NULL;
}

/* -------------------------------------------------------------------------
 * Internal dispatch helper fires on_complete (if set) then on_receive
 * and cmd_table walk, so registered commands work even when SEG is enabled.
 * -------------------------------------------------------------------------*/
static void xtp_seg_dispatch(xTP_Instance_t *inst,
                              xTP_Seg_Stream_t *stream,
                              uint8_t src,
                              const uint8_t *data, uint32_t len)
{
    xTP_Data_t *d = &inst->data;

    /* 1. Optional user-registered on_complete callback (SEG-specific). */
    if (stream->on_complete) {
        stream->on_complete(inst, src, d->id, data, len);
    }

    /* 2. Generic on_receive callback (same as non-SEG path). */
    if (inst->on_receive) {
        inst->on_receive(inst, src, d->id, data, (uint16_t)len);
    } else {
        xTP_ApplicationHandler(inst, src, d->id, data, (uint16_t)len);
    }

    /* 3. Command table walk allows xTP_RegisterCommand() to work with SEG. */
    {
        uint8_t ci;
        for (ci = 0U; ci < inst->cmd_count; ci++) {
            if (inst->cmd_table[ci].id == d->id &&
                inst->cmd_table[ci].handler != NULL) {
                inst->cmd_table[ci].handler(inst, data, (uint16_t)len);
                break;
            }
        }
    }
}


void xTP_SEG_Init(xTP_Instance_t *inst, xTP_Seg_Stream_t *streams)
{
    if (inst == NULL || streams == NULL) { return; }
    memset(streams, 0, sizeof(xTP_Seg_Stream_t) * XTP_SEG_MAX_STREAMS);
    inst->seg_streams = streams;
    xTP_Log("[SEG] init %u streams", (unsigned)XTP_SEG_MAX_STREAMS);
}

void xTP_SEG_SetCallback(xTP_Instance_t *inst,
                          void (*on_complete)(xTP_Instance_t *, uint8_t,
                                              xTP_ID_t, const uint8_t *, uint32_t))
{
    uint8_t i;
    if (inst == NULL || inst->seg_streams == NULL) { return; }
    s_seg_on_complete = on_complete;
    for (i = 0U; i < XTP_SEG_MAX_STREAMS; i++) {
        inst->seg_streams[i].on_complete = on_complete;
    }
}

xTP_Return_t xTP_SEG_SendChunk(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                                uint8_t stream_id, uint32_t chunk_idx,
                                uint32_t total_chunks,
                                const void *data, uint16_t len)
{
    if (inst == NULL || (data == NULL && len > 0U)) { return XTP_ERR; }
    inst->data.seg_stream = stream_id;
    inst->data.seg_idx    = chunk_idx;
    inst->data.seg_total  = total_chunks;
#if XTP_USE_ARQ
    return xTP_ARQ_Send(inst, id, dst, data, len);
#else
    return xTP_SendEx(inst, id, dst, 0x00U, stream_id, chunk_idx, total_chunks, data, len);
#endif
}

xTP_Return_t xTP_SEG_Send(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                           const void *data, uint32_t total_len)
{
    const uint8_t *src_ptr;
    uint32_t       remaining;
    uint32_t       total_chunks;
    uint32_t       chunk_idx;
    static uint8_t s_stream_id = 0U;
    uint8_t        stream_id;

    if (inst == NULL || data == NULL || total_len == 0U) { return XTP_ERR; }

    total_chunks = (total_len + XTP_DATA_MAX_LEN - 1U) / XTP_DATA_MAX_LEN;
    if (total_chunks > XTP_SEG_MAX_CHUNKS || total_chunks == 0U) {
        xTP_Log("[SEG] %lu B: %lu chunks > max %u",
                (unsigned long)total_len, (unsigned long)total_chunks,
                (unsigned)XTP_SEG_MAX_CHUNKS);
        return XTP_ERR_MEM;
    }

    stream_id = s_stream_id++;
    src_ptr   = (const uint8_t *)data;
    remaining = total_len;

    for (chunk_idx = 0U; chunk_idx < total_chunks; chunk_idx++) {
        uint16_t     chunk_len;
        xTP_Return_t r;

        chunk_len = (remaining > (uint32_t)XTP_DATA_MAX_LEN)
                    ? (uint16_t)XTP_DATA_MAX_LEN : (uint16_t)remaining;

        r = xTP_SEG_SendChunk(inst, id, dst, stream_id, chunk_idx,
                               total_chunks, src_ptr, chunk_len);
        if (r != XTP_OK) { return r; }

#if XTP_USE_ARQ
        // No need spin-wait for ACK here; xTP_ARQ_Send() already does that. 
        // /* Spin until ACKed (bare-metal blocking). */
        // {
        //     uint32_t deadline = inst->port.get_tick() +
        //                (uint32_t)(XTP_ARQ_TIMEOUT_MS * (XTP_ARQ_MAX_RETRIES + 2U) + 10U);
        //     while (!xTP_ARQ_IsIdle(inst)) {
        //         xTP_Process(inst);
        //         xTP_ARQ_Poll(inst);
        //         if (inst->arq && inst->arq->state == XTP_ARQ_FAILED) {
        //             xTP_Log("[SEG] ARQ failed chunk %lu/%lu",
        //                     (unsigned long)chunk_idx, (unsigned long)(total_chunks-1U));
        //             return XTP_ERR;
        //         }
        //         if ((inst->port.get_tick() - deadline) < 0x80000000UL) {
        //             xTP_Log("[SEG] deadline exceeded");
        //             return XTP_ERR_TIMEOUT;
        //         }
        //     }
        // }
#else
        /* No ARQ = no ACK-based flow control.
         * Pace chunks with a fixed inter-frame gap so the TX FIFO drains
         * before the next chunk is pushed in, preventing CRC corruption. */
#if XTP_SEG_INTER_FRAME_DELAY_MS > 0
        {
            uint32_t t = inst->port.get_tick();
            while ((inst->port.get_tick() - t) <
                   (uint32_t)XTP_SEG_INTER_FRAME_DELAY_MS) { ; }
        }
#endif
#endif /* XTP_USE_ARQ */

        xTP_Log("[SEG] sent chunk %lu/%lu (%u B)",
                (unsigned long)chunk_idx, (unsigned long)(total_chunks-1U),
                (unsigned)chunk_len);
        src_ptr   += chunk_len;
        remaining -= chunk_len;
    }
    return XTP_OK;
}

void xTP_SEG_OnChunk(xTP_Instance_t *inst,
                      const uint8_t *app_payload, uint16_t app_len)
{
    xTP_Data_t       *d;
    xTP_Seg_Stream_t *stream;
    uint8_t           src, sid;
    uint32_t          idx, total, now;

    if (inst == NULL || inst->seg_streams == NULL) { return; }
    d   = &inst->data;
    now = inst->port.get_tick();

#if XTP_USE_TARGET
    src = d->src_addr_received;
#else
    src = 0U;
#endif
    sid   = d->seg_stream;
    idx   = d->seg_idx;
    total = d->seg_total;

    /* ------------------------------------------------------------------ */
    /* STREAMING MODE (total == 0): deliver each chunk live, no buffer.    */
    /* ------------------------------------------------------------------ */
    if (total == 0U) {
        stream = seg_find(inst, src, sid);
        if (stream == NULL) {
            stream = seg_alloc(inst, src, sid, 0U, now);
            if (stream == NULL) { return; }
            // if (XTP_SEG_MAX_STREAMS > 0U)
            //     stream->on_complete = inst->seg_streams[0].on_complete;
        }
        stream->last_chunk_ms = now;
        xTP_Log("[SEG] stream chunk idx=%lu  %u B (streaming)",
                (unsigned long)idx, (unsigned)app_len);
        xtp_seg_dispatch(inst, stream, src, app_payload, (uint32_t)app_len);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* KNOWN-TOTAL MODE: reassemble into static buffer.                    */
    /* ------------------------------------------------------------------ */
    if (idx >= total) {
        xTP_Log("[SEG] invalid idx=%lu total=%lu", (unsigned long)idx,
                (unsigned long)total);
        return;
    }

    stream = seg_find(inst, src, sid);
    if (stream == NULL) {
        if (total > XTP_SEG_MAX_CHUNKS) {
            xTP_Log("[SEG] total=%lu > max %u chunks — use streaming",
                    (unsigned long)total, (unsigned)XTP_SEG_MAX_CHUNKS);
            return;
        }
        stream = seg_alloc(inst, src, sid, total, now);
        if (stream == NULL) { return; }
        // if (XTP_SEG_MAX_STREAMS > 0U)
        //     stream->on_complete = inst->seg_streams[0].on_complete;
    }

    stream->last_chunk_ms = now;

    if (idx != stream->next_expected_idx) {
        xTP_Log("[SEG] OOO idx=%lu expected=%lu",
                (unsigned long)idx, (unsigned long)stream->next_expected_idx);
        return;
    }
    if (stream->bytes_received + app_len > XTP_SEG_BUF_SIZE) {
        xTP_Log("[SEG] buf overflow, discard stream");
        stream->active = 0U; return;
    }

    memcpy(&stream->buf[stream->bytes_received], app_payload, app_len);
    stream->bytes_received   += app_len;
    stream->next_expected_idx = idx + 1U;

    xTP_Log("[SEG] chunk %lu/%lu  rxd=%lu B",
            (unsigned long)idx, (unsigned long)(total - 1U),
            (unsigned long)stream->bytes_received);

    if (stream->next_expected_idx == total) {
        xTP_Log("[SEG] stream %u complete (%lu B)",
                (unsigned)sid, (unsigned long)stream->bytes_received);
        XTP_STAT_INC(inst, seg_reassembled);
        xtp_seg_dispatch(inst, stream, src,
                         stream->buf, stream->bytes_received);
        stream->active = 0U;
    }
}

void xTP_SEG_Poll(xTP_Instance_t *inst)
{
    uint8_t  i;
    uint32_t now;
    if (inst == NULL || inst->seg_streams == NULL) { return; }
    now = inst->port.get_tick();
    for (i = 0U; i < XTP_SEG_MAX_STREAMS; i++) {
        xTP_Seg_Stream_t *s = &inst->seg_streams[i];
        if (s->active && !s->streaming &&
            (now - s->last_chunk_ms) > (uint32_t)XTP_SEG_TIMEOUT_MS) {
            xTP_Log("[SEG] stream %u timeout (%lu/%lu chunks)",
                    (unsigned)s->stream_id,
                    (unsigned long)s->next_expected_idx,
                    (unsigned long)s->total_chunks);
            XTP_STAT_INC(inst, seg_timeouts);
            s->active = 0U;
        }
    }
}

#endif /* XTP_USE_SEG */
