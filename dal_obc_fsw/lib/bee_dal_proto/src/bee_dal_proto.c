/* bee_dal_proto.c */
#include "bee_dal_proto.h"
#include "bee_dal_crc16.h"
#include <string.h>

/* =========================================================================
 * Decoder FSM states (internal)
 * ========================================================================= */
enum {
    ST_WAIT_START = 0,
    ST_TYPE,
    ST_ADDR,
    ST_LEN_LO,   /* first LENGTH byte on the wire -- LSB first (little-endian) */
    ST_LEN_HI,
    ST_PAYLOAD,
    ST_CRC_LO,   /* first CRC byte on the wire -- LSB first (little-endian) */
    ST_CRC_HI,
};

static void dec_reset(bee_dal_decoder_t *dec)
{
    dec->state    = ST_WAIT_START;
    dec->data_idx = 0;
    dec->length   = 0;
    dec->crc_calc = 0;
    dec->crc_rx   = 0;
    dec->idle_ms  = 0;
}

void bee_dal_decoder_init(bee_dal_decoder_t *dec)
{
    memset(dec, 0, sizeof(*dec));
    dec_reset(dec);
}

bee_dal_dec_result_t bee_dal_decoder_feed(bee_dal_decoder_t *dec, uint8_t b,
                                           bee_dal_tc_frame_t *out)
{
    dec->idle_ms = 0;

    switch (dec->state) {

    case ST_WAIT_START:
        if (b == BEE_DAL_START_BYTE) {
            dec->crc_calc = bee_dal_crc16_init();
            dec->state = ST_TYPE;
        }
        /* else: discard byte, stay here (auto-resync on next 0xA5) */
        break;

    case ST_TYPE:
        dec->type     = b;
        dec->state    = ST_ADDR;
        break;

    case ST_ADDR:
        dec->addr     = b;
        dec->state    = ST_LEN_LO;
        break;

    case ST_LEN_LO:
        dec->length   = b;
        dec->state    = ST_LEN_HI;
        break;

    case ST_LEN_HI:
        dec->length  |= (uint16_t)((uint16_t)b << 8);
        if (dec->length > BEE_DAL_MAX_PAYLOAD) {
            /* Can't buffer this frame */
            dec_reset(dec);
            break;
        }
        dec->data_idx = 0;
        dec->state = (dec->length == 0) ? ST_CRC_LO : ST_PAYLOAD;
        break;

    case ST_PAYLOAD:
        dec->payload[dec->data_idx++] = b;
        dec->crc_calc = bee_dal_crc16_update(dec->crc_calc, b);
        if (dec->data_idx >= dec->length)
            dec->state = ST_CRC_LO;
        break;

    case ST_CRC_LO:
        dec->crc_rx = b;
        dec->state  = ST_CRC_HI;
        break;

    case ST_CRC_HI: {
        dec->crc_rx |= (uint16_t)((uint16_t)b << 8);
        bee_dal_dec_result_t result;

        if (dec->crc_rx == dec->crc_calc) {
            out->type   = dec->type;
            out->addr   = dec->addr;
            out->length = dec->length;
            if (dec->length > 0)
                memcpy(out->payload, dec->payload, dec->length);
            result = BEE_DAL_DEC_FRAME_OK;
        } else {
            result = BEE_DAL_DEC_CRC_ERROR;
        }
        dec_reset(dec);
        return result;
    }

    default:
        dec_reset(dec);
        break;
    }

    return BEE_DAL_DEC_NEED_MORE;
}

bee_dal_dec_result_t bee_dal_decoder_idle(bee_dal_decoder_t *dec, uint32_t elapsed_ms)
{
    if (dec->state == ST_WAIT_START)
        return BEE_DAL_DEC_NEED_MORE;  /* nothing pending -- no-op */

    dec->idle_ms += elapsed_ms;
    if (dec->idle_ms >= BEE_DAL_INTER_BYTE_TIMEOUT_MS) {
        dec_reset(dec);
        return BEE_DAL_DEC_TIMEOUT;
    }
    return BEE_DAL_DEC_NEED_MORE;
}

/* =========================================================================
 * Encoder
 * ========================================================================= */
int bee_dal_encode_tm(uint8_t ack_err, uint8_t addr_errcode,
                      const uint8_t *payload, uint16_t len,
                      uint8_t *out, size_t out_cap)
{
    size_t need = (size_t)BEE_DAL_TM_HDR_LEN + len + 2U;
    if (out_cap < need)
        return -1;
    if (len > 0 && payload == NULL)
        return -1;

    size_t off = 0;
    out[off++] = BEE_DAL_START_BYTE;
    out[off++] = ack_err;
    out[off++] = addr_errcode;
    out[off++] = (uint8_t)(len & 0xFFU);
    out[off++] = (uint8_t)((len >> 8) & 0xFFU);

    uint16_t crc = bee_dal_crc16_init();
    for (uint16_t i = 0; i < len; i++) {
        out[off++] = payload[i];
        crc = bee_dal_crc16_update(crc, payload[i]);
    }

    out[off++] = (uint8_t)(crc & 0xFFU);
    out[off++] = (uint8_t)((crc >> 8) & 0xFFU);

    return (int)off;
}

int bee_dal_encode_err(uint8_t errcode, uint8_t *out, size_t out_cap)
{
    return bee_dal_encode_tm(BEE_DAL_ACK_ERR_MARK, errcode, NULL, 0, out, out_cap);
}

/* =========================================================================
 * Payload marshalling helpers -- little-endian (LSB first)
 * ========================================================================= */
void bee_dal_put_u8(uint8_t *buf, size_t *off, uint8_t v)
{
    buf[(*off)++] = v;
}

void bee_dal_put_u16(uint8_t *buf, size_t *off, uint16_t v)
{
    buf[(*off)++] = (uint8_t)(v & 0xFFU);
    buf[(*off)++] = (uint8_t)((v >> 8) & 0xFFU);
}

void bee_dal_put_u32(uint8_t *buf, size_t *off, uint32_t v)
{
    buf[(*off)++] = (uint8_t)(v & 0xFFU);
    buf[(*off)++] = (uint8_t)((v >> 8) & 0xFFU);
    buf[(*off)++] = (uint8_t)((v >> 16) & 0xFFU);
    buf[(*off)++] = (uint8_t)((v >> 24) & 0xFFU);
}

void bee_dal_put_i8(uint8_t *buf, size_t *off, int8_t v)
{
    bee_dal_put_u8(buf, off, (uint8_t)v);
}

void bee_dal_put_i16(uint8_t *buf, size_t *off, int16_t v)
{
    bee_dal_put_u16(buf, off, (uint16_t)v);
}

void bee_dal_put_str(uint8_t *buf, size_t *off, const char *s, size_t fixed_len)
{
    size_t n = 0;
    if (s) {
        while (n < fixed_len && s[n] != '\0')
            n++;
        memcpy(buf + *off, s, n);
    }
    if (n < fixed_len)
        memset(buf + *off + n, 0, fixed_len - n);
    *off += fixed_len;
}

uint8_t bee_dal_get_u8(const uint8_t *buf, size_t *off)
{
    return buf[(*off)++];
}

uint16_t bee_dal_get_u16(const uint8_t *buf, size_t *off)
{
    uint16_t v = (uint16_t)(buf[*off] | ((uint16_t)buf[*off + 1] << 8));
    *off += 2;
    return v;
}

uint32_t bee_dal_get_u32(const uint8_t *buf, size_t *off)
{
    uint32_t v = (uint32_t)buf[*off]
               | ((uint32_t)buf[*off + 1] << 8)
               | ((uint32_t)buf[*off + 2] << 16)
               | ((uint32_t)buf[*off + 3] << 24);
    *off += 4;
    return v;
}
