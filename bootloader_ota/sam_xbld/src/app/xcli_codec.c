/*
 * xcli_codec.c  -  xCLI codec
 */

#include "xcli_codec.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* =========================================================================
 * Request encoder
 * =========================================================================*/
int xcli_encode_request(uint8_t *buf, size_t buf_cap,
                        uint8_t cmd_id, uint8_t seq,
                        const char *cmd_str)
{
    size_t slen = cmd_str ? strlen(cmd_str) : 0;
    size_t total = XCLI_REQ_HDR_LEN + slen;
    if (total > buf_cap) return -1;

    buf[0] = XCLI_DIR_REQUEST;
    buf[1] = cmd_id;
    buf[2] = seq;
    if (slen) memcpy(buf + 3, cmd_str, slen);
    return (int)total;
}

/* =========================================================================
 * Fragment accumulator
 * =========================================================================*/
void xcli_frag_reset(XcliFragBuf_t *fb)
{
    for (unsigned int i = 0; i < XCLI_MAX_FRAGS; i++) {
        free(fb->frags[i].data);
        fb->frags[i].data = NULL;
        fb->frags[i].len  = 0;
    }
    memset(fb, 0, sizeof(*fb));
}

int xcli_frag_feed(XcliFragBuf_t *fb, XcliResponse_t *rsp,
                   const uint8_t *payload, uint16_t plen,
                   uint8_t expected_cmd_id, uint8_t expected_seq)
{
    /* Validate header */
    if (plen < XCLI_RESP_HDR_LEN) return -1;
    if (payload[0] != XCLI_DIR_RESPONSE)    return -1;
    if (payload[1] != expected_cmd_id)       return -1;
    if (payload[2] != expected_seq)          return -1;

    uint8_t status = payload[3];
    uint8_t total  = payload[4];
    uint8_t idx    = payload[5];

    if (total == 0 || idx >= total || idx >= XCLI_MAX_FRAGS) return -1;

    /* First fragment initialises the accumulator */
    if (!fb->used) {
        xcli_frag_reset(fb);
        fb->used   = 1;
        fb->cmd_id = expected_cmd_id;
        fb->seq    = expected_seq;
        fb->total  = total;
        fb->have   = 0;
    } else {
        if (fb->cmd_id != expected_cmd_id || fb->seq != expected_seq) return -1;
        if (fb->total  != total) return -1;
    }

    /* Store fragment data (copy) */
    uint16_t dlen = plen - XCLI_RESP_HDR_LEN;
    if (fb->frags[idx].data == NULL) {
        fb->frags[idx].data = malloc(dlen > 0U ? dlen : 1U); 
        if (!fb->frags[idx].data) return -1;
        if (dlen > 0U) {
            memcpy(fb->frags[idx].data, payload + XCLI_RESP_HDR_LEN, dlen);
        }
        fb->frags[idx].len = dlen; 
        fb->have++;
    }

    if (fb->have < fb->total) return 0;   /* more to come */

    /* ------------------------------------------------------------------ *
     * All fragments received - reassemble + parse                         *
     * ------------------------------------------------------------------ */
    /* Calculate total buffer size */
    uint32_t total_len = 0;
    for (int i = 0; i < fb->total; i++) total_len += fb->frags[i].len;

    uint8_t *merged = malloc(total_len + 1);
    if (!merged) return -1;

    uint32_t off = 0;
    for (int i = 0; i < fb->total; i++) {
        if (fb->frags[i].len > 0) {
            memcpy(merged + off, fb->frags[i].data, fb->frags[i].len);
        }
        off += fb->frags[i].len;
    }
    merged[total_len] = 0;

    /* Fill response struct */
    memset(rsp, 0, sizeof(*rsp));
    rsp->cmd_id  = expected_cmd_id;
    rsp->seq     = expected_seq;
    rsp->status  = status;
    rsp->buf     = merged;
    rsp->buf_len = total_len;

    /* Parse TLV fields from merged buffer */
    uint32_t pos = 0;
    rsp->nfields = 0;
    while (pos < total_len && rsp->nfields < XCLI_MAX_FIELDS) {
        if (pos + 1 > total_len) break;
        uint8_t nl = merged[pos++];
        if (nl == 0 || pos + nl > total_len) break;
        char name[XCLI_MAX_FIELD_NAME];
        uint8_t copy_len = (nl < XCLI_MAX_FIELD_NAME-1) ? nl : (XCLI_MAX_FIELD_NAME-1);
        memcpy(name, merged + pos, copy_len);
        name[copy_len] = '\0';
        pos += nl;

        if (pos + 1 > total_len) break;
        uint8_t tc = merged[pos++];

        if (pos + 2 > total_len) break;
        uint16_t dl = (uint16_t)(merged[pos] | ((uint16_t)merged[pos+1] << 8));
        pos += 2;
        if (pos + dl > total_len) break;

        XcliField_t *field = &rsp->fields[rsp->nfields];
        memcpy(field->name, name, copy_len);
        field->name[copy_len] = '\0';
        field->type_code = tc;
        field->raw       = merged + pos;
        field->raw_len   = dl;
        rsp->nfields++;
        pos += dl;
    }

    xcli_frag_reset(fb);
    return 1;
}

void xcli_response_free(XcliResponse_t *rsp)
{
    free(rsp->buf);
    memset(rsp, 0, sizeof(*rsp));
}

/* =========================================================================
 * Field accessors
 * =========================================================================*/
const XcliField_t *xcli_field_get(const XcliResponse_t *rsp, const char *name)
{
    for (int i = 0; i < rsp->nfields; i++)
        if (strcmp(rsp->fields[i].name, name) == 0)
            return &rsp->fields[i];
    return NULL;
}

uint8_t xcli_field_u8(const XcliField_t *f)
{
    return (f && f->raw_len >= 1) ? f->raw[0] : 0;
}

uint16_t xcli_field_u16(const XcliField_t *f)
{
    if (!f || f->raw_len < 2) return 0;
    return (uint16_t)(f->raw[0] | ((uint16_t)f->raw[1] << 8));
}

uint32_t xcli_field_u32(const XcliField_t *f)
{
    if (!f || f->raw_len < 4) return 0;
    return (uint32_t)( f->raw[0]
                     | ((uint32_t)f->raw[1] << 8)
                     | ((uint32_t)f->raw[2] << 16)
                     | ((uint32_t)f->raw[3] << 24));
}

int8_t xcli_field_i8(const XcliField_t *f)
{
    return (int8_t)xcli_field_u8(f);
}

int32_t xcli_field_i32(const XcliField_t *f)
{
    return (int32_t)xcli_field_u32(f);
}

float xcli_field_f32(const XcliField_t *f)
{
    float v = 0.0f;
    if (f && f->raw_len >= 4) { memcpy(&v, f->raw, 4); }
    return v;
}

int xcli_field_bool(const XcliField_t *f)
{
    return xcli_field_u8(f) ? 1 : 0;
}

size_t xcli_field_str(const XcliField_t *f, char *dst, size_t dsz)
{
    if (!f || !dst || dsz == 0) return 0;
    size_t n = (f->raw_len < dsz - 1) ? f->raw_len : dsz - 1;
    memcpy(dst, f->raw, n);
    dst[n] = '\0';
    return n;
}

const uint8_t *xcli_field_bytes(const XcliField_t *f, uint16_t *out_len)
{
    if (!f) { if (out_len) *out_len = 0; return NULL; }
    if (out_len) *out_len = f->raw_len;
    return f->raw;
}
