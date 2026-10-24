/*
 * xbld_client.c  -  xBLD host protocol client
 *
 * Threading model
 *   rx_thread   : xTP_Port_ReadBlocking(50ms) - acquire RX_LOCK - xTP_Read()
 *   main thread : xTP_ARQ_Send() - xTP_Process() - acquire RX_LOCK - port.read_byte()
 *
 * [1.1.2] xbld_client_open()/close() now acquire/release the shared advisory lock
 */

#include "xbld_client.h"
#include "xbld_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>     /* open(), O_CREAT */
#include <sys/file.h>  /* flock() */
#include <sys/stat.h>  /* mkdir() */
#include <sys/select.h>
#include <termios.h>   /* tcdrain(), tcflush() */

/* -- Helpers ----------------------------------------------------- */
static inline int past_deadline(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}
static void ms_sleep(uint32_t ms)
{
    struct timespec ts = { ms/1000, (ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}

/* -- Packet queue -------------------------------------------------- */
static void pktq_init(PktQueue_t *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init (&q->cond, NULL);
}
static void pktq_destroy(PktQueue_t *q)
{
    pthread_mutex_lock(&q->mtx);
    while (q->count > 0) { free(q->items[q->head].data); q->head=(q->head+1)%PKT_QUEUE_DEPTH; q->count--; }
    pthread_mutex_unlock(&q->mtx);
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->cond);
}
static void pktq_flush(PktQueue_t *q)
{
    pthread_mutex_lock(&q->mtx);
    while (q->count > 0) { free(q->items[q->head].data); q->head=(q->head+1)%PKT_QUEUE_DEPTH; q->count--; }
    pthread_mutex_unlock(&q->mtx);
}
static void pktq_push(PktQueue_t *q, uint16_t id, uint8_t *data, uint16_t len)
{
    pthread_mutex_lock(&q->mtx);
    if (q->count >= PKT_QUEUE_DEPTH) {
        free(q->items[q->head].data);
        q->head = (q->head+1)%PKT_QUEUE_DEPTH;
        q->count--;
    }
    size_t t = q->tail;
    q->items[t].id = id; q->items[t].data = data; q->items[t].len = len;
    q->tail = (t+1)%PKT_QUEUE_DEPTH; q->count++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mtx);
}
/* Returns 1+fills *out on success; 0 on deadline. Caller owns out->data. */
static int pktq_pop_deadline(PktQueue_t *q, PktItem_t *out, uint32_t deadline_ms)
{
    pthread_mutex_lock(&q->mtx);
    while (q->count == 0) {
        uint32_t now = now_ms();
        if (past_deadline(now, deadline_ms)) { pthread_mutex_unlock(&q->mtx); return 0; }
        uint32_t rem = past_deadline(now, deadline_ms) ? 0U : (deadline_ms - now);
        struct timespec abs; clock_gettime(CLOCK_REALTIME, &abs);
        abs.tv_sec += rem/1000; abs.tv_nsec += (rem%1000)*1000000L;
        if (abs.tv_nsec >= 1000000000L) { abs.tv_sec++; abs.tv_nsec -= 1000000000L; }
        if (pthread_cond_timedwait(&q->cond, &q->mtx, &abs) == ETIMEDOUT)
            { pthread_mutex_unlock(&q->mtx); return 0; }
    }
    *out = q->items[q->head];
    q->head = (q->head+1)%PKT_QUEUE_DEPTH; q->count--;
    pthread_mutex_unlock(&q->mtx);
    return 1;
}

/* -- on_receive callback --------------------------------------------- */
static XbldClient_t *s_client = NULL;
static void on_receive(xTP_Instance_t *inst, uint8_t src,
                       xTP_ID_t id, const uint8_t *payload, uint16_t len)
{
    (void)inst; (void)src;

    if (!s_client) return;
    uint8_t *copy = malloc(len ? len : 1);
    if (!copy) return;
    if (len) memcpy(copy, payload, len);
    if      (id == XCLI_XTP_RESP_ID) {
        pktq_push(&s_client->xcli_q, id, copy, len);
    }
    else if (id == XBLD_XTP_WRITE_ACK_ID)  pktq_push(&s_client->ack_q,  id, copy, len);
    else                                    free(copy);
}

/* -- RX thread ------------------------------------------------------- */
static void *rx_thread_fn(void *arg)
{
    XbldClient_t *c = (XbldClient_t *)arg;
    while (c->rx_running) {
        if (c->arq_busy) {
            ms_sleep(1);
            continue;
        }
        /* xTP_Process no need to acquire RX_LOCK    anymore */
        xTP_Process(&c->xtp);
    }
    return NULL;
}

/* -- Port advisory lock (flock-based) ------- */
static int xbld_lock_acquire(XbldClient_t *c)
{
    mkdir("/home/root/tmp",     0755);
    mkdir("/home/root/tmp/run", 0755);
    if (mkdir(XCLI_LOCK_DIR, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[xbld_lock] cannot create dir '%s': %s\n",
                XCLI_LOCK_DIR, strerror(errno));
        return -1;
    }

    const char *base = strrchr(c->dev, '/');
    base = base ? base + 1 : c->dev;

    char path[128];
    snprintf(path, sizeof(path), "%s/%s.lock", XCLI_LOCK_DIR, base);

    int fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        fprintf(stderr, "[xbld_lock] cannot open lock file '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        c->lock_fd = fd;
        return 0;
    }

    if (errno != EWOULDBLOCK) {
        fprintf(stderr, "[xbld_lock] flock error on '%s': %s\n",
                path, strerror(errno));
        close(fd);
        return -1;
    }

    fprintf(stderr, "[xbld_lock] port '%s' busy, waiting up to %u s...\n",
            c->dev, XCLI_LOCK_WAIT_MS / 1000U);

    uint32_t deadline = now_ms() + XCLI_LOCK_WAIT_MS;
    while (!past_deadline(now_ms(), deadline)) {
        ms_sleep(50);
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            c->lock_fd = fd;
            return 0;
        }
        if (errno != EWOULDBLOCK) {
            fprintf(stderr, "[xbld_lock] flock error: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
    }

    fprintf(stderr, "[xbld_lock] timeout: port '%s' still held after %u s\n",
            c->dev, XCLI_LOCK_WAIT_MS / 1000U);
    close(fd);
    return -1;
}

/* -- Lifecycle ------------------------------------------------------- */
void xbld_client_init(XbldClient_t *c, const char *dev, uint32_t baud,
                      int use_arq, int use_ecc, int crc_type, int verbose)
{
    memset(c, 0, sizeof(*c));
    strncpy(c->dev, dev, sizeof(c->dev)-1);
    c->baud = baud; c->use_arq = use_arq; c->use_ecc = use_ecc;
    c->crc_type = (crc_type==32)?32:16; c->verbose = verbose;

    /* Dual-slot defaults */
    c->s0_hdr_addr = 0x00420000U;
    c->s0_max_size = 0x000FFE00U;
    c->s1_hdr_addr = 0x00520000U;
    c->s1_max_size = 0x000DFE00U;
    c->num_slots   = 1;
    c->active_slot = 0;

    /* hdr_addr/max_size always point at the currently selected slot (default: slot 0) */
    c->hdr_addr  = c->s0_hdr_addr;
    c->max_size  = c->s0_max_size;

    c->hw_id = 0; c->write_max = 251U;
    c->lock_fd = -1;   /* -1 = not held; 0 would be a valid fd (stdin) */
}

int xbld_client_open(XbldClient_t *c)
{
    if (xbld_lock_acquire(c) < 0) return -1;

    if (xTP_Port_Open(&c->port_ctx, c->dev, c->baud) < 0) {
        close(c->lock_fd); c->lock_fd = -1;
        return -1;
    }
    pktq_init(&c->xcli_q); pktq_init(&c->ack_q);
    pthread_mutex_init(&c->req_mtx, NULL);
    xTP_Init(&c->xtp, 0);
    c->xtp.port = xTP_GetDefaultPortCtx(&c->port_ctx);

    c->xtp.on_receive = on_receive;
    s_client = c;
    if (c->use_arq) xTP_ARQ_Init(&c->xtp, &c->arq);

    c->rx_running = 1;
    if (pthread_create(&c->rx_thread, NULL, rx_thread_fn, c) != 0) {
        xTP_Port_Close(&c->port_ctx);
        close(c->lock_fd); c->lock_fd = -1;
        return -1;
    }
    return 0;
}
void xbld_client_close(XbldClient_t *c)
{
    c->rx_running = 0;
    pthread_join(c->rx_thread, NULL);
    xTP_Port_Close(&c->port_ctx);
    pktq_destroy(&c->xcli_q); pktq_destroy(&c->ack_q);
    pthread_mutex_destroy(&c->req_mtx);
    s_client = NULL;
    if (c->lock_fd >= 0) { close(c->lock_fd); c->lock_fd = -1; }
}

/* -- XCLI send+wait ----------------------------------------------------- */
static int xcli_send_wait(XbldClient_t *c, uint8_t cmd_id, const char *cmd_str,
                           uint32_t timeout_ms, XcliResponse_t *out_rsp)
{
    pthread_mutex_lock(&c->req_mtx);
    uint8_t seq = c->seq++;
    pthread_mutex_unlock(&c->req_mtx);

    uint8_t req[300];
    int rlen = xcli_encode_request(req, sizeof(req), cmd_id, seq, cmd_str);
    if (rlen < 0) return -1;

    pktq_flush(&c->xcli_q);
    xcli_frag_reset(&c->frag_buf);

    xTP_Return_t rc;
    if (c->use_arq) {
    c->arq_busy = 1;
    rc = xTP_ARQ_Send(&c->xtp, XCLI_XTP_CMD_ID, 0xFF, req, (uint16_t)rlen);
    c->arq_busy = 0;
    {
        uint32_t drain_end = now_ms() + 300;
        while (!past_deadline(now_ms(), drain_end)) {
            xTP_Process(&c->xtp);   
            if (c->xcli_q.count > 0) break;
        }
    }
} else {
        rc = xTP_Send(&c->xtp, XCLI_XTP_CMD_ID, 0xFF, req, (uint16_t)rlen);
    }

    if (rc != XTP_OK) {
        if (c->verbose) fprintf(stderr, "[xcli] send failed rc=%d cmd=0x%02X\n", rc, cmd_id);
        return -1;
    }

    uint32_t deadline = now_ms() + timeout_ms;

    while (!past_deadline(now_ms(), deadline)) {
        PktItem_t pkt;
        if (!pktq_pop_deadline(&c->xcli_q, &pkt, deadline)) break;
        int r = xcli_frag_feed(&c->frag_buf, out_rsp, pkt.data, pkt.len, cmd_id, seq);
        free(pkt.data);
        if (r == 1)  return 0;
        if (r <  0) { xcli_frag_reset(&c->frag_buf); return -1; }
    }
    xcli_frag_reset(&c->frag_buf);
    if (c->verbose) fprintf(stderr, "[xcli] timeout cmd=0x%02X seq=%u\n", cmd_id, seq);
    return -1;
}

/* -- xBLD commands ----------------------------------------------------- */

/* Select active slot - updates hdr_addr/max_size used by write_hex */
void xbld_select_slot(XbldClient_t *c, uint8_t slot)
{
    c->target_slot = slot;
    if (slot == 1) {
        c->hdr_addr = c->s1_hdr_addr;
        c->max_size = c->s1_max_size;
    } else {
        c->hdr_addr = c->s0_hdr_addr;
        c->max_size = c->s0_max_size;
    }
}

/* -- Persistent state (xbldinfo) ----------------------------------------
 * Records the most recent write/flash attempt to XBLD_STATE_FILE so
 * `xbldinfo` (REPL command or one-shot CLI arg) can report it without
 * needing to talk to the MCU. Single file, overwritten every attempt. */
static void xbld_save_state(XbldClient_t *c, const char *hex_path,
                             uint8_t slot, const char *status)
{
    XbldStateEntry_t st;
    memset(&st, 0, sizeof(st));

    const char *base = strrchr(hex_path, '/');
    base = base ? base + 1 : hex_path;
    snprintf(st.filename, sizeof(st.filename), "%s", base);
    snprintf(st.path,     sizeof(st.path),     "%s", hex_path);
    snprintf(st.port,     sizeof(st.port),     "%s", c->dev);
    st.slot            = slot;
    st.timestamp_flash = (uint32_t)time(NULL);

    if (c->last_hdr_valid) {
        st.hdr_valid       = 1U;
        st.timestamp_build = c->last_hdr.timestamp;
        st.build_number    = c->last_hdr.build_number;
        st.ver_major       = c->last_hdr.version_major;
        st.ver_minor       = c->last_hdr.version_minor;
        st.ver_patch       = c->last_hdr.version_patch;
        st.image_size      = c->last_hdr.image_size;
        st.image_crc32     = c->last_hdr.image_crc32;
        st.hw_id           = c->last_hdr.hw_id;
    }

    snprintf(st.status, sizeof(st.status), "%s", status);
    xbld_state_save(&st);
}

static int xbld_ping_timeout(XbldClient_t *c, uint32_t timeout_ms)
{
    XcliResponse_t rsp;
    if (xcli_send_wait(c, XBLD_CMD_PING, "bping", timeout_ms, &rsp) < 0) return -1;
    if (rsp.status != XCLI_STATUS_OK) { xcli_response_free(&rsp); return -1; }
    const XcliField_t *f;

    if ((f=xcli_field_get(&rsp,"slots")))  c->num_slots   = xcli_field_u8(f);
    if ((f=xcli_field_get(&rsp,"s0_hdr"))) c->s0_hdr_addr = xcli_field_u32(f);
    if ((f=xcli_field_get(&rsp,"s0_max"))) c->s0_max_size = xcli_field_u32(f);
    if ((f=xcli_field_get(&rsp,"s1_hdr"))) c->s1_hdr_addr = xcli_field_u32(f);
    if ((f=xcli_field_get(&rsp,"s1_max"))) c->s1_max_size = xcli_field_u32(f);
    if ((f=xcli_field_get(&rsp,"slot")))   c->active_slot = xcli_field_u8(f);

    if ((f=xcli_field_get(&rsp,"hdr")))    c->s0_hdr_addr = xcli_field_u32(f);
    if ((f=xcli_field_get(&rsp,"max_sz"))) c->s0_max_size = xcli_field_u32(f);

    c->hdr_addr = c->s0_hdr_addr;
    c->max_size = c->s0_max_size;

    if ((f=xcli_field_get(&rsp,"hw_id")))  c->hw_id     = xcli_field_u8(f);
    if ((f=xcli_field_get(&rsp,"wr_max"))) c->write_max = xcli_field_u8(f);

    if (c->verbose) {
        char lib[32]="?";
        if ((f=xcli_field_get(&rsp,"lib"))) xcli_field_str(f,lib,sizeof(lib));
        const char *board_str = "";
        char board_buf[24] = "";
        if ((f = xcli_field_get(&rsp, "board_ident"))) {
            uint32_t bid = xcli_field_u32(f);
            if (bid == 0xFFFFFFFFU)
                snprintf(board_buf, sizeof(board_buf), " board=unknown");
            else
                snprintf(board_buf, sizeof(board_buf), " board=BOARD%u", bid + 1U);
            board_str = board_buf;
        }

        printf("  [OK] bping: %s hw=%u wr_max=%uB slots=%u "
               "main=0x%08X backup=0x%08X active=%s%s\n",
               lib, c->hw_id, c->write_max, c->num_slots,
               c->s0_hdr_addr, c->s1_hdr_addr,
               c->active_slot ? "backup" : "main",
               board_str);
    }
    xcli_response_free(&rsp); return 0;
}

int xbld_ping(XbldClient_t *c)
{
    return xbld_ping_timeout(c, XCLI_RESP_TIMEOUT_MS);
}

int xbld_ping_app(XbldClient_t *c)
{
    /* An empty text CLI line produces the UART0 prompt. Do not send an
     * application xCLI packet: current EXP firmware accepts text on UART0. */
    static const char prompt[] = "DEBUG@MCU:~ $ ";
    size_t matched = 0;
    int found = 0;
    c->arq_busy = 1;
    ms_sleep(20); /* allow an in-progress xTP_Process() read to finish */
    tcflush(c->port_ctx.fd, TCIFLUSH);
    if (write(c->port_ctx.fd, "\r", 1) != 1 || tcdrain(c->port_ctx.fd) < 0)
        goto done;

    uint32_t deadline = now_ms() + 800U;
    while (!past_deadline(now_ms(), deadline)) {
        fd_set read_fds;
        struct timeval timeout = {0, 100000};
        FD_ZERO(&read_fds);
        FD_SET(c->port_ctx.fd, &read_fds);
        int ready = select(c->port_ctx.fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) continue;
        uint8_t buf[128];
        ssize_t count = read(c->port_ctx.fd, buf, sizeof(buf));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        for (ssize_t i = 0; i < count; i++) {
            matched = (buf[i] == (uint8_t)prompt[matched]) ? matched + 1U
                      : (buf[i] == (uint8_t)prompt[0] ? 1U : 0U);
            if (matched == sizeof(prompt) - 1U) { found = 1; goto done; }
        }
    }

done:
    c->arq_busy = 0;
    if (found && c->verbose) printf("  [OK] App text CLI responding\n");
    return found ? 0 : -1;
}

int xbld_wait_for_app(XbldClient_t *c, uint32_t timeout_ms)
{
    if (c->use_arq) xTP_ARQ_SoftReset(&c->xtp);
    c->seq = 0;
    pktq_flush(&c->xcli_q);
    pktq_flush(&c->ack_q);
    ms_sleep(500);
    uint32_t deadline = now_ms() + timeout_ms;
    while (!past_deadline(now_ms(), deadline)) {
        if (xbld_ping_app(c) == 0) {
            if (c->verbose) printf("  [OK] App is running\n");
            return 0;
        }
        ms_sleep(200);
    }
    if (c->verbose) printf("  [WARN] App did not respond within %ums\n", timeout_ms);
    return -1;
}

int xbld_erase(XbldClient_t *c, uint8_t slot)
{
    char cmd_str[16];
    snprintf(cmd_str, sizeof(cmd_str), "berase %u", slot);
    if (c->verbose) { printf("  Erasing %s...", slot ? "backup slot" : "main slot"); fflush(stdout); }
    uint32_t t0 = now_ms();
    XcliResponse_t rsp;
    if (xcli_send_wait(c, XBLD_CMD_ERASE, cmd_str, XCLI_ERASE_TIMEOUT_MS, &rsp) < 0) {
        if (c->verbose) printf(" FAIL\n");
        return -1;
    }
    const XcliField_t *f = xcli_field_get(&rsp, "ok");
    int ok = f && xcli_field_bool(f);
    if (c->verbose) printf(" %.1fs - %s\n", (now_ms()-t0)/1000.0, ok?"OK":"FAIL");
    xcli_response_free(&rsp);
    if (!ok) return -1;
    if (c->use_arq) xTP_ARQ_SoftReset(&c->xtp);
    c->seq = 0;
    pktq_flush(&c->xcli_q); pktq_flush(&c->ack_q);
    ms_sleep(100); return 0;
}

int xbld_verify(XbldClient_t *c, uint8_t slot)
{
    char cmd_str[16];
    snprintf(cmd_str, sizeof(cmd_str), "bverify %u", slot);
    if (c->verbose) { printf("  Verifying %s...", slot ? "backup slot" : "main slot"); fflush(stdout); }
    uint32_t t0 = now_ms();
    XcliResponse_t rsp;
    if (xcli_send_wait(c, XBLD_CMD_VERIFY, cmd_str, XCLI_VERIFY_TIMEOUT_MS, &rsp) < 0) {
        if (c->verbose) printf(" FAIL\n");
        return -1;
    }
    const XcliField_t *f = xcli_field_get(&rsp,"ok");
    int ok = f && xcli_field_bool(f);
    char res[32]="?"; if ((f=xcli_field_get(&rsp,"result"))) xcli_field_str(f,res,sizeof(res));
    if (c->verbose) printf(" %.1fs - %s\n", (now_ms()-t0)/1000.0, res);
    xcli_response_free(&rsp); return ok?0:-1;
}

int xbld_jump(XbldClient_t *c, uint8_t slot)
{
    char cmd_str[16];
    snprintf(cmd_str, sizeof(cmd_str), "bjump %u", slot);
    if (c->verbose) { printf("  Jumping to %s...", slot ? "backup slot" : "main slot"); fflush(stdout); }
    
    XcliResponse_t rsp;
    if (xcli_send_wait(c, XBLD_CMD_JUMP, cmd_str, XCLI_JUMP_TIMEOUT_MS, &rsp) < 0) {
        if (c->verbose) printf(" (no response -- may have jumped)\n");
        return 0;
    }
    const XcliField_t *f = xcli_field_get(&rsp, "ok");
    int ok = f && xcli_field_bool(f);
    if (c->verbose) printf(" %s\n", ok ? "OK" : "FAIL");
    xcli_response_free(&rsp);
    return ok ? 0 : -1;
}

int xbld_status(XbldClient_t *c, uint8_t *out_state, uint32_t *out_written)
{
    XcliResponse_t rsp;
    if (xcli_send_wait(c, XBLD_CMD_STATUS, "bstatus", XCLI_RESP_TIMEOUT_MS, &rsp) < 0) return -1;
    const XcliField_t *f;
    if (out_state   && (f=xcli_field_get(&rsp,"state")))   *out_state   = xcli_field_u8(f);
    if (out_written && (f=xcli_field_get(&rsp,"written"))) *out_written = xcli_field_u32(f);
    if ((f=xcli_field_get(&rsp,"slot"))) c->active_slot = xcli_field_u8(f);
    xcli_response_free(&rsp); return 0;
}

/* -- Write one chunk ----------------------------------------------------- */
static int write_one_chunk(XbldClient_t *c, uint32_t addr,
                            const uint8_t *data, uint32_t dlen)
{
    uint8_t payload[4 + XBLD_CHUNK_SIZE + 16];
    if (dlen > sizeof(payload)-4) dlen = (uint32_t)(sizeof(payload)-4);
    payload[0]=(uint8_t)(addr);      payload[1]=(uint8_t)(addr>>8);
    payload[2]=(uint8_t)(addr>>16);  payload[3]=(uint8_t)(addr>>24);
    memcpy(payload+4, data, dlen);
    uint16_t plen = (uint16_t)(4+dlen);

    xTP_Return_t rc;
    if (c->use_arq) {
    c->arq_busy = 1;
    rc = xTP_ARQ_Send(&c->xtp, XBLD_XTP_WRITE_ID, 0xFF, payload, plen);
    c->arq_busy = 0;
    {
        uint32_t drain_end = now_ms() + 300;
        while (!past_deadline(now_ms(), drain_end)) {
            xTP_Process(&c->xtp);
            if (c->ack_q.count > 0) break;
        }
    }
} else {
        rc = xTP_Send(&c->xtp, XBLD_XTP_WRITE_ID, 0xFF, payload, plen);
    }
    if (rc != XTP_OK) return -1;

    uint32_t deadline = now_ms() + WRITE_ACK_TIMEOUT_MS;
    while (!past_deadline(now_ms(), deadline)) {
        PktItem_t pkt;
        if (!pktq_pop_deadline(&c->ack_q, &pkt, deadline)) break;
        if (pkt.len < XBLD_WRITE_ACK_LEN) { free(pkt.data); continue; }
        uint32_t ack_addr = (uint32_t)(pkt.data[0]|((uint32_t)pkt.data[1]<<8)
                           |((uint32_t)pkt.data[2]<<16)|((uint32_t)pkt.data[3]<<24));
        uint8_t  status   = pkt.data[4];
        free(pkt.data);
        if (ack_addr != addr) continue;
        return (status == XBLD_WRITE_ACK_OK) ? 0 : -1;
    }
    return -1;
}

int xbld_write_chunks(XbldClient_t *c, const ChunkList_t *cl)
{
    uint32_t total_bytes = chunk_list_total_bytes(cl);
    uint32_t written=0; uint32_t t0=now_ms();
    for (size_t i=0; i<cl->count; i++) {
        uint32_t addr=cl->chunks[i].addr, dlen=cl->chunks[i].len;
        const uint8_t *data=cl->chunks[i].data;
        uint32_t off=0;
        while (off<dlen) {
            uint32_t sz=dlen-off; if (sz>c->write_max) sz=c->write_max;
            int ok=0;
            for (int a=0;a<XBLD_WRITE_RETRIES;a++) {
                if (write_one_chunk(c,addr+off,data+off,sz)==0) { ok=1; break; }
                if (c->verbose) fprintf(stderr,"\n  [RETRY] 0x%08X attempt %d\n",addr+off,a+1);
            }
            if (!ok) { if(c->verbose) fprintf(stderr,"\n  [ERROR] 0x%08X failed\n",addr+off); return -1; }
            written+=sz; off+=sz;
        }
        if (c->verbose && (i%32==0 || i==cl->count-1)) {
            uint32_t el=now_ms()-t0;
            float kbps=el?(written/1024.0f)/(el/1000.0f):0;
            printf("\r  %5.1f%% | %u/%u B | %.1f KB/s | 0x%08X",
                   total_bytes?written*100.0f/total_bytes:100.0f,
                   written,total_bytes,kbps,cl->chunks[i].addr);
            fflush(stdout);
        }
    }
    if (c->verbose) {
        uint32_t el=now_ms()-t0;
        printf("\n  Done: %u bytes in %.1fs (%.1f KB/s)\n",
               written,el/1000.0f,el?(written/1024.0f)/(el/1000.0f):0);
    }
    return 0;
}

int xbld_write_hex(XbldClient_t *c, const char *path)
{
    if (c->verbose) printf("  Reading: %s\n", path);
    uint32_t flash_end = c->hdr_addr + 512U + c->max_size;
    HexMem_t mem; hex_mem_init(&mem);
    int loaded = hex_load(&mem, path, c->hdr_addr, flash_end);
    if (loaded <= 0) {
        fprintf(stderr, "  [ERROR] %s\n", loaded<0?"Cannot read HEX":"No data in flash range");
        hex_mem_free(&mem);
        xbld_save_state(c, path, c->target_slot, "write_fail");
        return -1;
    }

    /* Best-effort snapshot of the signed image header for xbldinfo. */
    c->last_hdr_valid = 0;
    {
        uint8_t hdr_buf[XBLD_IMAGE_HDR_SIZE];
        size_t got = hex_mem_read_range(&mem, c->hdr_addr, hdr_buf, sizeof(hdr_buf));
        if (got == sizeof(hdr_buf)) {
            XbldImageHeader_t hdr;
            memcpy(&hdr, hdr_buf, sizeof(hdr));
            if (hdr.magic == XBLD_IMAGE_MAGIC && hdr.hdr_version == XBLD_IMAGE_HDR_VERSION) {
                c->last_hdr       = hdr;
                c->last_hdr_valid = 1;
            }
        }
    }

    uint32_t chunk_size = (XBLD_CHUNK_SIZE < c->write_max) ? XBLD_CHUNK_SIZE : c->write_max;
    ChunkList_t cl; chunk_list_init(&cl);
    if (mem_to_chunks(&mem, &cl, chunk_size) < 0) {
        hex_mem_free(&mem); chunk_list_free(&cl);
        xbld_save_state(c, path, c->target_slot, "write_fail");
        return -1;
    }
    hex_mem_free(&mem);
    if (c->verbose)
        printf("  HEX: %d bytes -> %zu chunks (%uB) -> %u bytes\n",
               loaded, cl.count, chunk_size, chunk_list_total_bytes(&cl));
    int r = xbld_write_chunks(c, &cl);
    chunk_list_free(&cl);
    xbld_save_state(c, path, c->target_slot, r == 0 ? "ok" : "write_fail");
    return r;
}

int xbld_flash(XbldClient_t *c, const char *hex_path, uint8_t slot)
{
    printf("\n-- xBLD FLASH (slot %u) --------------------\n", slot);
    c->last_hdr_valid = 0;
    if (xbld_reboot_and_ping(c)   < 0) { printf("  Aborted: ping failed\n");
        xbld_save_state(c, hex_path, slot, "ping_fail"); return -1; }
    xbld_select_slot(c, slot);
    if (xbld_erase(c, slot)       < 0) { printf("  Aborted: erase failed\n");
        xbld_save_state(c, hex_path, slot, "erase_fail"); return -1; }
    /* xbld_write_hex() records its own "ok" / "write_fail" state. */
    if (xbld_write_hex(c,hex_path)< 0) { printf("  Aborted: write failed\n");  return -1; }
    if (xbld_verify(c, slot)      < 0) { printf("  Aborted: verify failed\n");
        xbld_save_state(c, hex_path, slot, "verify_fail"); return -1; }
    xbld_save_state(c, hex_path, slot, "ok");
    if (slot == 0) {
        xbld_jump(c, 0);
        printf("-- xBLD DONE (jumped to slot 0) ------------\n\n");
    } else {
        printf("-- xBLD DONE (backup written, no jump needed)\n\n");
    }
    return 0;
}

int xbld_reboot_and_ping(XbldClient_t *c)
{
    /* The current EXP application has a text CLI on UART0. Pause the xTP
     * reader while sending its reset command on the same serial fd. The
     * leading CR clears a partial CLI line, including stray binary bytes. */
    static const char reset_cmd[] = "\rreset\r";
    printf("  Sending UART0 text reset...\n");
    c->arq_busy = 1;
    ms_sleep(20); /* let any in-progress xTP_Process() read finish */
    tcflush(c->port_ctx.fd, TCIFLUSH);
    size_t off = 0;
    while (off < sizeof(reset_cmd) - 1U) {
        ssize_t n = write(c->port_ctx.fd, reset_cmd + off,
                          sizeof(reset_cmd) - 1U - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { c->arq_busy = 0; return -1; }
        off += (size_t)n;
    }
    if (tcdrain(c->port_ctx.fd) < 0) { c->arq_busy = 0; return -1; }
    tcflush(c->port_ctx.fd, TCIFLUSH); /* discard text echo/prompt */
    xTP_Reset(&c->xtp);
    if (c->use_arq) xTP_ARQ_SoftReset(&c->xtp);
    c->seq = 0;
    pktq_flush(&c->xcli_q); pktq_flush(&c->ack_q);
    c->arq_busy = 0;
    printf("  Pinging bootloader...\n");
    uint32_t deadline = now_ms() + 5000U;
    while (!past_deadline(now_ms(), deadline)) {
        if (xbld_ping_timeout(c, 500U) == 0) {
            printf("  [OK] Bootloader responding\n");
            return 0;
        }
        ms_sleep(100);
    }
    printf("  [FAIL] Bootloader not responding\n"); return -1;
}

int xbld_ensure_bootloader(XbldClient_t *c)
{
    if (xbld_ping(c) == 0) return 0;
    printf("  Bootloader not active; requesting entry from application...\n");
    return xbld_reboot_and_ping(c);
}

int xbld_read(XbldClient_t *c, uint32_t addr)
{
    char cmd_str[32];
    snprintf(cmd_str, sizeof(cmd_str), "bread 0x%X", addr);

    XcliResponse_t rsp;
    if (xcli_send_wait(c, XBLD_CMD_READ, cmd_str, XCLI_RESP_TIMEOUT_MS, &rsp) < 0) {
        printf("  Read failed\n"); return -1;
    }

    const XcliField_t *f = xcli_field_get(&rsp, "data");
    if (!f) { printf("  No data field\n"); xcli_response_free(&rsp); return -1; }

    uint16_t len;
    const uint8_t *data = xcli_field_bytes(f, &len);

    for (uint16_t i = 0; i < len; i += 16) {
        printf("  %08X  ", addr + i);
        for (uint16_t j = 0; j < 16; j++) {
            if (i+j < len) printf("%02X ", data[i+j]);
            else           printf("   ");
            if (j == 7)    printf(" ");
        }
        printf(" |");
        for (uint16_t j = 0; j < 16 && i+j < len; j++) {
            uint8_t ch = data[i+j];
            printf("%c", (ch >= 0x20 && ch < 0x7F) ? ch : '.');
        }
        printf("|\n");
    }

    xcli_response_free(&rsp);
    return 0;
}

int xbld_dump(XbldClient_t *c, uint32_t addr, uint32_t size, const char *path)
{
    printf("  Dumping 0x%08X (%u bytes) -> %s\n", addr, size, path);
    uint8_t *all = malloc(size); if (!all) return -1;
    uint32_t offset=0; char cmd[64];
    while (offset<size) {
        uint32_t cur=addr+offset;
        snprintf(cmd,sizeof(cmd),"bread 0x%X",cur);
        XcliResponse_t rsp;
        if (xcli_send_wait(c,XBLD_CMD_READ,cmd,XCLI_RESP_TIMEOUT_MS,&rsp)<0) {
            fprintf(stderr,"\n  [ERROR] read at 0x%08X\n",cur); free(all); return -1;
        }
        const XcliField_t *f=xcli_field_get(&rsp,"data");
        if (!f) { xcli_response_free(&rsp); free(all); return -1; }
        uint16_t got; const uint8_t *chunk=xcli_field_bytes(f,&got);
        uint32_t copy=(got>size-offset)?(size-offset):got;
        memcpy(all+offset,chunk,copy);
        xcli_response_free(&rsp);
        offset+=copy;
        printf("\r  %u/%u bytes",offset,size); fflush(stdout);
    }
    printf("\n");
    int ok=0; FILE *fp=NULL;
    size_t el=strlen(path); const char *ext=(el>=4)?path+el-4:"";
    if (strcmp(ext,".bin")==0) {
        fp=fopen(path,"wb"); if(fp){fwrite(all,1,size,fp);fclose(fp);printf("  Saved BIN\n");ok=1;}
    } else if (strcmp(ext,".hex")==0) {
        fp=fopen(path,"w");
        if (fp) {
            uint16_t last_upper = 0xFFFFU;   /* sentinel: upper chưa được emit */
            for (uint32_t i=0;i<size;i+=16) {
                uint32_t csz=(size-i<16)?(size-i):16;
                uint32_t cur_addr = addr + i;

                /* Emit Extended Linear Address record khi upper 16 bit thay đổi */
                uint16_t upper = (uint16_t)(cur_addr >> 16);
                if (upper != last_upper) {
                    last_upper = upper;
                    uint8_t uh = (uint8_t)(upper >> 8);
                    uint8_t ul = (uint8_t)(upper & 0xFF);
                    uint8_t ela_chk = (uint8_t)(-(0x02U + 0x00U + 0x00U + 0x04U + uh + ul));
                    fprintf(fp,":02000004%02X%02X%02X\n", uh, ul, ela_chk);
                }

                uint16_t ra=(uint16_t)(cur_addr & 0xFFFFU);
                uint8_t ln[20]; ln[0]=(uint8_t)csz; ln[1]=(uint8_t)(ra>>8); ln[2]=(uint8_t)ra; ln[3]=0;
                memcpy(ln+4,all+i,csz);
                uint8_t chk=0; for(uint32_t j=0;j<4+csz;j++) chk+=ln[j]; chk=(~chk+1)&0xFF;
                fprintf(fp,":"); for(uint32_t j=0;j<4+csz;j++) fprintf(fp,"%02X",ln[j]);
                fprintf(fp,"%02X\n",chk);
            }
            fprintf(fp,":00000001FF\n"); fclose(fp); printf("  Saved HEX\n"); ok=1;
        }
    } else { fprintf(stderr,"  [ERROR] file must be .bin or .hex\n"); }
    free(all); return ok?0:-1;
}

int xbld_bootinfo(XbldClient_t *c)
{
    XcliResponse_t rsp;
    memset(&rsp, 0, sizeof(rsp));
 
    if (xcli_send_wait(c, XBLD_CMD_BHISTORY, "bbhistory",
                       XCLI_RESP_TIMEOUT_MS, &rsp) < 0) {
        printf("  [FAIL] bbhistory: no response\n");
        return -1;
    }
 
    const XcliField_t *f;
 
    /* ok field */
    f = xcli_field_get(&rsp, "ok");
    if (!f || !xcli_field_bool(f)) {
        printf("  [FAIL] bbhistory: device returned ok=0 "
               "(boot info not recorded yet?)\n");
        xcli_response_free(&rsp);
        return -1;
    }
 
    /* count */
    uint16_t count = 0;
    if ((f = xcli_field_get(&rsp, "count")))
        count = xcli_field_u16(f);
 
    /* history bytes */
    uint16_t      hist_len = 0;
    const uint8_t *hist    = NULL;
    if ((f = xcli_field_get(&rsp, "data")))
        hist = xcli_field_bytes(f, &hist_len);
 
    static const char *cause_str[] = {
        "POR/BOR",
        "Backup/WakeUp",
        "Watchdog Reset",
        "Software Reset",
        "User Reset (NRST)",
    };
 
    printf("  [BOOT] Count   : %u (0x%04X)\n", count, count);
    printf("  [BOOT] History :\n");
 
    uint16_t slots = (hist_len < XBLD_BOOTINFO_HISTORY_LEN)
                     ? hist_len : XBLD_BOOTINFO_HISTORY_LEN;
 
    for (uint16_t i = 0; i < slots; i++) {
        uint8_t     cause = hist ? hist[i] : 0xFFU;
        const char *tag   = (i == 0)         ? "latest" :
                            (i == slots - 1U) ? "oldest" : "      ";
        const char *cstr  = (cause < 5U) ? cause_str[cause] : "(empty)";
        printf("    [%u] %-6s  0x%02X  %s\n", i, tag, cause, cstr);
    }
 
    xcli_response_free(&rsp);
    return 0;
}
