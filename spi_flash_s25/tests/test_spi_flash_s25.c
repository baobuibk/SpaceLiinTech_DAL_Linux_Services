/* Host tests with a NOR emulator at the ioctl boundary. No hardware accesses. */
#define _POSIX_C_SOURCE 200809L
#include <sys/ioctl.h>
#include <stdarg.h>
#include <assert.h>
static int mock_ioctl(int fd, unsigned long request, ...);
#define ioctl mock_ioctl
#define main tool_main
#include "../files/spi_flash_s25.c"
#undef main
#undef ioctl

static uint8_t memory[CAPACITY], sr, cr, sr2, arch = 1, family = 0x80;
static unsigned erases, programs, busy_reads;
static bool fl_l;
static bool fail_io, short_io, no_wel, corrupt, stuck_busy;

static int mock_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    assert(request == SPI_IOC_MESSAGE(1));
    va_list ap;
    va_start(ap, request);
    struct spi_ioc_transfer *tr = va_arg(ap, struct spi_ioc_transfer *);
    va_end(ap);
    if (fail_io) { errno = EIO; return -1; }
    if (short_io) return 0;
    const uint8_t *tx = (const void *)(uintptr_t)tr->tx_buf;
    uint8_t *rx = (void *)(uintptr_t)tr->rx_buf;
    if (rx) memset(rx, 0, tr->len);
    if (tx[0] == 0x9f) {
        uint8_t id[] = {1, 0x20, 0x18, 0x4d, arch, family};
        if (fl_l) {
            id[1] = 0x60;
            id[3] = id[4] = id[5] = 0xff;
        }
        assert(tr->len == 7); memcpy(rx + 1, id, 6);
    } else if (tx[0] == 0x05) {
        rx[1] = sr | ((stuck_busy || busy_reads) ? 1 : 0);
        if (busy_reads) busy_reads--;
    } else if (tx[0] == 0x35) rx[1] = cr;
    else if (tx[0] == 0x07) rx[1] = sr2;
    else if (tx[0] == 0x06) { if (!no_wel) sr |= 2; }
    else {
        assert(tr->len >= 5);
        uint32_t a = ((uint32_t)tx[1] << 24) | ((uint32_t)tx[2] << 16) |
                     ((uint32_t)tx[3] << 8) | tx[4];
        size_t n = tr->len - 5;
        assert(a < CAPACITY);
        if (tx[0] == 0x13) {
            assert(a + n <= CAPACITY); memcpy(rx + 5, memory + a, n);
        } else if (tx[0] == 0x12) {
            assert(sr & 2); assert(n && n <= 256 && (a % 256) + n <= 256);
            for (size_t i = 0; i < n; i++) memory[a + i] &= tx[5 + i];
            if (corrupt) memory[a] ^= 1;
            sr &= (uint8_t)~2U; busy_reads = 1; programs++;
        } else {
            assert(tx[0] == 0x21 || tx[0] == 0xdc);
            bool small = fl_l || (arch == 1 && ((cr & 4) ? a >= CAPACITY - 131072U : a < 131072U));
            uint32_t len = fl_l ? 4096U : (arch == 0 ? 262144U : (small ? 4096U : 65536U));
            assert((tx[0] == 0x21) == small);
            assert(sr & 2); assert(a % len == 0);
            memset(memory + a, 0xff, len);
            sr &= (uint8_t)~2U; busy_reads = 1; erases++;
        }
    }
    return (int)tr->len;
}

static void update_case(Flash *f, uint32_t addr, size_t size)
{
    for (uint32_t i = 0; i < CAPACITY; i++) memory[i] = (uint8_t)(i * 13U + i / 251U);
    uint8_t *expected = malloc(CAPACITY), *data = malloc(size);
    assert(expected && data);
    memcpy(expected, memory, CAPACITY);
    memset(data, 0x5a, size);
    memcpy(expected + addr, data, size);
    assert(update_data(f, addr, data, size) == 0);
    assert(memcmp(expected, memory, CAPACITY) == 0);
    unsigned old = erases;
    assert(update_data(f, addr, data, size) == 0); /* no erase for identical content */
    assert(erases == old);
    free(expected); free(data);
}

int main(void)
{
    Flash f = {.fd = 10, .speed = 5000000};
    assert(valid_range(CAPACITY - 1, 1));
    assert(!valid_range(CAPACITY - 1, 2));
    assert(!valid_range(UINT32_MAX, 1));
    assert(!valid_range(0, 0));
    assert(!valid_range(0, SIZE_MAX));
    assert(identify(&f) == 0);
    assert(sector_size(&f, 0) == 4096 && sector_size(&f, 131072) == 65536);
    assert(!erase_aligned(&f, 1, 4096));
    assert(!erase_aligned(&f, 131072, 4096));
    assert(erase_aligned(&f, 126976, 4096 + 65536));
    update_case(&f, 131000, 600); /* crosses hybrid boundary and pages */
    cr = 4; assert(identify(&f) == 0);
    update_case(&f, CAPACITY - 131072 - 71, 700);
    update_case(&f, CAPACITY - 17, 17);
    arch = 0; cr = 0; assert(identify(&f) == 0);
    update_case(&f, 262144 - 73, 600);
    assert(erase_aligned(&f, 0, CAPACITY));
    assert(!erase_aligned(&f, 0, 65536));
    assert(erase_data(&f, 0, 262144) == 0);
    for (size_t i = 0; i < 262144; i++) assert(memory[i] == 0xff);
    family = 0; assert(identify(&f) < 0); family = 0x80;
    arch = 2; assert(identify(&f) < 0); arch = 0;
    sr2 = 1; assert(identify(&f) < 0); sr2 = 0;
    no_wel = true; assert(write_enable(&f) < 0); no_wel = false;
    sr = 0x40; assert(wait_ready(&f, 10) < 0);
    sr = 0x20; assert(wait_ready(&f, 10) < 0); sr = 0;
    stuck_busy = true; assert(wait_ready(&f, 2) < 0); stuck_busy = false;
    uint8_t b = 0;
    corrupt = true; assert(update_data(&f, 0, &b, 1) == -2); corrupt = false;
    fail_io = true; assert(read_data(&f, 0, &b, 1) < 0); fail_io = false;
    short_io = true; assert(read_data(&f, 0, &b, 1) < 0); short_io = false;
    fl_l = true;
    sr = 0x60; /* FL-L SR1 protection bits are NOT program/erase errors. */
    assert(identify(&f) == 0 && f.fl_l);
    assert(sector_size(&f, 0) == 4096 && sector_size(&f, CAPACITY - 1) == 4096);
    assert(wait_ready(&f, 10) == 0);
    sr = 0;
    update_case(&f, 4096 - 71, 700);
    update_case(&f, 65536 - 17, 600);
    update_case(&f, CAPACITY - 17, 17);
    assert(erase_data(&f, 4096, 8192) == 0);
    for (size_t i = 4096; i < 12288; i++) assert(memory[i] == 0xff);
    assert(!erase_aligned(&f, 0, 4095));
    for (unsigned err = 0x20; err <= 0x40; err <<= 1) {
        sr2 = (uint8_t)err;
        sr = 1; /* Errors must fail immediately even when WIP stays set. */
        assert(wait_ready(&f, 10) < 0);
    }
    sr = 0; sr2 = 2; assert(identify(&f) < 0); sr2 = 0;
    assert(identify(&f) == 0);
    printf("PASS: NOR geometry, sector preservation, page boundaries, verify and failures (%u erases, %u programs)\n",
           erases, programs);
    return 0;
}
