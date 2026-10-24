/*
 * xbld_client.h  xBLD host protocol client
 *
 * [1.1.2] Added session-scope port lock (xbld_lock_acquire/release)
 */

#ifndef XBLD_CLIENT_H
#define XBLD_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* xTP library (host build) */
#include "../lib/xtp/xtp.h"
#include "../lib/xtp/xtp_arq.h"
#include "../lib/xtp/xtp_port.h"

#include "xcli_codec.h"
#include "hex_reader.h"
#include "fw_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Config
 * =========================================================================*/
#define XBLD_WRITE_RETRIES      3
#define XBLD_CHUNK_SIZE         240U   /* bytes per write frame */
#define WRITE_ACK_TIMEOUT_MS    5000U
#define ARQ_ACK_TIMEOUT_MS      3000U
#define XCLI_JUMP_TIMEOUT_MS    1000U
#define XCLI_RESP_TIMEOUT_MS    5000U
#define XCLI_ERASE_TIMEOUT_MS  90000U
#define XCLI_VERIFY_TIMEOUT_MS 60000U

#define XCLI_LOCK_DIR      "/home/root/tmp/run/xtp"
#define XCLI_LOCK_WAIT_MS  15000U

/* Write ACK layout */
#define XBLD_WRITE_ACK_OK   0x00U
#define XBLD_WRITE_ACK_ERR  0x01U
#define XBLD_WRITE_ACK_LEN  5U   /* [addr 4B LE][status 1B] */

/* =========================================================================
 * Simple packet queue (fixed-size ring, heap-allocated payloads)
 * =========================================================================*/
#define PKT_QUEUE_DEPTH  16U

typedef struct {
    uint16_t id;
    uint8_t *data;
    uint16_t len;
} PktItem_t;

typedef struct {
    PktItem_t       items[PKT_QUEUE_DEPTH];
    size_t          head, tail, count;
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
} PktQueue_t;

/* =========================================================================
 * Client
 * =========================================================================*/
typedef struct {
    /* Config */
    char     dev[64];
    uint32_t baud;
    int      use_arq;
    int      use_ecc;
    int      crc_type;      /* 16 or 32 */
    int      verbose;

    /* xTP stack */
    xTP_PortCtx_t  port_ctx;
    xTP_Instance_t xtp;
    xTP_ARQ_t      arq;
    volatile int    arq_busy;
    /* Ping result cache */
    uint32_t hdr_addr;
    uint32_t max_size;
    uint32_t s0_hdr_addr; 
    uint32_t s0_max_size; 
    uint32_t s1_hdr_addr;   
    uint32_t s1_max_size;   
    uint8_t  num_slots;      
    uint8_t  active_slot;    

    uint8_t  hw_id;
    uint8_t  write_max;
    uint8_t  target_slot;    /* slot last targeted via xbld_select_slot() (for state log) */

    /* Snapshot of the image header pulled from the last hex file written
     * via xbld_write_hex() - best-effort, used for xbldinfo state log. */
    XbldImageHeader_t last_hdr;
    int               last_hdr_valid;

    /* Port advisory lock fd — held for the lifetime of an open session */
    int      lock_fd;

    /* RX thread */
    pthread_t       rx_thread;
    volatile int    rx_running;

    /* Packet queues */
    PktQueue_t      xcli_q;    /* XCLI response frames */
    PktQueue_t      ack_q;     /* WRITE_ACK frames */

    /* Request seq counter (protected by req_mtx) */
    pthread_mutex_t req_mtx;
    uint8_t         seq;

    /* Fragment buffer for XCLI reassembly */
    XcliFragBuf_t   frag_buf;
} XbldClient_t;

/* =========================================================================
 * Lifecycle
 * =========================================================================*/

/** Initialise client struct with connection params. Does not open the port. */
void xbld_client_init(XbldClient_t *c,
                      const char   *dev,
                      uint32_t      baud,
                      int           use_arq,
                      int           use_ecc,
                      int           crc_type,
                      int           verbose);

/** Open serial port, start RX thread. Returns 0 ok, -1 error. */
int  xbld_client_open(XbldClient_t *c);

/** Stop RX thread, close serial port. */
void xbld_client_close(XbldClient_t *c);

/* =========================================================================
 * xBLD commands  (all return 0=ok, -1=error)
 * =========================================================================*/
void xbld_select_slot(XbldClient_t *c, uint8_t slot);
/** Ping bootloader, populates c->hdr_addr / max_size / hw_id / write_max. */
int xbld_ping(XbldClient_t *c);
/** Probe the EXP application's UART0 text prompt (no application xCLI). */
int xbld_ping_app(XbldClient_t *c);
int xbld_wait_for_app(XbldClient_t *c, uint32_t timeout_ms);
/** Erase application flash region. */
int  xbld_erase(XbldClient_t *c, uint8_t slot);

/** Verify image CRC (MCU side). Returns 0 ok, -1 fail. */
int  xbld_verify(XbldClient_t *c, uint8_t slot);

/** Jump to application. */
int  xbld_jump(XbldClient_t *c, uint8_t slot);

/** Read current BL state. */
int xbld_status(XbldClient_t *c, uint8_t *out_state, uint32_t *out_written);

/** Write a signed Intel HEX file (erase must be done first). */
int xbld_write_hex(XbldClient_t *c, const char *path);

/** Write pre-parsed chunk list. */
int xbld_write_chunks(XbldClient_t *c, const ChunkList_t *cl);

/**
 * Returns 0 on success, -1 on any step failure.
 */
int  xbld_flash(XbldClient_t *c, const char *hex_path, uint8_t slot);

/** Reboot to bootloader, then ping until it responds. */
int xbld_reboot_and_ping(XbldClient_t *c);

/** Keep an active bootloader session, or request entry from the application. */
int xbld_ensure_bootloader(XbldClient_t *c);

/** Read 240 bytes from address (for debug). Returns 0 ok, -1 error. */
int xbld_read(XbldClient_t *c, uint32_t addr);

/** Dump flash region to file (.bin or .hex). */
int xbld_dump(XbldClient_t *c, uint32_t addr, uint32_t size, const char *path);

int xbld_bootinfo(XbldClient_t *c);
#ifdef __cplusplus
}
#endif
#endif /* XBLD_CLIENT_H */
