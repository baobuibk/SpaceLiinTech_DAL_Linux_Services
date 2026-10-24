#define _POSIX_C_SOURCE 200809L
#include "programmer.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }
struct hardware { int spi; int gpio[2]; uint32_t speed; };
static int transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t size)
{
    struct hardware *h = ctx;
    if (stopped) return -1;
    struct spi_ioc_transfer tr = {
        .tx_buf = (uintptr_t)tx, .rx_buf = (uintptr_t)rx,
        .len = (uint32_t)size, .speed_hz = h->speed, .bits_per_word = 8
    };
    int n = ioctl(h->spi, SPI_IOC_MESSAGE(1), &tr);
    if (n != (int)size) { perror("SPI transfer"); return -1; }
    return 0;
}
static int gpio(void *ctx, unsigned pin, int value)
{
    struct hardware *h = ctx;
    char c = value ? '1' : '0';
    if (stopped || pin > 1) return -1;
    if (lseek(h->gpio[pin], 0, SEEK_SET) < 0 || write(h->gpio[pin], &c, 1) != 1) {
        perror("GPIO write"); return -1;
    }
    return 0;
}
static int pause_ms(void *ctx, unsigned ms)
{
    (void)ctx;
    struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000};
    while (!stopped && nanosleep(&delay, &delay)) {
        if (errno != EINTR) return -1;
    }
    return stopped ? -1 : 0;
}
static uint8_t *load_image(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > FLASH_SIZE) {
        fprintf(stderr, "Image must be a regular file of 1..16777216 bytes\n");
        fclose(f); return NULL;
    }
    *size = (size_t)st.st_size;
    uint8_t *data = malloc(*size);
    if (!data || fread(data, 1, *size, f) != *size) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    /* Expect a raw, uncompressed/unencrypted x1 boot binary; reject containers. */
    size_t sync = 0;
    while (sync + 4 <= *size && sync < 4096) {
        if (data[sync] == 0xaa && data[sync+1] == 0x99 &&
            data[sync+2] == 0x55 && data[sync+3] == 0x66) break;
        if (data[sync] != 0xff) break;
        ++sync;
    }
    /* Xilinx bus-width detection sequence may precede the synchronization word. */
    if (sync + 8 <= *size && !memcmp(data + sync, "\x00\x00\x00\xbb\x11\x22\x00\x44", 8)) {
        sync += 8;
        while (sync < *size && sync < 4096 && data[sync] == 0xff) ++sync;
    }
    if (sync >= 4096 || sync + 4 > *size || memcmp(data+sync, "\xaa\x99\x55\x66", 4)) {
        fprintf(stderr, "Expected raw Xilinx .bin preamble; .bit/.mcs/FMOD containers are unsupported\n");
        free(data); return NULL;
    }
    return data;
}
static void usage(const char *name)
{
    printf("Usage: %s [--dry-run] [--device PATH] id|verify IMAGE.bin|program IMAGE.bin\n"
           "DAL OBC v1.1.0, S25FL128L. Operations interrupt FPGA execution.\n"
           "Programs address 0; erases through the final 4 KiB sector.\n", name);
}
int main(int argc, char **argv)
{
    const char *device = "/dev/spidev2.0";
    int dry = 0, i = 1;
    for (; i < argc && argv[i][0] == '-'; ++i) {
        if (!strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        if (!strcmp(argv[i], "--dry-run")) dry = 1;
        else if (!strcmp(argv[i], "--device") && i+1 < argc) device = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if (i >= argc) { usage(argv[0]); return 2; }
    enum operation op;
    if (!strcmp(argv[i], "id")) op = OP_ID;
    else if (!strcmp(argv[i], "verify")) op = OP_VERIFY;
    else if (!strcmp(argv[i], "program")) op = OP_PROGRAM;
    else { usage(argv[0]); return 2; }
    ++i;
    if (argc-i != (op == OP_ID ? 0 : 1)) { usage(argv[0]); return 2; }
    size_t size = 0;
    uint8_t *image = op == OP_ID ? NULL : load_image(argv[i], &size);
    if (op != OP_ID && !image) return 2;
    printf("Device %s, SPI mode 0, 1 MHz; image %zu bytes, erase extent %zu bytes\n",
           device, size, (size+4095)/4096*4096);
    if (dry) { puts("Dry run: no hardware opened or changed."); free(image); return 0; }
    int result = 1;
    struct hardware hw = {.spi = -1, .gpio = {-1, -1}, .speed = 1000000};
    int lock = open("/run/lock/dal-fpga-prog.lock", O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX|LOCK_NB)) { perror("programmer lock"); goto done; }
    const char *paths[] = {
        "/sys/class/leds/fpga_program_b/brightness",
        "/sys/class/leds/sel_flash_spi_fpga/brightness"
    };
    FILE *power = fopen("/sys/class/leds/pow_onoff_fpga/brightness", "r");
    int on = 0;
    if (power) { if (fscanf(power, "%d", &on) != 1) on = 0; fclose(power); }
    if (!on) { fprintf(stderr, "FPGA power must already be enabled; power is never toggled here\n"); goto done; }
    for (unsigned p = 0; p < 2; ++p) {
        hw.gpio[p] = open(paths[p], O_RDWR|O_CLOEXEC);
        if (hw.gpio[p] < 0) { perror(paths[p]); goto done; }
    }
    hw.spi = open(device, O_RDWR|O_CLOEXEC);
    if (hw.spi < 0) { perror(device); goto done; }
    uint8_t mode = SPI_MODE_0, bits = 8;
    if (ioctl(hw.spi, SPI_IOC_WR_MODE, &mode) ||
        ioctl(hw.spi, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
        ioctl(hw.spi, SPI_IOC_WR_MAX_SPEED_HZ, &hw.speed)) { perror("SPI setup"); goto done; }
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
    char reset_state;
    if (read(hw.gpio[0], &reset_state, 1) != 1 ||
        (reset_state != '0' && reset_state != '1' && reset_state != '2')) {
        fprintf(stderr, "Cannot read initial reset state\n"); goto done;
    }
    struct backend backend = {&hw, reset_state != '0', transfer, gpio, pause_ms};
    result = program_run(&backend, op, image, size) ? 1 : 0;
done:
    if (hw.spi >= 0) close(hw.spi);
    for (unsigned p = 0; p < 2; ++p) if (hw.gpio[p] >= 0) close(hw.gpio[p]);
    if (lock >= 0) close(lock);
    free(image);
    return result;
}
