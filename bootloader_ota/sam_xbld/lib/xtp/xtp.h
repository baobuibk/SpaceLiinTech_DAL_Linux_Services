/*
 * xtp.h  -  xTP: eXtensible Transport Protocol
 * 
 * Wire format:
 *   [C0][DE][TM][SRC*][DST*][ID_L][ID_H][SEQ*][SS*][SI*][ST*]
 *   [LEN_L][LEN_H*][DATA...][ECC0*][ECC1*][CRC*][MAC*][DA][ED]
 *
 * PROT|XFER byte (TM) - static, agreed at compile time:
 *   PROT nibble [7:4]: CRC_EN | CRC_TYPE | ECC_EN  | HMAC_EN
 *   XFER nibble [3:0]: LEN_16 | SEG_EN   | ARQ_EN  | ADR_EN
 *
 * ID field: always 16-bit (uint16_t), little-endian.
 *   0x0000..0xFEFF - application commands.
 *   0xFF00..0xFFFF - RESERVED for ARQ control frames:
 *     0xFFFE=ACK  0xFFFD=NACK  0xFFFC=RST
 *
 * LEN field: 8-bit (LEN_16=0) or 16-bit little-endian (LEN_16=1).
 *   LEN_16=1 requires ECC=0 (ECC limited to 255-byte blocks).
 *
 * SEQ byte (ARQ_EN=1): bits6:0=seq 0..127 (debug only, no SYN).
 *   Placed in the frame HEADER field - NOT in the application payload.
 *
 * See xtp_config.h for all feature flags.
 *                                                  C.H
 */

#ifndef XTP_H
#define XTP_H

#include <stdint.h>
#include "xtp_config.h"
#include "xtp_crc.h"
#include "xtp_ecc.h"
#include "xtp_hmac.h"
#include "xtp_port.h"
#include "xtp_stats.h"

#ifdef __cplusplus
extern "C" {
#endif


/* SEQ byte fields (ARQ_EN=1): */
#define XTP_SEQ_SYN_MASK   0x80U   /**< bit 7 = session init */
#define XTP_SEQ_NUM_MASK   0x7FU   /**< bits 6:0 = seq 0..127 */
#define XTP_SEQ_MAX        127U

/* SEG chunk_idx byte (SEG_EN=1): bit 7 = INTLV (reserved=0), bits 6:0 = idx */
#define XTP_SEG_IDX_MASK   0x7FU
#define XTP_SEG_INTLV_MASK 0x80U
#define XTP_SEG_MAX_TOTAL  128U   /**< max chunks per transfer */

/* =========================================================================
 * PROT|XFER byte - static, agreed at compile time
 *
 * PROT nibble (bits 7:4) - data protection:
 *   bit 7  CRC_EN   | bit 6  CRC_TYPE | bit 5  ECC_EN | bit 4  HMAC_EN
 *
 * XFER nibble (bits 3:0) - transport control:
 *   bit 3  LEN_16   | bit 2  SEG_EN   | bit 1  ARQ_EN | bit 0  ADR_EN
 *
 * ID is always 16-bit - no flag needed.
 * =========================================================================*/

/* PROT nibble (bits 7:4): */
#define XTP_PROT_CRC_EN    ((uint8_t)(((XTP_USE_CRC)   ? 1U : 0U) << 3U))
#define XTP_PROT_CRC_TYPE  ((uint8_t)((XTP_USE_CRC && (XTP_USE_CRC_TYPE==32)) ? (1U<<2U) : 0U))
#define XTP_PROT_ECC_EN    ((uint8_t)(((XTP_USE_ECC)   ? 1U : 0U) << 1U))
#define XTP_PROT_HMAC_EN   ((uint8_t)(((XTP_USE_HMAC)  ? 1U : 0U) << 0U))
#define XTP_PROT_FIELD     ((uint8_t)(XTP_PROT_CRC_EN | XTP_PROT_CRC_TYPE | \
                                      XTP_PROT_ECC_EN | XTP_PROT_HMAC_EN))

/* XFER nibble (bits 3:0): */
#define XTP_XFER_LEN_16    ((uint8_t)(((XTP_USE_LEN_16) ? 1U : 0U) << 3U))
#define XTP_XFER_SEG_EN    ((uint8_t)(((XTP_USE_SEG)    ? 1U : 0U) << 2U))
#define XTP_XFER_ARQ_EN    ((uint8_t)(((XTP_USE_ARQ)    ? 1U : 0U) << 1U))
#define XTP_XFER_ADR_EN    ((uint8_t)(((XTP_USE_TARGET) ? 1U : 0U) << 0U))
#define XTP_XFER_FIELD     ((uint8_t)(XTP_XFER_LEN_16 | XTP_XFER_SEG_EN | \
                                      XTP_XFER_ARQ_EN | XTP_XFER_ADR_EN))

#define XTP_PROT_XFER_BYTE ((uint8_t)((XTP_PROT_FIELD << 4U) | XTP_XFER_FIELD))

/* Backward-compat alias: */
#define XTP_TYPE_METHOD_BYTE  XTP_PROT_XFER_BYTE

/* Decode helpers for received nibbles (prot=upper, xfer=lower): */
#define XTP_PN_HAS_CRC(pn)    (((pn) >> 3U) & 1U)
#define XTP_PN_CRC32(pn)      (((pn) >> 2U) & 1U)
#define XTP_PN_HAS_ECC(pn)    (((pn) >> 1U) & 1U)
#define XTP_PN_HAS_HMAC(pn)   (((pn) >> 0U) & 1U)
#define XTP_XN_HAS_LEN16(xn)  (((xn) >> 3U) & 1U)
#define XTP_XN_HAS_SEG(xn)    (((xn) >> 2U) & 1U)
#define XTP_XN_HAS_ARQ(xn)    (((xn) >> 1U) & 1U)
#define XTP_XN_HAS_ADR(xn)    (((xn) >> 0U) & 1U)

/* Backward-compat decode aliases: */
#define XTP_TN_HAS_CRC(tn)    XTP_PN_HAS_CRC(tn)
#define XTP_TN_CRC32(tn)      XTP_PN_CRC32(tn)
#define XTP_TN_HAS_ECC(tn)    XTP_PN_HAS_ECC(tn)
#define XTP_TN_HAS_ADR(tn)    XTP_XN_HAS_ADR(tn)
#define XTP_MN_HAS_LEN16(mn)  XTP_XN_HAS_LEN16(mn)
#define XTP_MN_HAS_SEG(mn)    XTP_XN_HAS_SEG(mn)
#define XTP_MN_HAS_ARQ(mn)    XTP_XN_HAS_ARQ(mn)
#define XTP_MN_HAS_HMAC(mn)   XTP_PN_HAS_HMAC(mn)
#if XTP_USE_TX_LOCK
#define XTP_TX_LOCK(inst)   do { \
    if ((inst)->port.tx_lock != NULL && (inst)->port.tx_lock() != 0) \
        { return XTP_ERR_BUSY; } } while(0)
#define XTP_TX_UNLOCK(inst) do { \
    if ((inst)->port.tx_unlock != NULL) { (inst)->port.tx_unlock(); } } while(0)
#else
#define XTP_TX_LOCK(inst)   ((void)0)
#define XTP_TX_UNLOCK(inst) ((void)0)
#endif

#if XTP_USE_RX_LOCK
#define XTP_RX_LOCK(inst)   do { \
    if ((inst)->port.rx_lock != NULL && (inst)->port.rx_lock() != 0) \
        { return XTP_ERR_BUSY; } } while(0)
#define XTP_RX_UNLOCK(inst) do { \
    if ((inst)->port.rx_unlock != NULL) { (inst)->port.rx_unlock(); } } while(0)
#else
#define XTP_RX_LOCK(inst)   ((void)0)
#define XTP_RX_UNLOCK(inst) ((void)0)
#endif

/* ARQ control frame IDs - always 16-bit.
 * Upper byte 0xFF is RESERVED; app IDs must be in 0x0000..0xFEFF. */
#define XTP_ARQ_ID_ACK    ((xTP_ID_t)0xFFFEU)
#define XTP_ARQ_ID_NACK   ((xTP_ID_t)0xFFFDU)
#define XTP_ARQ_ID_RST    ((xTP_ID_t)0xFFFCU)

#define XTP_OH_START   2U
#define XTP_OH_TM      1U
#define XTP_OH_ID      2U   /**< ID always 16-bit. */
#define XTP_OH_LEN     ((XTP_USE_LEN_16) ? 2U : 1U)
#define XTP_OH_STOP    2U
#define XTP_OH_ADDR    ((XTP_USE_TARGET)  ? 2U : 0U)
#define XTP_OH_SEQ     ((XTP_USE_ARQ)     ? 1U : 0U)
/* NOTE: XTP_OH_SEG = minimum SEG overhead (stream=1B + idx=1B + total=1B).
 * Actual overhead may be larger if LEB128 encodes large idx/total values. */
#define XTP_OH_SEG     ((XTP_USE_SEG)     ? 3U : 0U)
/* XTP_OH_CRC: XTP_USE_CRC_TYPE is always defined unconditionally in xtp_config.h */
#define XTP_OH_CRC     ((XTP_USE_CRC)     ? ((XTP_USE_CRC_TYPE==32) ? 4U : 2U) : 0U)
#define XTP_OH_ECC     ((XTP_USE_ECC)     ? 2U : 0U)
#define XTP_OH_HMAC    ((XTP_USE_HMAC)    ? 4U : 0U)

#define XTP_FRAME_OVERHEAD  (XTP_OH_START + XTP_OH_TM + XTP_OH_ADDR + XTP_OH_ID + \
                             XTP_OH_SEQ + XTP_OH_SEG + XTP_OH_LEN + XTP_OH_ECC + \
                             XTP_OH_CRC + XTP_OH_HMAC + XTP_OH_STOP)

#define XTP_PKT_MIN_SIZE   XTP_FRAME_OVERHEAD
#define XTP_PKT_MAX_SIZE   (XTP_FRAME_OVERHEAD + XTP_DATA_MAX_LEN)

/* =========================================================================
 * ID type - always 16-bit
 * =========================================================================*/
typedef uint16_t xTP_ID_t;

/* =========================================================================
 * Return codes
 * =========================================================================*/
typedef enum {
    XTP_OK             = 0x00,
    XTP_ERR            = 0x01,
    XTP_INPROG         = 0x02,
    XTP_VALID          = 0x03,
    XTP_ERR_TYPEMETHOD = 0x04,
    XTP_ERR_ADDR       = 0x05,
    XTP_ERR_CRC        = 0x06,
    XTP_ERR_STOP       = 0x07,
    XTP_WAITDATA       = 0x08,
    XTP_ERR_MEM        = 0x09,
    XTP_ERR_TIMEOUT    = 0x0A,
    XTP_ERR_BUSY       = 0x0B,
    XTP_ERR_HMAC       = 0x0C,
    XTP_ERR_ECC        = 0x0D,
    XTP_ERR_SEQ        = 0x0E,
} xTP_Return_t;

/* =========================================================================
 * Decoder FSM states
 * =========================================================================*/
typedef enum {
    XTP_DECODE_START1 = 0,
    XTP_DECODE_START2,
    XTP_DECODE_TYPEMETHOD,
#if XTP_USE_TARGET
    XTP_DECODE_SRC,
    XTP_DECODE_DST,
#endif
    XTP_DECODE_ID,          /**< ID low byte  (always present). */
    XTP_DECODE_ID_HIGH,     /**< ID high byte (always present, ID is always 16-bit). */
#if XTP_USE_ARQ
    XTP_DECODE_SEQ,
#endif
#if XTP_USE_SEG
    XTP_DECODE_SEG_STREAM,
    XTP_DECODE_SEG_IDX,
    XTP_DECODE_SEG_TOTAL,
#endif
    XTP_DECODE_LEN,         /**< LEN low byte (always present). */
#if XTP_USE_LEN_16
    XTP_DECODE_LEN_HIGH,    /**< LEN high byte (LEN_16=1 only). */
#endif
    XTP_DECODE_DATA,
#if XTP_USE_ECC
    XTP_DECODE_ECC0,
    XTP_DECODE_ECC1,
#endif
#if XTP_USE_CRC
    XTP_DECODE_CRC1,
    XTP_DECODE_CRC2,
#if (XTP_USE_CRC_TYPE == 32)
    XTP_DECODE_CRC3,
    XTP_DECODE_CRC4,
#endif
#endif
#if XTP_USE_HMAC
    XTP_DECODE_MAC1,
    XTP_DECODE_MAC2,
    XTP_DECODE_MAC3,
    XTP_DECODE_MAC4,
#endif
    XTP_DECODE_STOP1,
    XTP_DECODE_STOP2,
    XTP_DECODE_SENTINEL
} xTP_DecodeState_t;

/* =========================================================================
 * Core receive state (xTP_Data_t)
 * =========================================================================*/
typedef struct {
    xTP_DecodeState_t  state;
    uint8_t            typemethod;         /**< Expected TM byte. */
#if XTP_USE_TARGET
    uint8_t            src;                /**< Own address. */
    uint8_t            src_addr_received;  /**< Sender's address. */
#endif
    xTP_ID_t           id;                 /**< Decoded command ID. */
#if XTP_USE_ARQ
    uint8_t            seq_byte;           /**< Raw SEQ byte (SYN|seq) from header. */
#endif
#if XTP_USE_SEG
    uint8_t            seg_stream;         /**< stream_id. */
    uint32_t           seg_idx;            /**< chunk_idx (LEB128 decoded). */
    uint32_t           seg_total;          /**< total_chunks (0=streaming). */
    uint8_t            seg_leb_shift;      /**< LEB128 accumulator bit shift. */
#endif
    uint16_t           length;             /**< Payload length (0..255). */
    uint8_t            data[XTP_DATA_MAX_LEN];
    uint16_t           index;              /**< Byte counter in DATA state. */
#if XTP_USE_ECC
    uint8_t            ecc_received[2];
    uint8_t            ecc_corrected;
#endif
#if XTP_USE_CRC
    xTP_CRC_t          crc_ctx;
    uint32_t           crc_received;
    uint8_t            crc_bytes_buf[6];   /**< ECC0+ECC1 (if ECC) + CRC bytes for HMAC input. */
    uint8_t            crc_bytes_len;
#endif
#if XTP_USE_HMAC
    uint8_t            mac_hdr_buf[16];    /**< Header bytes captured for MAC. */
    uint8_t            mac_hdr_len;
    uint32_t           mac_received;       /**< Accumulated received MAC. */
    uint8_t            mac_byte_idx;
#endif
    uint32_t           last_rx_tick;
} xTP_Data_t;

/* =========================================================================
 * Command dispatch
 * =========================================================================*/
struct xTP_Instance_s;
typedef void (*xTP_Handler_t)(struct xTP_Instance_s *inst,
                               const uint8_t *payload, uint16_t len);
typedef struct {
    xTP_ID_t       id;
    xTP_Handler_t  handler;
} xTP_Command_t;

/* =========================================================================
 * Forward-declare ARQ and SEG types (defined in xtp_arq.h / xtp_seg.h)
 * =========================================================================*/
#if XTP_USE_ARQ
typedef struct xTP_ARQ_s xTP_ARQ_t;
#endif
#if XTP_USE_SEG
typedef struct xTP_Seg_Stream_s xTP_Seg_Stream_t;
#endif

/* =========================================================================
 * Instance (one per independent channel)
 * =========================================================================*/
typedef struct xTP_Instance_s {
    xTP_Data_t     data;
    xTP_Port_t     port;
    xTP_Command_t  cmd_table[XTP_MAX_COMMANDS];
    uint8_t        cmd_count;

    /** Channel statistics - NULL to disable (attach via xTP_Stats_Attach). */
    xTP_Stats_t   *stats;

    /** Per-instance receive callback.  NULL - weak xTP_ApplicationHandler(). */
    void (*on_receive)(struct xTP_Instance_s *inst, uint8_t src_addr,
                       xTP_ID_t id, const uint8_t *payload, uint16_t len);

#if XTP_USE_ARQ
    xTP_ARQ_t     *arq;   /**< Pointer to ARQ state (set by xTP_ARQ_Init). */
#endif
#if XTP_USE_SEG
    xTP_Seg_Stream_t *seg_streams;  /**< Pointer to stream array (set by xTP_SEG_Init). */
#endif
} xTP_Instance_t;

/* =========================================================================
 * Core API
 * =========================================================================*/
xTP_Return_t xTP_Init(xTP_Instance_t *inst, uint8_t src_addr);
void         xTP_Reset(xTP_Instance_t *inst);
xTP_Return_t xTP_Read(xTP_Instance_t *inst, const uint8_t *rx_byte);

/**
 * xTP_Send - encode and transmit one frame with default SEQ/SEG headers.
 * SEQ header = 0x00, SEG = {stream=0, idx=0, total=1}.
 * For ARQ sends, use xTP_ARQ_Send() instead.
 */
xTP_Return_t xTP_Send(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                      const void *data, uint16_t len);

/**
 * xTP_SendEx - full-control encoder.
 * @param seq_hdr    SEQ header byte (SYN|seq).
 * @param seg_stream SEG stream_id.
 * @param seg_idx    SEG chunk index (LEB128 encoded, unbounded).
 * @param seg_total  SEG total chunks (LEB128); 0 = streaming mode.
 * @param len        Payload byte count. Max 255 when LEN_16=0 or ECC=1;
 *                   up to 65535 when LEN_16=1 and ECC=0.
 */
xTP_Return_t xTP_SendEx(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                         uint8_t seq_hdr,
                         uint8_t seg_stream, uint32_t seg_idx, uint32_t seg_total,
                         const void *data, uint16_t len);

xTP_Return_t xTP_Process(xTP_Instance_t *inst);

/* Command dispatch */
void                  xTP_RegisterCommand(xTP_Instance_t *inst, xTP_ID_t id,
                                           xTP_Handler_t handler);
const xTP_Command_t  *xTP_GetCommandTable(const xTP_Instance_t *inst);
uint8_t               xTP_GetCommandTableSize(const xTP_Instance_t *inst);

/* Global singleton helpers */
xTP_Return_t    xTP_InitDefault(uint8_t src_addr);
xTP_Return_t    xTP_ProcessGlobal(void);
xTP_Instance_t *xTP_GetGlobalInstance(void);
#define XTP_GLOBAL (xTP_GetGlobalInstance())

/* Application callback (weak - override in application code) */
void xTP_ApplicationHandler(xTP_Instance_t *inst, uint8_t src_addr,
                             xTP_ID_t id, const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif
#endif /* XTP_H */