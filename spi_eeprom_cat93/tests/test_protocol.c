/* Host protocol emulator: no hardware access. */
#include <stdarg.h>
static char power_file[] = "/tmp/cat93-power-XXXXXX";
#define POWER_PATH power_file
#define ioctl mock_ioctl
#define main application_main
#include "../files/spi_eeprom_cat93.c"
#undef main
#undef ioctl
#include <assert.h>
static unsigned organization, writes;
static bool enabled, fail_write, corrupt_dummy;
static uint8_t memory[128];
int mock_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    assert(request == SPI_IOC_MESSAGE(1));
    va_list ap;
    va_start(ap, request);
    struct spi_ioc_transfer *tr = va_arg(ap, struct spi_ioc_transfer *);
    va_end(ap);
    const uint8_t *tx = (const void *)(uintptr_t)tr->tx_buf;
    uint8_t *rx = (void *)(uintptr_t)tr->rx_buf;
    unsigned bits = organization == 8 ? 7 : 6;
    unsigned cmd = ((unsigned)tx[0] << 8) | tx[1];
    unsigned op = cmd >> bits, addr = cmd & ((1U << bits) - 1);
    if (op == 6) {
        assert(addr == 0 && tr->len == 131 && rx);
        memset(rx, 0, tr->len);
        for (unsigned i = 0; i < 1024; ++i) {
            unsigned pos = 17 + i;
            rx[pos / 8] |= (uint8_t)(((memory[i / 8] >> (7 - i % 8)) & 1) << (7 - pos % 8));
        }
        if (corrupt_dummy) rx[2] |= 0x80;
    } else if (op == 4) {
        assert(tr->len == 2);
        enabled = addr == (3U << (bits - 2));
        assert(enabled || addr == 0);
    } else {
        assert(op == 5 && enabled);
        unsigned bytes = organization / 8;
        assert(tr->len == bytes + 2 && addr * bytes + bytes <= 128);
        if (fail_write) { errno = EIO; return -1; }
        memcpy(memory + addr * bytes, tx + 2, bytes);
        writes++;
    }
    return (int)tr->len;
}
int main(void)
{
    char path[] = "/tmp/cat93-mux-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && write(fd, "0\n", 2) == 2);
    close(fd);
    mux_path = path;
    for (organization = 8; organization <= 16; organization += 8) {
        Cat cat = {.fd = -1, .speed = 100000, .org = organization};
        uint8_t actual[128], expected[128];
        for (unsigned i = 0; i < 128; ++i) memory[i] = (uint8_t)(i * 37);
        assert(read_memory(&cat, actual) == 0 && !memcmp(actual, memory, 128));
        memcpy(expected, memory, 128);
        const uint8_t patch[] = {0x55, 0xaa, 0x71};
        memcpy(expected + 3, patch, sizeof(patch));
        writes = 0;
        assert(update_memory(&cat, 3, patch, sizeof(patch)) == 0);
        assert(!memcmp(expected, memory, 128) && !enabled);
        assert(writes == (organization == 8 ? 3 : 2));
        writes = 0;
        assert(update_memory(&cat, 3, patch, sizeof(patch)) == 0 && writes == 0);
        uint8_t blank = 0xff;
        assert(update_memory(&cat, 127, &blank, 1) == 0 && memory[127] == 255);
        assert(memory[126] == expected[126]);
        fail_write = true;
        assert(update_memory(&cat, 0, &blank, 1) < 0 && !enabled);
        fail_write = false;
        corrupt_dummy = true;
        assert(read_memory(&cat, actual) < 0);
        corrupt_dummy = false;
        assert(update_memory(&cat, 127, patch, 3) < 0);
    }
    assert(gpio_set(path, 1) == 0 && require_mpu() < 0);
    fd = mkstemp(power_file);
    assert(fd >= 0 && write(fd, "0\n", 2) == 2);
    close(fd);
    assert(prepare_access() < 0 && gpio_read(power_file) == 0);
    assert(switch_to(0) == 0 && gpio_read(power_file) == 0);
    assert(prepare_access() == 0 && gpio_read(power_file) == 1);
    assert(gpio_read(path) == 0);
    assert(switch_to(1) == 0 && gpio_read(power_file) == 1);
    unlink(power_file);
    unlink(path);
    puts("CAT93 x8/x16 protocol, preservation, bounds, cleanup: PASS");
    return 0;
}
