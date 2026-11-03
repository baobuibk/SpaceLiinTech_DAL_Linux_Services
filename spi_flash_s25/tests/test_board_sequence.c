/* Exercise the actual CLI startup with fake GPIOs and ioctl SPI response. */
#define _POSIX_C_SOURCE 200809L
#include <sys/ioctl.h>
#include <time.h>
#include <stdarg.h>
#include <assert.h>
static char mux_path[256], power_path[256], lock_path[256];
static int mock_ioctl(int fd, unsigned long request, ...);
static int mock_sleep(const struct timespec *req, struct timespec *rem);
#define MUX_PATH mux_path
#define POWER_PATH power_path
#define LOCK_PATH lock_path
#define ioctl mock_ioctl
#define nanosleep mock_sleep
#define main tool_main
#include "../files/spi_flash_s25.c"
#undef main
#undef nanosleep
#undef ioctl

static bool power_waited, change_mux, bad_id;
static unsigned spi_calls;

static int mock_sleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    if (req->tv_nsec == 10000000) {
        assert(gpio_read(POWER_PATH) == 1);
        assert(gpio_read(MUX_PATH) == 0);
        power_waited = true;
        if (change_mux) assert(gpio_set(MUX_PATH, 1) == 0);
    } else assert(false); /* no switch settling delay in flash commands */
    return 0;
}

static int mock_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    assert(power_waited);
    assert(gpio_read(POWER_PATH) == 1 && gpio_read(MUX_PATH) == 0);
    if (request != SPI_IOC_MESSAGE(1)) return 0;
    va_list ap;
    va_start(ap, request);
    struct spi_ioc_transfer *tr = va_arg(ap, struct spi_ioc_transfer *);
    va_end(ap);
    spi_calls++;
    const uint8_t *tx = (const void *)(uintptr_t)tr->tx_buf;
    uint8_t *rx = (void *)(uintptr_t)tr->rx_buf;
    memset(rx, 0, tr->len);
    if (bad_id) assert(tx[0] == 0x9f); /* no array access on wrong ID */
    if (tx[0] == 0x9f) {
        uint8_t id[] = {1, 0x60, 0x18, 0xff, 0xff, 0xff};
        if (bad_id) id[0] = 0;
        memcpy(rx + 1, id, sizeof(id));
    } else if (tx[0] == 0x13) memset(rx + 5, 0xff, tr->len - 5);
    else assert(tx[0] == 0x05 || tx[0] == 0x07 || tx[0] == 0x35);
    return (int)tr->len;
}

static void put_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    assert(fp); assert(fputs(content, fp) >= 0); assert(fclose(fp) == 0);
}

int main(void)
{
    char dir[] = "/tmp/s25-sequence-XXXXXX", device[256], input[256];
    assert(mkdtemp(dir));
    snprintf(mux_path, sizeof(mux_path), "%s/mux", dir);
    snprintf(power_path, sizeof(power_path), "%s/power", dir);
    snprintf(lock_path, sizeof(lock_path), "%s/lock", dir);
    snprintf(device, sizeof(device), "%s/device", dir);
    snprintf(input, sizeof(input), "%s/input", dir);
    put_file(device, ""); put_file(input, "\xff");
    const char *commands[] = {"id", "read", "verify", "write", "erase"};
    for (int on = 0; on <= 1; on++) {
        for (size_t i = 0; i < 5; i++) {
            put_file(power_path, on ? "1\n" : "0\n"); put_file(mux_path, "0\n");
            power_waited = false; spi_calls = 0;
            char *args[] = {"tool", "-d", device, (char *)commands[i], NULL, NULL, NULL, NULL};
            int argc = 4;
            if (i == 1) { args[argc++] = "-s"; args[argc++] = "1"; }
            if (i == 2 || i == 3) { args[argc++] = "-f"; args[argc++] = input; }
            if (i == 3 || i == 4) args[argc++] = "-y";
            /* Unaligned erase reaches identification then refuses mutation. */
            if (i == 4) { args[argc++] = "-s"; args[argc++] = "1"; }
            optind = 0;
            assert(tool_main(argc, args) == (i == 4 ? 1 : 0));
            assert(power_waited && spi_calls > 0);
            assert(gpio_read(POWER_PATH) == 1 && gpio_read(MUX_PATH) == 0);
            /* FPGA owns the switch: refuse every command without touching power/SPI. */
            put_file(mux_path, "1\n"); put_file(power_path, "0\n");
            spi_calls = 0; power_waited = false;
            optind = 0; assert(tool_main(argc, args) == 1);
            assert(spi_calls == 0 && !power_waited);
            assert(gpio_read(MUX_PATH) == 1 && gpio_read(POWER_PATH) == 0);
            /* Unsupported chip must never receive read/program/erase commands. */
            put_file(mux_path, "0\n"); bad_id = true;
            optind = 0; assert(tool_main(argc, args) == 1);
            assert(spi_calls == 1); bad_id = false;

        }
    }
    put_file(mux_path, "0\n"); unlink(power_path);
    spi_calls = 0; power_waited = false;
    char *args[] = {"tool", "-d", device, "id", NULL};
    optind = 0; assert(tool_main(4, args) == 1);
    assert(spi_calls == 0 && gpio_read(MUX_PATH) == 0);
    put_file(power_path, "invalid\n");
    optind = 0; assert(tool_main(4, args) == 1);
    assert(spi_calls == 0 && gpio_read(MUX_PATH) == 0);
    put_file(power_path, "0\n"); unlink(mux_path);
    optind = 0; assert(tool_main(4, args) == 1);
    assert(spi_calls == 0 && gpio_read(POWER_PATH) == 0);
    /* Switch changed by another user while power settles: recheck blocks SPI. */
    put_file(mux_path, "0\n"); change_mux = true;
    optind = 0; assert(tool_main(4, args) == 1);
    assert(spi_calls == 0 && gpio_read(MUX_PATH) == 1);
    unlink(mux_path);
    unlink(power_path); unlink(lock_path); unlink(device); unlink(input); rmdir(dir);
    puts("PASS: MPU required, power settling, mux recheck, valid ID before data; FPGA/invalid ID block all flash commands");
    return 0;
}
