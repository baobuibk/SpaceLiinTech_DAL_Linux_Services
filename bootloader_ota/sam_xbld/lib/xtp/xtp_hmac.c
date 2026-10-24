/*
 * xtp_hmac.c  
 *
 * Reference: https://131002.net/siphash/siphash.pdf
 *
 * Key:  128 bits = k[0..15]
 * Init: v0..v3 = k XOR nothing-up-my-sleeve ASCII constants.
 * Compress: 2 SipRounds per 8-byte message block.
 * Finalize: 4 SipRounds, return v0^v1^v2^v3 (64-bit), truncate to 32.
 *                                                  C.H
 */

#include "xtp_hmac.h"

#if XTP_USE_HMAC

#include <string.h>

/* -------------------------------------------------------------------------
 * SipHash primitives
 * -------------------------------------------------------------------------*/
#define ROTL64(x, b) (((x) << (b)) | ((x) >> (64u - (b))))

static void sip_round(uint64_t *v0, uint64_t *v1,
                      uint64_t *v2, uint64_t *v3)
{
    *v0 += *v1; *v1 = ROTL64(*v1, 13u); *v1 ^= *v0;
    *v0 = ROTL64(*v0, 32u);
    *v2 += *v3; *v3 = ROTL64(*v3, 16u); *v3 ^= *v2;
    *v0 += *v3; *v3 = ROTL64(*v3, 21u); *v3 ^= *v0;
    *v2 += *v1; *v1 = ROTL64(*v1, 17u); *v1 ^= *v2;
    *v2 = ROTL64(*v2, 32u);
}

/* Little-endian load of 8 bytes. */
static uint64_t load_le64(const uint8_t *p)
{
    return ((uint64_t)p[0]      ) | ((uint64_t)p[1] <<  8u) |
           ((uint64_t)p[2] << 16u) | ((uint64_t)p[3] << 24u) |
           ((uint64_t)p[4] << 32u) | ((uint64_t)p[5] << 40u) |
           ((uint64_t)p[6] << 48u) | ((uint64_t)p[7] << 56u);
}

/* -------------------------------------------------------------------------
 * Internal SipHash-2-4 context (incremental update)
 * -------------------------------------------------------------------------*/
typedef struct {
    uint64_t v[4];
    uint64_t pending;       /* partial 8-byte block */
    uint8_t  pending_len;   /* bytes in pending (0..7) */
    uint32_t total_len;     /* total bytes fed */
} sip_ctx_t;

static void sip_init(sip_ctx_t *ctx, const uint8_t key[16])
{
    uint64_t k0 = load_le64(key);
    uint64_t k1 = load_le64(key + 8u);
    ctx->v[0] = k0 ^ 0x736f6d6570736575ULL;   /* "somepseu" */
    ctx->v[1] = k1 ^ 0x646f72616e646f6dULL;   /* "dorandom" */
    ctx->v[2] = k0 ^ 0x6c7967656e657261ULL;   /* "lygenera" */
    ctx->v[3] = k1 ^ 0x7465646279746573ULL;   /* "tedbytes" */
    ctx->pending     = 0u;
    ctx->pending_len = 0u;
    ctx->total_len   = 0u;
}

static void sip_feed_block(sip_ctx_t *ctx, uint64_t m)
{
    ctx->v[3] ^= m;
    sip_round(&ctx->v[0], &ctx->v[1], &ctx->v[2], &ctx->v[3]);
    sip_round(&ctx->v[0], &ctx->v[1], &ctx->v[2], &ctx->v[3]);
    ctx->v[0] ^= m;
}

static void sip_update(sip_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; i++) {
        ctx->pending |= ((uint64_t)data[i]) << (8u * ctx->pending_len);
        ctx->pending_len++;
        ctx->total_len++;
        if (ctx->pending_len == 8u) {
            sip_feed_block(ctx, ctx->pending);
            ctx->pending     = 0u;
            ctx->pending_len = 0u;
        }
    }
}

static uint64_t sip_finish(sip_ctx_t *ctx)
{
    /* Encode remaining bytes + total length in last block (top byte). */
    uint64_t last = ctx->pending
                  | ((uint64_t)(ctx->total_len & 0xFFu) << 56u);
    sip_feed_block(ctx, last);

    ctx->v[2] ^= 0xFFu;
    sip_round(&ctx->v[0], &ctx->v[1], &ctx->v[2], &ctx->v[3]);
    sip_round(&ctx->v[0], &ctx->v[1], &ctx->v[2], &ctx->v[3]);
    sip_round(&ctx->v[0], &ctx->v[1], &ctx->v[2], &ctx->v[3]);
    sip_round(&ctx->v[0], &ctx->v[1], &ctx->v[2], &ctx->v[3]);
    return ctx->v[0] ^ ctx->v[1] ^ ctx->v[2] ^ ctx->v[3];
}

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
uint32_t xTP_HMAC_Compute(const uint8_t *hdr_buf, uint8_t  hdr_len,
                           const uint8_t *data,     uint8_t  data_len,
                           const uint8_t *crc_buf,  uint8_t  crc_len)
{
    static const uint8_t key[16] = XTP_HMAC_KEY;
    sip_ctx_t ctx;
    uint64_t  tag64;

    sip_init(&ctx, key);
    if (hdr_len  > 0u && hdr_buf  != NULL) { sip_update(&ctx, hdr_buf,  hdr_len);  }
    if (data_len > 0u && data     != NULL) { sip_update(&ctx, data,     data_len); }
    if (crc_len  > 0u && crc_buf  != NULL) { sip_update(&ctx, crc_buf,  crc_len);  }
    tag64 = sip_finish(&ctx);
    return (uint32_t)(tag64 & 0xFFFFFFFFUL);
}

#endif /* XTP_USE_HMAC */
