/*
 * xtp.c
 * V1.1.0
 *    - Fix logic ARQ NAK send
 *                                                  C.H
 */

#include <stdio.h>
#include "xtp.h"
#include "xtp_arq.h"
#include "xtp_seg.h"

#define DETAIL_LOG 0
/* =========================================================================
 * Global singleton
 * =========================================================================*/
static xTP_Instance_t g_xtp_instance;

/* =========================================================================
 * Internal helpers
 * =========================================================================*/

/* --- CRC helpers -------------------------------------------------------- */
#if XTP_USE_CRC
static void xtp_crc_reset(xTP_Data_t *d)
{
    xTP_CRC_Init(&d->crc_ctx);
    d->crc_received  = 0U;
#if XTP_USE_HMAC
    d->crc_bytes_len = 0U;
#endif
}
static inline void xtp_crc_update(xTP_Data_t *d, uint8_t b)
{
    xTP_CRC_Update(&d->crc_ctx, b);
}
#else
static inline void xtp_crc_reset(xTP_Data_t *d)  { (void)d; }
static inline void xtp_crc_update(xTP_Data_t *d, uint8_t b) { (void)d; (void)b; }
#endif

/* --- MAC header capture ------------------------------------------------- */
#if XTP_USE_HMAC
static inline void xtp_mac_hdr(xTP_Data_t *d, uint8_t b)
{
    if (d->mac_hdr_len < (uint8_t)sizeof(d->mac_hdr_buf)) {
        d->mac_hdr_buf[d->mac_hdr_len++] = b;
    }
}
static inline void xtp_mac_crc(xTP_Data_t *d, uint8_t b)
{
    /* Buffer holds ECC0+ECC1 (2B) + CRC (2 or 4B) = up to 6B total. */
    if (d->crc_bytes_len < 6U) {
        d->crc_bytes_buf[d->crc_bytes_len++] = b;
    }
}
static void xtp_mac_reset(xTP_Data_t *d)
{
    d->mac_hdr_len  = 0U;
    d->mac_received = 0U;
    d->mac_byte_idx = 0U;
    d->crc_bytes_len = 0U;
}
#else
static inline void xtp_mac_hdr(xTP_Data_t *d, uint8_t b)  { (void)d; (void)b; }
static inline void xtp_mac_crc(xTP_Data_t *d, uint8_t b)  { (void)d; (void)b; }
static inline void xtp_mac_reset(xTP_Data_t *d)            { (void)d; }
#endif

/* Helper: update CRC and capture for MAC header simultaneously. */
static inline void xtp_hdr_byte(xTP_Data_t *d, uint8_t b)
{
    xtp_crc_update(d, b);
    xtp_mac_hdr(d, b);
}

/* =========================================================================
 * State-transition helpers
 * =========================================================================*/
static inline void xtp_go(xTP_Data_t *d, xTP_DecodeState_t s)
{
    d->state = s;
    d->index = 0U;
}

static void xtp_next_after_id(xTP_Data_t *d)
{
    xtp_go(d, XTP_DECODE_ID_HIGH);
}
static void xtp_next_after_id_high(xTP_Data_t *d)
{
#if XTP_USE_ARQ
    xtp_go(d, XTP_DECODE_SEQ);
#elif XTP_USE_SEG
    xtp_go(d, XTP_DECODE_SEG_STREAM);
#else
    xtp_go(d, XTP_DECODE_LEN);
#endif
}
#if XTP_USE_ARQ
static void xtp_next_after_seq(xTP_Data_t *d)
{
#if XTP_USE_SEG
    xtp_go(d, XTP_DECODE_SEG_STREAM);
#else
    xtp_go(d, XTP_DECODE_LEN);
#endif
}
#endif /* XTP_USE_ARQ */
#if XTP_USE_SEG
static void xtp_next_after_seg(xTP_Data_t *d)
{
    xtp_go(d, XTP_DECODE_LEN);
}
#endif /* XTP_USE_SEG */
static void xtp_next_after_len_high(xTP_Data_t *d)
{
    if (d->length > 0U) {
        xtp_go(d, XTP_DECODE_DATA);
    } else {
#if XTP_USE_ECC
        xtp_go(d, XTP_DECODE_ECC0);
#elif XTP_USE_CRC
        xtp_go(d, XTP_DECODE_CRC1);
#elif XTP_USE_HMAC
        xtp_go(d, XTP_DECODE_MAC1);
#else
        xtp_go(d, XTP_DECODE_STOP1);
#endif
    }
}
static void xtp_next_after_len(xTP_Data_t *d)
{
#if XTP_USE_LEN_16
    /* Low byte received - wait for high byte before deciding DATA/ECC/CRC. */
    xtp_go(d, XTP_DECODE_LEN_HIGH);
#else
    /* 8-bit LEN: length is complete, decide next state now. */
    xtp_next_after_len_high(d);
#endif
}
static void xtp_next_after_data(xTP_Data_t *d)
{
#if XTP_USE_ECC
    xtp_go(d, XTP_DECODE_ECC0);
#elif XTP_USE_CRC
    xtp_go(d, XTP_DECODE_CRC1);
#elif XTP_USE_HMAC
    xtp_go(d, XTP_DECODE_MAC1);
#else
    xtp_go(d, XTP_DECODE_STOP1);
#endif
}
#if XTP_USE_ECC
static void xtp_next_after_ecc(xTP_Data_t *d)
{
#if XTP_USE_CRC
    xtp_go(d, XTP_DECODE_CRC1);
#elif XTP_USE_HMAC
    xtp_go(d, XTP_DECODE_MAC1);
#else
    xtp_go(d, XTP_DECODE_STOP1);
#endif
}
#endif /* XTP_USE_ECC */

#if XTP_USE_CRC
static void xtp_next_after_crc(xTP_Data_t *d)
{
#if XTP_USE_HMAC
    xtp_go(d, XTP_DECODE_MAC1);
#else
    xtp_go(d, XTP_DECODE_STOP1);
#endif
}
#endif /* XTP_USE_CRC */

/* =========================================================================
 * xTP_Init / xTP_Reset
 * =========================================================================*/
xTP_Return_t xTP_Init(xTP_Instance_t *inst, uint8_t src_addr)
{
    if (inst == NULL) { return XTP_ERR; }
    // memset(&inst->port, 0, sizeof(inst->port));
    inst->data.typemethod = XTP_PROT_XFER_BYTE;   /* PROT|XFER config byte */
    inst->cmd_count       = 0U;
    inst->on_receive      = NULL;
    inst->stats           = NULL;
#if XTP_USE_ARQ
    inst->arq             = NULL;
#endif
#if XTP_USE_SEG
    inst->seg_streams     = NULL;
#endif
#if XTP_USE_TARGET
    inst->data.src = src_addr;
    xTP_Log("xTP_Init src=0x%02X PX=0x%02X", (unsigned)src_addr,
            (unsigned)XTP_PROT_XFER_BYTE);
#else
    (void)src_addr;
    xTP_Log("xTP_Init PX=0x%02X", (unsigned)XTP_PROT_XFER_BYTE);
#endif
    xTP_Reset(inst);
    return XTP_OK;
}

void xTP_Reset(xTP_Instance_t *inst)
{
    xTP_Data_t *d;
    uint32_t    tick;
    if (inst == NULL) { return; }
    d    = &inst->data;
    tick = d->last_rx_tick;   /* preserve across reset */
    xTP_MemSet(d->data, 0, sizeof(d->data));
    d->index   = 0U;
    d->length  = 0U;
    d->id      = (xTP_ID_t)0U;
#if XTP_USE_TARGET
    d->src_addr_received = 0U;
#endif
#if XTP_USE_ARQ
    d->seq_byte = 0U;
#endif
#if XTP_USE_SEG
    d->seg_stream = 0U; d->seg_idx = 0U; d->seg_total = 0U; d->seg_leb_shift = 0U;
#endif
#if XTP_USE_ECC
    d->ecc_received[0] = 0U; d->ecc_received[1] = 0U; d->ecc_corrected = 0U;
#endif
    xtp_crc_reset(d);
    xtp_mac_reset(d);
    d->state        = XTP_DECODE_START1;
    d->last_rx_tick = tick;
}

/* =========================================================================
 * xTP_Read - byte-at-a-time FSM
 * =========================================================================*/
xTP_Return_t xTP_Read(xTP_Instance_t *inst, const uint8_t *rx_byte)
{
    uint8_t    b;
    xTP_Data_t *d;

    if (inst == NULL || rx_byte == NULL) { return XTP_ERR; }
    d = &inst->data;
    b = *rx_byte;

    switch (d->state) {

    /* ------------------------------------------------------------------ */
    case XTP_DECODE_START1:
        /* Accept any START1 with upper nibble 0xC (any protocol version). */
        if ((b & 0xF0U) == 0xC0U) {
            xTP_Reset(inst);
            xtp_go(d, XTP_DECODE_START2);
        }
        break;

    case XTP_DECODE_START2:
        if (b == XTP_START2_BYTE) { xtp_go(d, XTP_DECODE_TYPEMETHOD); }
        else { xTP_Log("ERR START2 0x%02X", (unsigned)b); xTP_Reset(inst); }
        break;

    /* ------------------------------------------------------------------ */
    case XTP_DECODE_TYPEMETHOD:
        if (b != d->typemethod) {
            xTP_Log("ERR TM got=0x%02X exp=0x%02X", (unsigned)b,
                    (unsigned)d->typemethod);
            xTP_Reset(inst);
            return XTP_ERR_TYPEMETHOD;
        }
        xtp_hdr_byte(d, b);
        xTP_Log("PX=0x%02X", (unsigned)b);
#if XTP_USE_TARGET
        xtp_go(d, XTP_DECODE_SRC);
#else
        xtp_go(d, XTP_DECODE_ID);
#endif
        break;

    /* ------------------------------------------------------------------ */
#if XTP_USE_TARGET
    case XTP_DECODE_SRC:
        d->src_addr_received = b;
        xtp_hdr_byte(d, b);
        xTP_Log("SRC=0x%02X", (unsigned)b);
        xtp_go(d, XTP_DECODE_DST);
        break;

    case XTP_DECODE_DST:
        if (b != d->src && b != XTP_BROADCAST_ADDR) {
            xTP_Log("DST=0x%02X not for us (own=0x%02X)", (unsigned)b, (unsigned)d->src);
            xTP_Reset(inst);
            return XTP_ERR_ADDR;
        }
        xtp_hdr_byte(d, b);
        xTP_Log("DST=0x%02X OK", (unsigned)b);
        xtp_go(d, XTP_DECODE_ID);
        break;
#endif

    /* ------------------------------------------------------------------ */
    case XTP_DECODE_ID:
        d->id = (xTP_ID_t)b;          /* low byte */
        xtp_hdr_byte(d, b);
        xtp_next_after_id(d);          /* XTP_DECODE_ID_HIGH always */
        break;

    case XTP_DECODE_ID_HIGH:
        d->id |= (xTP_ID_t)((xTP_ID_t)b << 8U);   /* high byte */
        xtp_hdr_byte(d, b);
        xtp_next_after_id_high(d);
        break;

    /* ------------------------------------------------------------------ */
#if XTP_USE_ARQ
    case XTP_DECODE_SEQ:
        d->seq_byte = b;
        xtp_hdr_byte(d, b);
        xTP_Log("SEQ=0x%02X",(unsigned)b);
        xtp_next_after_seq(d);
        break;
#endif

    /* ------------------------------------------------------------------ */
#if XTP_USE_SEG
    case XTP_DECODE_SEG_STREAM:
        d->seg_stream    = b;
        d->seg_idx       = 0U;
        d->seg_total     = 0U;
        d->seg_leb_shift = 0U;
        xtp_hdr_byte(d, b);
        xtp_go(d, XTP_DECODE_SEG_IDX);
        break;

    case XTP_DECODE_SEG_IDX:
        /* LEB128: accumulate until MSB=0. */
        if (d->seg_leb_shift < 28U) {  /* guard: max 4 bytes */
            d->seg_idx |= (uint32_t)(b & 0x7FU) << d->seg_leb_shift;
        }
        d->seg_leb_shift += 7U;
        xtp_hdr_byte(d, b);
        if (!(b & 0x80U)) {
            /* Last LEB128 byte for idx - move to total. */
            d->seg_leb_shift = 0U;
            xtp_go(d, XTP_DECODE_SEG_TOTAL);
        }
        /* else: stay in SEG_IDX for next byte */
        break;

    case XTP_DECODE_SEG_TOTAL:
        /* LEB128: accumulate until MSB=0. */
        if (d->seg_leb_shift < 28U) {
            d->seg_total |= (uint32_t)(b & 0x7FU) << d->seg_leb_shift;
        }
        d->seg_leb_shift += 7U;
        xtp_hdr_byte(d, b);
        if (!(b & 0x80U)) {
            xTP_Log("SEG stream=%u idx=%lu total=%lu%s",
                    (unsigned)d->seg_stream,
                    (unsigned long)d->seg_idx,
                    (unsigned long)d->seg_total,
                    d->seg_total == 0U ? " (stream)" : "");
            xtp_next_after_seg(d);
        }
        /* else: stay in SEG_TOTAL for next byte */
        break;
#endif

    /* ------------------------------------------------------------------ */
    case XTP_DECODE_LEN:
        d->length = (uint16_t)b;       /* low byte always */
        xtp_hdr_byte(d, b);
#if DETAIL_LOG
        xTP_Log("LEN_L=0x%02X", (unsigned)b);
#endif
        xtp_next_after_len(d);
        break;

#if XTP_USE_LEN_16
    case XTP_DECODE_LEN_HIGH:
        d->length |= (uint16_t)((uint16_t)b << 8U);  /* high byte */
        xtp_hdr_byte(d, b);
        xTP_Log("LEN=%u (16-bit)", (unsigned)d->length);
        xtp_next_after_len_high(d);
        break;
#endif

    /* ------------------------------------------------------------------ */
    case XTP_DECODE_DATA:
        if (d->index >= (uint16_t)sizeof(d->data)) {
            xTP_Log("ERR payload overflow");
            xTP_Reset(inst); return XTP_ERR_MEM;
        }
        d->data[d->index++] = b;
        xtp_crc_update(d, b);   /* data NOT in mac_hdr (passed separately to HMAC) */
        if (d->index == d->length) {
#if DETAIL_LOG
            xTP_Log("DATA %u bytes", (unsigned)d->length);
#endif
            xtp_next_after_data(d);
        }
        break;

    /* ------------------------------------------------------------------ */
#if XTP_USE_ECC
    case XTP_DECODE_ECC0:
        d->ecc_received[0] = b;
        xtp_crc_update(d, b);   /* ECC covered by CRC */
        xtp_mac_crc(d, b);      /* ECC NOT in mac_hdr - treated like CRC bytes */
        xtp_go(d, XTP_DECODE_ECC1);
        break;

    case XTP_DECODE_ECC1:
        d->ecc_received[1] = b;
        xtp_crc_update(d, b);
        xtp_mac_crc(d, b);
        xtp_next_after_ecc(d);
        break;
#endif

    /* ------------------------------------------------------------------ */
#if XTP_USE_CRC
    case XTP_DECODE_CRC1:
        d->crc_received = (uint32_t)b;
        xtp_mac_crc(d, b);
        xtp_go(d, XTP_DECODE_CRC2);
        break;

    case XTP_DECODE_CRC2:
        d->crc_received |= (uint32_t)b << 8U;
        xtp_mac_crc(d, b);
#if (XTP_USE_CRC_TYPE == 32)
        xtp_go(d, XTP_DECODE_CRC3);
#else
        /* CRC-16 done: verify */
        {
            uint16_t calc = (uint16_t)xTP_CRC_Finish(&d->crc_ctx);
            uint16_t recv = (uint16_t)d->crc_received;
            if (calc == recv) {
#if DETAIL_LOG
                xTP_Log("CRC16 OK");
#endif
                xtp_next_after_crc(d);
            } else {
#if XTP_USE_ECC
                xTP_ECC_Result_t er = xTP_ECC_Decode(
                    d->data, (uint8_t)d->length,
                    d->ecc_received[0], d->ecc_received[1]);
                if (er == XTP_ECC_CORRECTED) {   
                    d->ecc_corrected = 1U;
                    xTP_Log("ECC corrected 1B (CRC16 retry)");
                    xtp_next_after_crc(d);
                } else
#endif
                {
                    xTP_Log("ERR CRC16 calc=0x%04X recv=0x%04X", (unsigned)calc, (unsigned)recv);
                    XTP_STAT_INC(inst, rx_crc_errors);
                #if XTP_USE_ARQ
                    xTP_ARQ_SendNack(inst, d->seq_byte);
                #endif
                    xTP_Reset(inst); return XTP_ERR_CRC;
                }
            }
        }
#endif
        break;

#if (XTP_USE_CRC_TYPE == 32)
    case XTP_DECODE_CRC3:
        d->crc_received |= (uint32_t)b << 16U;
        xtp_mac_crc(d, b);
        xtp_go(d, XTP_DECODE_CRC4);
        break;

    case XTP_DECODE_CRC4:
        d->crc_received |= (uint32_t)b << 24U;
        xtp_mac_crc(d, b);
        {
            uint32_t calc = xTP_CRC_Finish(&d->crc_ctx);
            if (calc == d->crc_received) {
#if DETAIL_LOG
                xTP_Log("CRC32 OK");
#endif
                xtp_next_after_crc(d);
            } else {
#if XTP_USE_ECC
                xTP_ECC_Result_t er = xTP_ECC_Decode(
                    d->data, (uint8_t)d->length,
                    d->ecc_received[0], d->ecc_received[1]);
                if (er == XTP_ECC_CORRECTED) { 
                    d->ecc_corrected = 1U;
                    xTP_Log("ECC corrected 1B (CRC32 retry)");
                    xtp_next_after_crc(d);
                } else
#endif
                {
                    xTP_Log("ERR CRC32 calc=%08lX recv=%08lX",
                            (unsigned long)calc, (unsigned long)d->crc_received);
                    XTP_STAT_INC(inst, rx_crc_errors);
                #if XTP_USE_ARQ
                    xTP_ARQ_SendNack(inst, d->seq_byte);
                #endif
                    xTP_Reset(inst); return XTP_ERR_CRC;
                }
            }
        }
        break;
#endif /* CRC32 */
#endif /* XTP_USE_CRC */

    /* ------------------------------------------------------------------ */
#if XTP_USE_HMAC
    case XTP_DECODE_MAC1: d->mac_received  = (uint32_t)b;        xtp_go(d, XTP_DECODE_MAC2); break;
    case XTP_DECODE_MAC2: d->mac_received |= (uint32_t)b <<  8U; xtp_go(d, XTP_DECODE_MAC3); break;
    case XTP_DECODE_MAC3: d->mac_received |= (uint32_t)b << 16U; xtp_go(d, XTP_DECODE_MAC4); break;
    case XTP_DECODE_MAC4:
        d->mac_received |= (uint32_t)b << 24U;
        {
            uint32_t exp = xTP_HMAC_Compute(
                d->mac_hdr_buf, d->mac_hdr_len,
                d->data,        (uint8_t)d->length,
                d->crc_bytes_buf, d->crc_bytes_len);
            if (exp != d->mac_received) {
                xTP_Log("ERR MAC exp=%08lX recv=%08lX",
                        (unsigned long)exp, (unsigned long)d->mac_received);
                xTP_Reset(inst); return XTP_ERR_HMAC;
            }
#if DETAIL_LOG
            xTP_Log("MAC OK");
#endif
        }
        xtp_go(d, XTP_DECODE_STOP1);
        break;
#endif

    /* ------------------------------------------------------------------ */
    case XTP_DECODE_STOP1:
        if (b == XTP_STOP1_BYTE) { xtp_go(d, XTP_DECODE_STOP2); }
        else { xTP_Log("ERR STOP1 0x%02X", (unsigned)b); XTP_STAT_INC(inst, rx_frame_errors); xTP_Reset(inst); return XTP_ERR_STOP; }
        break;

    case XTP_DECODE_STOP2:
        if (b == XTP_STOP2_BYTE) {
            xtp_go(d, XTP_DECODE_START1);
            xTP_Log("<< Got id=0x%02X l=%u", (unsigned)d->id, (unsigned)d->length);
            return XTP_VALID;
        }
        xTP_Log("ERR STOP2 0x%02X", (unsigned)b); 
        XTP_STAT_INC(inst, rx_frame_errors);
        xTP_Reset(inst); 
        return XTP_ERR_STOP;

    default:
        xTP_Reset(inst); return XTP_ERR;
    }

    return (d->state == XTP_DECODE_START1) ? XTP_WAITDATA : XTP_INPROG;
}

/* =========================================================================
 * xTP_SendEx - full-control frame encoder (SEQ header + SEG header params)
 * xTP_Send  - convenience wrapper with default SEQ=0x00, SEG={0,0,1}
 * =========================================================================*/
xTP_Return_t xTP_SendEx(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                         uint8_t seq_hdr,
                         uint8_t seg_stream, uint32_t seg_idx, uint32_t seg_total,
                         const void *data, uint16_t len)
{
    const uint8_t *pdata = (const uint8_t *)data;
    xTP_Data_t    *d;
    uint8_t        byte;
    uint16_t       i;
    xTP_Return_t   res = XTP_OK;
#if XTP_USE_CRC
    xTP_CRC_t      crc;
#endif
#if XTP_USE_HMAC
    uint8_t        mac_hdr[16];
    uint8_t        mac_hdr_len = 0U;
    /* crc_bytes holds [ECC0, ECC1 (if ECC)] + [CRC bytes]: max 2+4=6 bytes. */
    uint8_t        crc_bytes[6];
    uint8_t        crc_bytes_len = 0U;
#define MAC_HDR_PUSH(bv) do { if (mac_hdr_len < 16U) mac_hdr[mac_hdr_len++] = (uint8_t)(bv); } while(0)
#else
#define MAC_HDR_PUSH(bv) (void)(bv)
#endif

    if (inst == NULL)               { return XTP_ERR; }
    if (data == NULL && len > 0U)   { return XTP_ERR; }
    if (len > XTP_DATA_MAX_LEN)     { return XTP_ERR_MEM; }
    d = &inst->data;

    XTP_TX_LOCK(inst);
    /* Check TX buffer space. */
    if (inst->port.get_tx_space != NULL) {
        uint16_t need = (uint16_t)(XTP_PKT_MIN_SIZE + len);
        if (inst->port.get_tx_space() < need) { res = XTP_ERR_MEM; goto done; }
    }

#if XTP_USE_CRC
    xTP_CRC_Init(&crc);
#endif

    /* Helper macros.
     * SEND    : raw byte (not in CRC / MAC).
     * SEND_CH : header byte - CRC + MAC header.
     * SEND_CD : data/ECC byte - CRC only.
     * When XTP_USE_CRC=0, CRC update calls compile away to nothing. */
#define SEND(bv)    do { \
    byte = (uint8_t)(bv); \
    if (inst->port.send_byte) inst->port.send_byte(&byte); \
} while(0)
#if XTP_USE_CRC
#define SEND_CH(bv) do { \
        byte = (uint8_t)(bv);        \
        inst->port.send_byte(&byte); \
        xTP_CRC_Update(&crc, byte);  \
        MAC_HDR_PUSH(byte);          \
    } while(0)
#define SEND_CD(bv) do { \
        byte = (uint8_t)(bv);        \
        inst->port.send_byte(&byte); \
        xTP_CRC_Update(&crc, byte);  \
    } while(0)
#else  /* CRC disabled no CRC update, MAC header still captured */
#define SEND_CH(bv) do { \
        byte = (uint8_t)(bv);        \
        inst->port.send_byte(&byte); \
        MAC_HDR_PUSH(byte);          \
    } while(0)
#define SEND_CD(bv) do { \
        byte = (uint8_t)(bv);        \
        inst->port.send_byte(&byte); \
    } while(0)
#endif

    /* START (not in CRC / MAC). */
    SEND(XTP_START1_BYTE);
    SEND(XTP_START2_BYTE);

    /* Header fields (in CRC + MAC header). */
    SEND_CH(d->typemethod);

#if XTP_USE_TARGET
    SEND_CH(d->src);
    SEND_CH(dst);
#else
    (void)dst;
#endif

    /* ID - always 16-bit, little-endian. */
    SEND_CH((uint8_t)( id       & 0xFFU));
    SEND_CH((uint8_t)((id >> 8) & 0xFFU));

    /* SEQ header byte (ARQ_EN=1).  For non-ARQ sends: 0x00.
     * For ARQ data frames: SYN|seq supplied by xTP_ARQ_Send via pend_seq.
     * For ACK/NACK control frames: 0x00 (seq info is in 1-byte payload). */
#if XTP_USE_ARQ
    SEND_CH(seq_hdr);
#else
    (void)seq_hdr;
#endif

    /* SEG header: stream_id (1 byte) + chunk_idx (LEB128) + total_chunks (LEB128).
     * total=0 signals streaming mode on the receiver. */
#if XTP_USE_SEG
    {
        uint32_t _v; uint8_t _b;
        SEND_CH(seg_stream);
        /* LEB128 chunk_idx */
        _v = seg_idx;
        do { _b = _v & 0x7FU; _v >>= 7U; if (_v) _b |= 0x80U; SEND_CH(_b); } while (_v);
        /* LEB128 total_chunks */
        _v = seg_total;
        do { _b = _v & 0x7FU; _v >>= 7U; if (_v) _b |= 0x80U; SEND_CH(_b); } while (_v);
    }
#else
    (void)seg_stream; (void)seg_idx; (void)seg_total;
#endif

    /* LEN - 8-bit or 16-bit little-endian. */
    SEND_CH((uint8_t)( len       & 0xFFU));
#if XTP_USE_LEN_16
    SEND_CH((uint8_t)((len >> 8) & 0xFFU));
#endif

    /* DATA. */
    for (i = 0U; i < len; i++) { SEND_CD(pdata[i]); }

#if XTP_USE_ECC
    {
        uint8_t e0, e1;
        /* ECC limited to 255 bytes - LEN_16+ECC=1 is blocked at compile time. */
        xTP_ECC_Encode(pdata, (uint8_t)len, &e0, &e1);
        SEND_CD(e0);
        SEND_CD(e1);
#if XTP_USE_HMAC
        /* ECC in crc_bytes (not mac_hdr) - decoder puts ECC here too. */
        crc_bytes[0] = e0;
        crc_bytes[1] = e1;
        crc_bytes_len = 2U;
#endif
    }
#endif

    /* CRC bytes. */
#if XTP_USE_HMAC
    /* crc_bytes[6]: [ECC0, ECC1 (already set above), CRC0..3] */
    /* If ECC disabled, CRC starts at index 0. */
#endif
#if XTP_USE_CRC
    {
#if (XTP_USE_CRC_TYPE == 32)
        uint32_t cv = xTP_CRC_Finish(&crc);
        uint8_t  cb[4];
        cb[0] = (uint8_t)( cv        & 0xFFU);
        cb[1] = (uint8_t)((cv >>  8) & 0xFFU);
        cb[2] = (uint8_t)((cv >> 16) & 0xFFU);
        cb[3] = (uint8_t)((cv >> 24) & 0xFFU);
        SEND(cb[0]); SEND(cb[1]); SEND(cb[2]); SEND(cb[3]);
#if XTP_USE_HMAC
        crc_bytes[crc_bytes_len]     = cb[0];
        crc_bytes[crc_bytes_len + 1] = cb[1];
        crc_bytes[crc_bytes_len + 2] = cb[2];
        crc_bytes[crc_bytes_len + 3] = cb[3];
        crc_bytes_len = (uint8_t)(crc_bytes_len + 4U);
#endif
#else
        uint16_t cv = xTP_CRC_Finish(&crc);
        uint8_t  cb[2];
        cb[0] = (uint8_t)( cv       & 0xFFU);
        cb[1] = (uint8_t)((cv >> 8) & 0xFFU);
        SEND(cb[0]); SEND(cb[1]);
#if XTP_USE_HMAC
        crc_bytes[crc_bytes_len]     = cb[0];
        crc_bytes[crc_bytes_len + 1] = cb[1];
        crc_bytes_len = (uint8_t)(crc_bytes_len + 2U);
#endif
#endif
    }
#endif /* XTP_USE_CRC */

    /* MAC bytes. */
#if XTP_USE_HMAC
    {
        uint32_t mac = xTP_HMAC_Compute(mac_hdr, mac_hdr_len,
                                         pdata, len,
                                         crc_bytes, crc_bytes_len);
        SEND((uint8_t)( mac        & 0xFFU));
        SEND((uint8_t)((mac >>  8) & 0xFFU));
        SEND((uint8_t)((mac >> 16) & 0xFFU));
        SEND((uint8_t)((mac >> 24) & 0xFFU));
    }
#endif

    /* STOP (not in CRC / MAC). */
    SEND(XTP_STOP1_BYTE);
    SEND(XTP_STOP2_BYTE);

    /* Update TX stats. */
    XTP_STAT_INC(inst, tx_frames);
    XTP_STAT_ADD(inst, tx_bytes, len);

#undef SEND
#undef SEND_CH
#undef SEND_CD
#undef MAC_HDR_PUSH

done:
    XTP_TX_UNLOCK(inst);
    return res;
}

xTP_Return_t xTP_Send(xTP_Instance_t *inst, xTP_ID_t id, uint8_t dst,
                      const void *data, uint16_t len)
{
    /* Default: SEQ header = 0x00, SEG = {stream=0, idx=0, total=1}. */
    return xTP_SendEx(inst, id, dst, 0x00U, 0x00U, 0x00U, 0x01U, data, len);
}

/* =========================================================================
 * xTP_Process poll + timeout + dispatch
 * =========================================================================*/
xTP_Return_t xTP_Process(xTP_Instance_t *inst)
{
    uint8_t      byte;
    int          got;
    uint32_t     now;
    xTP_Return_t res;
    xTP_Data_t   *d;
    uint8_t      src;

    if (inst == NULL) { return XTP_ERR; }

    XTP_RX_LOCK(inst);

    d   = &inst->data;
    now = (inst->port.get_tick != NULL) ? inst->port.get_tick() : 0U;
    got = (inst->port.read_byte != NULL) ? inst->port.read_byte(&byte) : -1;

    if (got == 0) {
        d->last_rx_tick = now;
        res = xTP_Read(inst, &byte);

        if (res == XTP_VALID) {
            src = 0U;
#if XTP_USE_TARGET
            src = d->src_addr_received;
#endif
            /* Update RX stats. */
            XTP_STAT_INC(inst, rx_frames);
            XTP_STAT_ADD(inst, rx_bytes, d->length);
#if XTP_USE_ECC
            if (d->ecc_corrected) { XTP_STAT_INC(inst, rx_ecc_corrections); }
#endif
            /* Route through ARQ layer first if attached. */
#if XTP_USE_ARQ
            if (inst->arq != NULL) {
                int consumed = xTP_ARQ_OnReceive(inst, d->id,
                                                  d->data, d->length);
                if (consumed) { goto process_done; }
                /* ARQ accepted frame; SEQ is in header - payload is pure app data. */
                {
                    const uint8_t *app_pl  = d->data;
                    uint16_t       app_len = d->length;
#if XTP_USE_SEG
                    if (inst->seg_streams != NULL) {
                        /* SEG layer reassembles chunks.
                         * When reassembly is complete, xTP_SEG_OnChunk() calls
                         * the on_complete callback AND also dispatches through
                         * on_receive + cmd_table via xTP_SEG_Dispatch().
                         * We do NOT goto process_done here  SEG handles dispatch. */
                        xTP_SEG_OnChunk(inst, app_pl, app_len);
                        goto process_done;
                    }
#endif
                    if (inst->on_receive) {
                        inst->on_receive(inst, src, d->id, app_pl, app_len);
                    } else {
                        xTP_ApplicationHandler(inst, src, d->id, app_pl, app_len);
                    }
                    /* Command table walk. */
                    { uint8_t ci;
                      for (ci = 0U; ci < inst->cmd_count; ci++) {
                          if (inst->cmd_table[ci].id == d->id &&
                              inst->cmd_table[ci].handler != NULL) {
                              inst->cmd_table[ci].handler(inst, app_pl, app_len);
                              break;
                          }
                      }
                    }
                }
            } else
#endif
            {
                /* No ARQ dispatch raw. */
                if (inst->on_receive) {
                    inst->on_receive(inst, src, d->id, d->data, d->length);
                } else {
                    xTP_ApplicationHandler(inst, src, d->id, d->data, d->length);
                }
                { uint8_t ci;
                  for (ci = 0U; ci < inst->cmd_count; ci++) {
                      if (inst->cmd_table[ci].id == d->id &&
                          inst->cmd_table[ci].handler != NULL) {
                          inst->cmd_table[ci].handler(inst, d->data, d->length);
                          break;
                      }
                  }
                }
            }
        }
    } else {
        res = XTP_WAITDATA;
        if (d->state != XTP_DECODE_START1 &&
            XTP_TIMEOUT_MS > 0U &&
            (now - d->last_rx_tick) > (uint32_t)XTP_TIMEOUT_MS) {
            xTP_Log("TIMEOUT state=%d", (int)d->state);
            XTP_STAT_INC(inst, rx_timeouts);
            xTP_Reset(inst);
            d->last_rx_tick = now;
            res = XTP_ERR_TIMEOUT;
        }
    }

#if XTP_USE_ARQ
process_done:
#endif
    XTP_RX_UNLOCK(inst);
    return res;
}

/* =========================================================================
 * Command table
 * =========================================================================*/
void xTP_RegisterCommand(xTP_Instance_t *inst, xTP_ID_t id, xTP_Handler_t handler)
{
    if (inst == NULL || handler == NULL) { return; }
    if (inst->cmd_count >= XTP_MAX_COMMANDS) { return; }
    inst->cmd_table[inst->cmd_count].id      = id;
    inst->cmd_table[inst->cmd_count].handler = handler;
    inst->cmd_count++;
}

const xTP_Command_t *xTP_GetCommandTable(const xTP_Instance_t *inst)
{
    return (inst != NULL) ? inst->cmd_table : NULL;
}

uint8_t xTP_GetCommandTableSize(const xTP_Instance_t *inst)
{
    return (inst != NULL) ? inst->cmd_count : 0U;
}

/* =========================================================================
 * Global singleton helpers
 * =========================================================================*/
xTP_Return_t xTP_InitDefault(uint8_t src_addr)
{
    g_xtp_instance.port = xTP_GetDefaultPort();
    return xTP_Init(&g_xtp_instance, src_addr);
}

xTP_Return_t xTP_ProcessGlobal(void)
{
    return xTP_Process(&g_xtp_instance);
}

xTP_Instance_t *xTP_GetGlobalInstance(void)
{
    return &g_xtp_instance;
}

/* =========================================================================
 * Default application callback (weak)
 * =========================================================================*/
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void xTP_ApplicationHandler(xTP_Instance_t *inst, uint8_t src_addr,
                             xTP_ID_t id, const uint8_t *payload, uint16_t len)
{
    (void)inst; (void)src_addr; (void)id; (void)payload; (void)len;
}
