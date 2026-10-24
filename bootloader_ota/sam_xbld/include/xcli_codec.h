/*
 * xcli_codec.h  
 *
 * Wire format (mirrors xcli.h on MCU):
 *
 *   REQUEST  payload = [DIR=0x00][CMD_ID][SEQ][cmd_string...]
 *   RESPONSE payload = [DIR=0x01][CMD_ID][SEQ][STATUS][TOTAL][IDX][fields...]
 *
 *   Field TLV:  [name_len(1B)][name(N)][type_code(1B)][data_len(2B LE)][data]
 *
 * Usage:
 *   uint8_t req[300];
 *   int len = xcli_encode_request(req, sizeof(req), XBLD_CMD_PING, seq, "bping");
 *   // send req[0..len-1] via xTP
 *
 *   XcliResponse_t rsp;
 *   // accumulate response fragments, then:
 *   xcli_parse_response(payload, plen, &rsp);
 *   const XcliField_t *f = xcli_field_get(&rsp, "hw_id");
 *   if (f) printf("hw_id = %u\n", xcli_field_u8(f));
 */

#ifndef XCLI_CODEC_H
#define XCLI_CODEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants (mirror xcli_config.h / xbld_host.py)
 * =========================================================================*/
#define XCLI_XTP_CMD_ID      0x0100U
#define XCLI_XTP_RESP_ID     0x0101U
#define XBLD_XTP_WRITE_ID    0x0200U
#define XBLD_XTP_WRITE_ACK_ID 0x0201U

#define XCLI_DIR_REQUEST     0x00U
#define XCLI_DIR_RESPONSE    0x01U

#define XCLI_REQ_HDR_LEN     3U   /* [DIR][CMD_ID][SEQ] */
#define XCLI_RESP_HDR_LEN    6U   /* [DIR][CMD_ID][SEQ][STATUS][TOTAL][IDX] */

#define XCLI_STATUS_OK          0x00U
#define XCLI_STATUS_UNKNOWN_CMD 0x01U
#define XCLI_STATUS_ERR         0x02U
#define XCLI_STATUS_BAD_ARG     0x03U
#define XCLI_STATUS_TIMEOUT     0x04U
#define XCLI_STATUS_BUSY        0x05U

#define XCLI_CMD_PING           0x02U
/* xBLD commands */
#define XBLD_CMD_PING               0x40U
#define XBLD_CMD_ERASE              0x41U
#define XBLD_CMD_VERIFY             0x42U
#define XBLD_CMD_JUMP               0x43U
#define XBLD_CMD_STATUS             0x44U
#define XBLD_CMD_READ               0x45U
#define XBLD_CMD_BCOUNT             0x46U
#define XBLD_CMD_BHISTORY           0x47U
#define XBLD_BOOTINFO_HISTORY_LEN   8U

/* Field type codes */
#define XCLI_TYPE_UINT8   0x01U
#define XCLI_TYPE_UINT16  0x02U
#define XCLI_TYPE_UINT32  0x03U
#define XCLI_TYPE_INT8    0x04U
#define XCLI_TYPE_INT16   0x05U
#define XCLI_TYPE_INT32   0x06U
#define XCLI_TYPE_FLOAT   0x07U
#define XCLI_TYPE_BOOL    0x08U
#define XCLI_TYPE_STRING  0x09U
#define XCLI_TYPE_BYTES   0x0AU

/* Max fields per response */
#define XCLI_MAX_FIELDS   32U
#define XCLI_MAX_FIELD_NAME 32U

/* =========================================================================
 * Field
 * =========================================================================*/
typedef struct {
    char     name[XCLI_MAX_FIELD_NAME];
    uint8_t  type_code;
    const uint8_t *raw;     /**< points into the reassembled buffer */
    uint16_t raw_len;
} XcliField_t;

/* =========================================================================
 * Parsed response (single or multi-fragment, fully reassembled)
 * =========================================================================*/
typedef struct {
    uint8_t     cmd_id;
    uint8_t     seq;
    uint8_t     status;
    XcliField_t fields[XCLI_MAX_FIELDS];
    uint8_t     nfields;
    /* Reassembly scratch buffer - caller must keep buf alive while using fields */
    uint8_t    *buf;        /**< heap-allocated reassembly buffer */
    uint32_t    buf_len;
} XcliResponse_t;

/* =========================================================================
 * Fragment accumulator  (handles multi-fragment MCU responses)
 * =========================================================================*/
#define XCLI_MAX_FRAGS  8U

typedef struct {
    uint8_t  used;
    uint8_t  cmd_id;
    uint8_t  seq;
    uint8_t  total;
    uint8_t  have;
    struct {
        uint8_t *data;
        uint16_t len;
    }        frags[XCLI_MAX_FRAGS];
} XcliFragBuf_t;

/* =========================================================================
 * API
 * =========================================================================*/

/**
 * Encode a REQUEST payload into buf.
 * @return number of bytes written, or -1 on overflow.
 */
int xcli_encode_request(uint8_t *buf, size_t buf_cap,
                        uint8_t cmd_id, uint8_t seq,
                        const char *cmd_str);

/**
 * Feed one incoming response fragment.
 * Returns 1 when all fragments are received (rsp is populated).
 * Returns 0 when more fragments are expected.
 * Returns -1 on error (wrong seq/cmd, OOM).
 *
 * On return==1, caller owns rsp->buf and must call xcli_response_free().
 */
int xcli_frag_feed(XcliFragBuf_t *fb, XcliResponse_t *rsp,
                   const uint8_t *payload, uint16_t plen,
                   uint8_t expected_cmd_id, uint8_t expected_seq);

/** Reset the fragment accumulator (e.g. on new request). */
void xcli_frag_reset(XcliFragBuf_t *fb);

/** Free heap memory in a response. */
void xcli_response_free(XcliResponse_t *rsp);

/* =========================================================================
 * Field accessors
 * =========================================================================*/
const XcliField_t *xcli_field_get(const XcliResponse_t *rsp, const char *name);
uint8_t  xcli_field_u8  (const XcliField_t *f);
uint16_t xcli_field_u16 (const XcliField_t *f);
uint32_t xcli_field_u32 (const XcliField_t *f);
int8_t   xcli_field_i8  (const XcliField_t *f);
int32_t  xcli_field_i32 (const XcliField_t *f); 
float    xcli_field_f32 (const XcliField_t *f); 
int      xcli_field_bool(const XcliField_t *f);
/** Copy string into dst (null-terminated). Returns chars copied. */
size_t   xcli_field_str (const XcliField_t *f, char *dst, size_t dsz);
/** Raw bytes. */
const uint8_t *xcli_field_bytes(const XcliField_t *f, uint16_t *out_len);

#ifdef __cplusplus
}
#endif
#endif /* XCLI_CODEC_H */
