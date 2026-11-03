/* bee_dal_proto.h
 *
 */

#ifndef BEE_DAL_PROTO_H
#define BEE_DAL_PROTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Wire constants
 * ========================================================================= */
#define BEE_DAL_START_BYTE   0xA5U
#define BEE_DAL_ACK_ERR_MARK 0xFFU   /* ACK/ERR value meaning "error, see ERRCODE" */
#define BEE_DAL_MARK_RESERVED 0xFFU  /* dat_mark: reserved */

#define BEE_DAL_MAX_PAYLOAD  32768U

#define BEE_DAL_TC_HDR_LEN   5U   /* START+TYPE+ADDR+LENGTH(2) */
#define BEE_DAL_TM_HDR_LEN   5U   /* START+ACKERR+ADDRERR+LENGTH(2) */
#define BEE_DAL_FRAME_MIN    7U   /* header + CRC16, LENGTH=0 */

#define BEE_DAL_INTER_BYTE_TIMEOUT_MS 2000U

typedef enum {
    BEE_DAL_TYPE_PING      = 0x00,
    BEE_DAL_TYPE_SYS_CFG   = 0x01,
    BEE_DAL_TYPE_EXP_CTRL  = 0x02,
    BEE_DAL_TYPE_DATA_XFER = 0x03,
    BEE_DAL_TYPE_TELEMETRY = 0x04,
    BEE_DAL_TYPE_SFU       = 0x05,
} bee_dal_type_t;

/* EXP gateway extensions within the existing experiment command group. */
#define BEE_DAL_EXP_PING    0x0AU
#define BEE_DAL_EXP_TLM     0x0BU
#define BEE_DAL_EXP_RUN     0x0CU
#define BEE_DAL_EXP_CSP_SUBMIT 0x0DU
#define BEE_DAL_EXP_CSP_RESULT 0x0EU
#define BEE_DAL_EXP_CSP_SNAPSHOT 0x0FU

/* Data Transfer (TYPE=0x03) sub-addresses */
typedef enum {
    BEE_DAL_DAT_LST = 0x00,
    BEE_DAL_DAT_SEL = 0x01,
    BEE_DAL_DAT_GET = 0x02,
    BEE_DAL_DAT_DES  = 0x03,
    BEE_DAL_DAT_DEL  = 0x04,
    BEE_DAL_DAT_MARK = 0x05,
} bee_dal_dat_addr_t;

/* Data Transfer "Data Type" values */
typedef enum {
    BEE_DAL_DATA_TYPE_HK   = 0,
    BEE_DAL_DATA_TYPE_SCI1 = 1,
    BEE_DAL_DATA_TYPE_SCI2 = 2,
} bee_dal_data_type_t;

/* Telemetry (TYPE=0x04) sub-addresses */
typedef enum {
    BEE_DAL_TM_GET = 0x00,
    BEE_DAL_TM_CFG = 0x01,
    BEE_DAL_TM_OBC_GET = 0x02,
} bee_dal_tm_addr_t;

/* TM ERRCODE values (sent when ACK/ERR == BEE_DAL_ACK_ERR_MARK) */
typedef enum {
    BEE_DAL_ERR_TC_INVALID = 0x00,
    BEE_DAL_ERR_DATA       = 0x01,
    BEE_DAL_ERR_MEMORY     = 0x02,
    BEE_DAL_ERR_UNKNOWN    = 0x03,
} bee_dal_errcode_t;

/* =========================================================================
 * Decoded TC frame
 * ========================================================================= */
typedef struct {
    uint8_t  type;
    uint8_t  addr;
    uint16_t length;
    uint8_t  payload[BEE_DAL_MAX_PAYLOAD];
} bee_dal_tc_frame_t;

/* =========================================================================
 * Decoder -- pure byte-stream FSM, no I/O.
 * ========================================================================= */
typedef enum {
    BEE_DAL_DEC_NEED_MORE = 0,  /* keep feeding bytes */
    BEE_DAL_DEC_FRAME_OK,       /* frame decoded, CRC verified */
    BEE_DAL_DEC_CRC_ERROR,      /* frame complete but CRC mismatch, discarded */
    BEE_DAL_DEC_TIMEOUT,        /* mid-frame, no byte for too long -- force-resynced */
} bee_dal_dec_result_t;

typedef struct {
    int      state;
    uint8_t  type;
    uint8_t  addr;
    uint16_t length;
    uint16_t data_idx;
    uint8_t  payload[BEE_DAL_MAX_PAYLOAD];
    uint16_t crc_calc;
    uint16_t crc_rx;
    uint32_t idle_ms;    /* time since the last byte arrived, mid-frame only */
} bee_dal_decoder_t;

void bee_dal_decoder_init(bee_dal_decoder_t *dec);

bee_dal_dec_result_t bee_dal_decoder_feed(bee_dal_decoder_t *dec, uint8_t byte,
                                           bee_dal_tc_frame_t *out);

bee_dal_dec_result_t bee_dal_decoder_idle(bee_dal_decoder_t *dec, uint32_t elapsed_ms);

/* =========================================================================
 * Encoder -- pure, no I/O.
 * ========================================================================= */

/**
 * Encode one TM frame into out[]. Returns encoded length, or -1 if
 * out_cap is too small.
 */
int bee_dal_encode_tm(uint8_t ack_err, uint8_t addr_errcode,
                      const uint8_t *payload, uint16_t len,
                      uint8_t *out, size_t out_cap);

/** Convenience: encode a protocol-level error reply (ACK/ERR=0xFF, LEN=0). */
int bee_dal_encode_err(uint8_t errcode, uint8_t *out, size_t out_cap);

/* =========================================================================
 * Payload marshalling helpers 
 * ========================================================================= */
void bee_dal_put_u8 (uint8_t *buf, size_t *off, uint8_t  v);
void bee_dal_put_u16(uint8_t *buf, size_t *off, uint16_t v);
void bee_dal_put_u32(uint8_t *buf, size_t *off, uint32_t v);
void bee_dal_put_i8 (uint8_t *buf, size_t *off, int8_t   v);
void bee_dal_put_i16(uint8_t *buf, size_t *off, int16_t  v);
void bee_dal_put_str(uint8_t *buf, size_t *off, const char *s, size_t fixed_len);

uint8_t  bee_dal_get_u8 (const uint8_t *buf, size_t *off);
uint16_t bee_dal_get_u16(const uint8_t *buf, size_t *off);
uint32_t bee_dal_get_u32(const uint8_t *buf, size_t *off);

#ifdef __cplusplus
}
#endif
#endif /* BEE_DAL_PROTO_H */
