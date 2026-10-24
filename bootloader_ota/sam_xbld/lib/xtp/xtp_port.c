/*
 * xtp_port.c  —  xTP port layer (Linux/POSIX)
 *
 * read_byte convention: 0 = got byte  (matches MCU / xtp.c line 787)
 *                       -1 = no data or error
 *
 * Two GetDefaultPort variants:
 *   xTP_GetDefaultPort()     — no-arg, uses global s_ctx (MCU compat)
 *   xTP_GetDefaultPortCtx()  — explicit ctx (host client code)
 */

#include "xtp_port.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <sys/select.h>
#include <sys/ioctl.h>

struct termios2 {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[19];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

#ifndef BOTHER
#define BOTHER 0010000
#endif
#ifndef TCGETS2
#define TCGETS2 _IOR('T', 0x2A, struct termios2)
#endif
#ifndef TCSETS2
#define TCSETS2 _IOW('T', 0x2B, struct termios2)
#endif

static xTP_PortCtx_t *s_ctx = NULL;

/* ── Baud helper ─────────────────────────────────────────────────────── */

/* ── Serial open ─────────────────────────────────────────────────────── */
static int serial_open(const char *dev, uint32_t baud)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("xtp_port: open"); return -1; }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios2 tio;
    if (ioctl(fd, TCGETS2, &tio) < 0) {
        perror("xtp_port: ioctl TCGETS2");
        close(fd);
        return -1;
    }

    tio.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tio.c_oflag &= (tcflag_t)~OPOST;
    tio.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag &= (tcflag_t)~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tio.c_cflag |= (tcflag_t)(CS8 | CLOCAL | CREAD);

    tio.c_cflag &= (tcflag_t)~CBAUD;
    tio.c_cflag |= (tcflag_t)BOTHER;
    tio.c_ispeed = baud;
    tio.c_ospeed = baud;

    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0; 

    if (ioctl(fd, TCSETS2, &tio) < 0) {
        perror("xtp_port: ioctl TCSETS2");
        close(fd);
        return -1;
    }

    /* Giải phóng buffer */
    ioctl(fd, TCFLSH, TCIOFLUSH);
    return fd;
}

/* ── Port callbacks ──────────────────────────────────────────────────── */
static void port_send_byte(const uint8_t *b)
{
    if (!s_ctx || s_ctx->fd < 0) return;
    ssize_t _w = write(s_ctx->fd, b, 1); (void)_w;
}

/* Non-blocking poll: 0=got byte, -1=empty/error */
static int port_read_byte(uint8_t *b)
{
    if (!s_ctx || s_ctx->fd < 0) return -1;
    fd_set fds;
    struct timeval tv = {0, 10000};   // 10ms — đủ để ngủ khi idle
    FD_ZERO(&fds); FD_SET(s_ctx->fd, &fds);
    if (select(s_ctx->fd + 1, &fds, NULL, NULL, &tv) <= 0) return -1;
    return (read(s_ctx->fd, b, 1) == 1) ? 0 : -1;
}

static uint32_t port_get_tick(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static uint16_t port_get_tx_space(void) { return 0xFFFF; }

static int  port_tx_lock  (void) { return pthread_mutex_lock  (&s_ctx->tx_mtx); }
static void port_tx_unlock(void) {        pthread_mutex_unlock(&s_ctx->tx_mtx); }
static int  port_rx_lock  (void) { return pthread_mutex_lock  (&s_ctx->rx_mtx); }
static void port_rx_unlock(void) {        pthread_mutex_unlock(&s_ctx->rx_mtx); }

/* ── Public API ──────────────────────────────────────────────────────── */
int xTP_Port_Open(xTP_PortCtx_t *ctx, const char *dev, uint32_t baud)
{
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->dev, dev, sizeof(ctx->dev) - 1);
    ctx->baud = baud;
    ctx->fd   = serial_open(dev, baud);
    if (ctx->fd < 0) return -1;
    pthread_mutex_init(&ctx->tx_mtx, NULL);
    pthread_mutex_init(&ctx->rx_mtx, NULL);
    s_ctx = ctx;
    return 0;
}

void xTP_Port_Close(xTP_PortCtx_t *ctx)
{
    if (ctx->fd >= 0) { close(ctx->fd); ctx->fd = -1; }
    pthread_mutex_destroy(&ctx->tx_mtx);
    pthread_mutex_destroy(&ctx->rx_mtx);
    if (s_ctx == ctx) s_ctx = NULL;
}

static xTP_Port_t make_port(void)
{
    xTP_Port_t p;
    memset(&p, 0, sizeof(p));
    p.send_byte    = port_send_byte;
    p.read_byte    = port_read_byte;
    p.get_tick     = port_get_tick;
    p.get_tx_space = port_get_tx_space;
    p.tx_lock      = port_tx_lock;
    p.tx_unlock    = port_tx_unlock;
    p.rx_lock      = port_rx_lock;
    p.rx_unlock    = port_rx_unlock;
    return p;
}

/* No-arg version — MCU xtp.c calls xTP_GetDefaultPort() */
xTP_Port_t xTP_GetDefaultPort(void) { return make_port(); }

/* Explicit-ctx version — host xbld_client.c calls this */
xTP_Port_t xTP_GetDefaultPortCtx(xTP_PortCtx_t *ctx)
{
    s_ctx = ctx;
    return make_port();
}

/* Blocking read for rx_thread */
int xTP_Port_ReadBlocking(uint8_t *b, uint32_t timeout_ms)
{
    if (!s_ctx || s_ctx->fd < 0) return -1;
    fd_set fds;
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    FD_ZERO(&fds); FD_SET(s_ctx->fd, &fds);
    if (select(s_ctx->fd + 1, &fds, NULL, NULL, &tv) <= 0) return -1;
    return (read(s_ctx->fd, b, 1) == 1) ? 0 : -1;
}

/* ── Logging ─────────────────────────────────────────────────────────── */
void xTP_LogImpl(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[xtp] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
