/* SPDX-License-Identifier: MIT
 * S25FL128L/S25FL128S userspace SPI NOR tool for DAL FLEXCOM11 (/dev/spidev2.0).
 * Uses dedicated 4-byte opcodes; does not change BAR or protection registers.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CAPACITY (16U * 1024U * 1024U)
#define MAX_SECTOR (256U * 1024U)
#define CHUNK 2048U
#ifndef MUX_PATH
#define MUX_PATH "/sys/class/leds/sel_flash_spi_fpga/brightness"
#endif
#ifndef POWER_PATH
#define POWER_PATH "/sys/class/leds/pow_onoff_fpga/brightness"
#endif
#ifndef LOCK_PATH
#define LOCK_PATH "/run/lock/spi_flash_s25.lock"
#endif

typedef struct {
    int fd;
    uint32_t speed;
    uint8_t id[6], cr;
    bool hybrid, fl_l;
} Flash;

static int transfer(Flash *f, const uint8_t *tx, uint8_t *rx, size_t n)
{
    struct spi_ioc_transfer tr = {
        .tx_buf = (uintptr_t)tx, .rx_buf = (uintptr_t)rx,
        .len = (uint32_t)n, .speed_hz = f->speed, .bits_per_word = 8
    };
    int ret = ioctl(f->fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret != (int)n) {
        if (ret < 0) perror("SPI_IOC_MESSAGE");
        else fprintf(stderr, "Short SPI transfer: %d/%zu\n", ret, n);
        return -1;
    }
    return 0;
}

static int reg_read(Flash *f, uint8_t op)
{
    uint8_t tx[2] = {op, 0}, rx[2];
    return transfer(f, tx, rx, sizeof(tx)) < 0 ? -1 : rx[1];
}

static int command(Flash *f, uint8_t op)
{
    return transfer(f, &op, NULL, 1);
}

static uint64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static int wait_ready(Flash *f, unsigned timeout)
{
    uint64_t start = milliseconds();
    for (;;) {
        int sr = reg_read(f, 0x05);
        if (sr < 0) return -1;
        /* FL-L reports errors in SR2V; SR1V[6:5] are protection bits. */
        int errors = f->fl_l ? reg_read(f, 0x07) : sr;
        if (errors < 0) return -1;
        if (errors & 0x60) {
            fprintf(stderr, "Flash erase/program error: %s=0x%02x; check protection and power.\n",
                    f->fl_l ? "SR2V" : "SR1", errors);
            return -1;
        }
        if (!(sr & 1)) return 0;
        if (milliseconds() - start >= timeout) {
            fprintf(stderr, "Flash busy timeout; keep SPI routed to MPU until operation completes.\n");
            return -1;
        }
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
}

static int write_enable(Flash *f)
{
    if (wait_ready(f, 10000) < 0 || command(f, 0x06) < 0) return -1;
    int sr = reg_read(f, 0x05);
    if (sr < 0) return -1;
    if (!(sr & 2)) {
        fprintf(stderr, "Write Enable latch not set (SR1=0x%02x).\n", sr);
        return -1;
    }
    return 0;
}

static void address(uint8_t *tx, uint8_t op, uint32_t addr)
{
    tx[0] = op;
    tx[1] = (uint8_t)(addr >> 24);
    tx[2] = (uint8_t)(addr >> 16);
    tx[3] = (uint8_t)(addr >> 8);
    tx[4] = (uint8_t)addr;
}

static bool valid_range(uint32_t addr, size_t size)
{
    return size > 0 && addr < CAPACITY && size <= CAPACITY - addr;
}

static int identify(Flash *f)
{
    uint8_t tx[7] = {0x9f}, rx[7];
    if (transfer(f, tx, rx, sizeof(tx)) < 0) return -1;
    memcpy(f->id, rx + 1, 6);
    fprintf(stderr, "ID: %02x %02x %02x %02x %02x %02x\n",
            f->id[0], f->id[1], f->id[2], f->id[3], f->id[4], f->id[5]);
    f->fl_l = f->id[0] == 0x01 && f->id[1] == 0x60 && f->id[2] == 0x18;
    bool fl_s = f->id[0] == 0x01 && f->id[1] == 0x20 && f->id[2] == 0x18 &&
                f->id[5] == 0x80 && f->id[4] <= 1;
    if (!f->fl_l && !fl_s) {
        fprintf(stderr, "Unsupported ID: expected S25FL128L (01 60 18) or S25FL128S (01 20 18 xx 00/01 80).\n");
        return -1;
    }
    f->hybrid = fl_s && f->id[4] == 1;
    if (wait_ready(f, 10000) < 0) return -1;
    int cr = reg_read(f, 0x35), sr2 = reg_read(f, 0x07);
    if (cr < 0 || sr2 < 0) return -1;
    if (sr2 & 3) {
        fprintf(stderr, "Flash has suspended erase/program operation; refusing access.\n");
        return -1;
    }
    f->cr = (uint8_t)cr;
    if (f->fl_l)
        fprintf(stderr, "S25FL128L: 16 MiB, uniform 4 KiB sectors, 256-byte pages, CR1V=0x%02x\n", cr);
    else
        fprintf(stderr, "S25FL128S: 16 MiB, %s, CR1=0x%02x\n",
                f->hybrid ? ((cr & 4) ? "hybrid, top 4 KiB sectors" : "hybrid, bottom 4 KiB sectors")
                          : "uniform 256 KiB sectors", cr);
    return 0;
}

static uint32_t sector_size(const Flash *f, uint32_t addr)
{
    if (f->fl_l) return 4096U;
    if (!f->hybrid) return MAX_SECTOR;
    bool small = (f->cr & 4) ? addr >= CAPACITY - 128U * 1024U : addr < 128U * 1024U;
    return small ? 4096U : 65536U;
}

static int read_data(Flash *f, uint32_t addr, uint8_t *buf, size_t size)
{
    if (!valid_range(addr, size)) return -1;
    while (size) {
        uint8_t tx[CHUNK + 5] = {0}, rx[CHUNK + 5];
        size_t n = size < CHUNK ? size : CHUNK;
        address(tx, 0x13, addr);
        if (transfer(f, tx, rx, n + 5) < 0) return -1;
        memcpy(buf, rx + 5, n);
        buf += n; addr += (uint32_t)n; size -= n;
    }
    return 0;
}

static int compare_data(Flash *f, uint32_t addr, const uint8_t *expected, size_t size)
{
    uint8_t buf[CHUNK];
    while (size) {
        size_t n = size < sizeof(buf) ? size : sizeof(buf);
        if (read_data(f, addr, buf, n) < 0) return -1;
        for (size_t i = 0; i < n; i++) {
            if (buf[i] != expected[i]) {
                fprintf(stderr, "Verify failed at 0x%08x: expected %02x, got %02x\n",
                        addr + (uint32_t)i, expected[i], buf[i]);
                return -2; /* Readback mismatch: distinct from transport errors. */
            }
        }
        addr += (uint32_t)n; expected += n; size -= n;
    }
    return 0;
}

static int erase_sector(Flash *f, uint32_t addr)
{
    uint8_t tx[5];
    address(tx, sector_size(f, addr) == 4096 ? 0x21 : 0xdc, addr);
    if (write_enable(f) < 0 || transfer(f, tx, NULL, sizeof(tx)) < 0) return -1;
    return wait_ready(f, 10000);
}

static int program_data(Flash *f, uint32_t addr, const uint8_t *buf, size_t size)
{
    /* 256-byte boundaries work with both 256- and 512-byte page variants.
     * Called only after erasure, once per 256-byte chunk (ECC units not rewritten).
     */
    while (size) {
        uint8_t tx[261];
        size_t n = 256U - (addr % 256U);
        if (n > size) n = size;
        bool blank = true;
        for (size_t i = 0; i < n; i++) if (buf[i] != 0xff) blank = false;
        if (!blank) {
            address(tx, 0x12, addr);
            memcpy(tx + 5, buf, n);
            if (write_enable(f) < 0 || transfer(f, tx, NULL, n + 5) < 0 ||
                wait_ready(f, 1000) < 0) return -1;
        }
        addr += (uint32_t)n; buf += n; size -= n;
    }
    return 0;
}

static int update_data(Flash *f, uint32_t addr, const uint8_t *data, size_t size)
{
    uint8_t *sector = malloc(MAX_SECTOR);
    if (!sector) return -1;
    int ret = -1;
    while (size) {
        uint32_t length = sector_size(f, addr), base = addr & ~(length - 1U);
        size_t offset = addr - base, n = length - offset;
        if (n > size) n = size;
        if (read_data(f, base, sector, length) < 0) goto out;
        if (memcmp(sector + offset, data, n)) {
            memcpy(sector + offset, data, n);
            fprintf(stderr, "Update sector 0x%08x (%u bytes)\n", base, length);
            if (erase_sector(f, base) < 0 || program_data(f, base, sector, length) < 0) goto out;
            int checked = compare_data(f, base, sector, length);
            if (checked < 0) { ret = checked; goto out; }
        }
        addr += (uint32_t)n; data += n; size -= n;
    }
    ret = 0;
out:
    free(sector);
    return ret;
}

static bool erase_aligned(const Flash *f, uint32_t addr, size_t size)
{
    while (size) {
        uint32_t n = sector_size(f, addr);
        if (addr % n || size < n) return false;
        addr += n; size -= n;
    }
    return true;
}

static int erase_data(Flash *f, uint32_t addr, size_t size)
{
    uint8_t *blank = malloc(MAX_SECTOR);
    if (!blank) return -1;
    memset(blank, 0xff, MAX_SECTOR);
    int ret = -1;
    while (size) {
        uint32_t n = sector_size(f, addr);
        fprintf(stderr, "Erase sector 0x%08x (%u bytes)\n", addr, n);
        if (erase_sector(f, addr) < 0 || compare_data(f, addr, blank, n) < 0) goto out;
        addr += n; size -= n;
    }
    ret = 0;
out:
    free(blank);
    return ret;
}

static uint32_t number(const char *s)
{
    char *end;
    errno = 0;
    unsigned long long n = strtoull(s, &end, 0);
    if (errno || *s == '-' || !*s || *end || n > UINT32_MAX) {
        fprintf(stderr, "Invalid number: %s\n", s);
        exit(2);
    }
    return (uint32_t)n;
}

static int sleep_ms(unsigned ms)
{
    struct timespec delay = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) < 0) {
        if (errno != EINTR) { perror("nanosleep"); return -1; }
    }
    return 0;
}

static int gpio_read(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return -1; }
    int value = -1;
    if (fscanf(fp, "%d", &value) != 1 || (value != 0 && value != 1)) {
        fprintf(stderr, "Invalid GPIO value: %s\n", path);
        value = -1;
    }
    fclose(fp);
    return value;
}

static int gpio_set(const char *path, int value)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) { perror(path); return -1; }
    const char *text = value ? "1\n" : "0\n";
    ssize_t n = write(fd, text, 2);
    int closed = close(fd);
    if (n != 2 || closed < 0) {
        fprintf(stderr, "Cannot write GPIO: %s\n", path);
        return -1;
    }
    if (gpio_read(path) != value) {
        fprintf(stderr, "GPIO readback mismatch: %s\n", path);
        return -1;
    }
    return 0;
}

/* Standalone switch command: no SPI transaction or power change. */
static int switch_to(int fpga)
{
    if (gpio_set(MUX_PATH, fpga) < 0 || sleep_ms(1) < 0) return -1;
    fprintf(stderr, "SPI switch -> %s: sel_flash_spi_fpga=%d (%s); retained on exit.\n",
            fpga ? "FPGA" : "MPU", fpga, fpga ? "HIGH" : "LOW");
    return 0;
}

static int require_mpu(void)
{
    int state = gpio_read(MUX_PATH);
    if (state < 0) return -1;
    if (state != 0) {
        fprintf(stderr, "SPI switch is routed to FPGA (sel_flash_spi_fpga=HIGH). "
                "Access refused; run spi_flash_s25 --sw mpu explicitly first.\n");
        return -1;
    }
    return 0;
}

static int prepare_access(void)
{
    if (require_mpu() < 0) return -1;
    int power = gpio_read(POWER_PATH);
    if (power < 0) return -1;
    if (!power) {
        if (gpio_set(POWER_PATH, 1) < 0) return -1;
        fprintf(stderr, "FPGA power ON: pow_onoff_fpga=1.\n");
    }
    /* Always allow 10 ms settling, including a recent power-on by another user. */
    if (sleep_ms(10) < 0) return -1;
    return require_mpu();
}

static void usage(void)
{
    puts("Usage: spi_flash_s25 [options] id|read|write|verify|erase\n"
         "       spi_flash_s25 --sw mpu|fpga\n"
         "  -d, --device PATH   Default /dev/spidev2.0 (FLEXCOM11)\n"
         "  -a, --address N     Start address, decimal or 0xHEX (default 0)\n"
         "  -s, --size N        Required for read/erase; bytes\n"
         "  -f, --file PATH     Input file for write/verify (whole file)\n"
         "  -o, --output PATH   Binary output for read; otherwise hex display\n"
         "      --speed N       SPI clock in Hz, 1..10000000 (default 5000000)\n"
         "      --sw mpu|fpga   Switch only: MPU=LOW, FPGA=HIGH; no SPI transaction\n"
         "  -y, --yes           Required for write/erase\n"
         "  -h, --help          Show help\n"
         "Flash access requires switch already at MPU; FPGA routing refuses access.\n"
         "Ensure FPGA power ON, wait 10 ms, recheck switch, validate ID before data access.\n"
         "Flash commands never change the switch; power remains ON on exit.\n"
         "write preserves other bytes in affected sectors and verifies them.\n"
         "erase requires exact sector alignment. Neither operation is power-fail atomic.\n"
         "Stop other users of the flash/FPGA SPI before selecting MPU.");
}

int main(int argc, char **argv)
{
    const char *device = "/dev/spidev2.0", *input = NULL, *output = NULL;
    uint32_t addr = 0, size = 0, speed = 5000000;
    bool yes = false;
    int sw = -1;
    static const struct option opts[] = {
        {"device", 1, NULL, 'd'}, {"address", 1, NULL, 'a'},
        {"size", 1, NULL, 's'}, {"file", 1, NULL, 'f'},
        {"output", 1, NULL, 'o'}, {"speed", 1, NULL, 1000},
        {"sw", 1, NULL, 1001},
        {"yes", 0, NULL, 'y'}, {"help", 0, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "d:a:s:f:o:yh", opts, NULL)) != -1) {
        switch (c) {
        case 'd': device = optarg; break;
        case 'a': addr = number(optarg); break;
        case 's': size = number(optarg); break;
        case 'f': input = optarg; break;
        case 'o': output = optarg; break;
        case 'y': yes = true; break;
        case 'h': usage(); return 0;
        case 1000: speed = number(optarg); break;
        case 1001:
            if (sw != -1 || (strcmp(optarg, "mpu") && strcmp(optarg, "fpga"))) {
                fprintf(stderr, "Use one --sw mpu or --sw fpga.\n");
                return 2;
            }
            sw = !strcmp(optarg, "fpga");
            break;
        default: return 2;
        }
    }
    if ((sw < 0 && optind != argc - 1) || (sw >= 0 && optind != argc)) {
        usage(); return 2;
    }
    const char *cmd = sw >= 0 ? "switch" : argv[optind];
    bool rd = !strcmp(cmd, "read"), wr = !strcmp(cmd, "write");
    bool verify = !strcmp(cmd, "verify"), erase = !strcmp(cmd, "erase"), id = !strcmp(cmd, "id");
    if ((!rd && !wr && !verify && !erase && !id && sw < 0) || !speed || speed > 10000000 ||
        ((wr || erase) && !yes) ||
        ((wr || verify) && (!input || size)) || (input && !wr && !verify) ||
        (output && !rd) || ((id || sw >= 0) && (addr || size)) ||
        ((rd || erase) && !valid_range(addr, size))) {
        fprintf(stderr, "Invalid arguments (write/erase require --yes); see --help.\n");
        return 2;
    }
    uint8_t *data = NULL;
    if (wr || verify) {
        FILE *fp = fopen(input, "rb");
        struct stat st;
        if (!fp) { perror(input); return 1; }
        if (fstat(fileno(fp), &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
            st.st_size > CAPACITY || !valid_range(addr, (size_t)st.st_size)) {
            fprintf(stderr, "Input must be a nonempty regular file fitting in 16 MiB flash.\n");
            fclose(fp); return 2;
        }
        size = (uint32_t)st.st_size;
        data = malloc(size);
        if (!data || fread(data, 1, size, fp) != size) {
            fprintf(stderr, "Cannot load input file.\n"); fclose(fp); free(data); return 1;
        }
        fclose(fp);
    }
    int ret = 1;
    Flash f = {.fd = -1, .speed = speed};
    int lock = open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
        perror("Cannot lock SPI flash tool"); goto done;
    }
    if (sw >= 0) {
        ret = switch_to(sw) < 0;
        if (!ret) fprintf(stderr, "switch: OK\n");
        goto done;
    }
    if (prepare_access() < 0) goto done;
    f.fd = open(device, O_RDWR | O_CLOEXEC);
    if (f.fd < 0) { perror(device); goto done; }
    if (flock(f.fd, LOCK_EX | LOCK_NB)) { perror("SPI device lock"); goto done; }
    uint8_t mode = SPI_MODE_0, bits = 8;
    if (ioctl(f.fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(f.fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(f.fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI configuration"); goto done;
    }
    if (identify(&f) < 0) goto done;
    if (id) ret = 0;
    else if (wr) {
        int updated = update_data(&f, addr, data, size);
        /* Let the deployment coordinator distinguish built-in verify mismatch. */
        ret = updated == -2 ? 254 : updated < 0;
    }
    else if (verify) ret = compare_data(&f, addr, data, size) < 0;
    else if (erase) {
        if (!erase_aligned(&f, addr, size)) {
            fprintf(stderr, "Erase range must follow detected sector boundaries; nothing erased.\n");
            goto done;
        }
        ret = erase_data(&f, addr, size) < 0;
    } else {
        data = malloc(size);
        if (!data || read_data(&f, addr, data, size) < 0) goto done;
        if (output) {
            /* Exclusive creation protects existing backups against accidental truncation. */
            int fd = open(output, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
            if (fd < 0) { perror(output); goto done; }
            FILE *fp = fdopen(fd, "wb");
            if (!fp) { close(fd); goto done; }
            bool ok = fwrite(data, 1, size, fp) == size;
            if (fclose(fp)) ok = false;
            if (!ok) { fprintf(stderr, "Output write failed (file may be partial).\n"); goto done; }
        } else {
            for (uint32_t i = 0; i < size; i++) {
                if (i % 16 == 0) printf("%08x:", addr + i);
                printf(" %02x", data[i]);
                if (i % 16 == 15 || i + 1 == size) putchar('\n');
            }
            if (fflush(stdout) || ferror(stdout)) goto done;
        }
        ret = 0;
    }
    if (!ret) fprintf(stderr, "%s: OK\n", cmd);
done:
    if (f.fd >= 0) close(f.fd);
    if (lock >= 0) close(lock);
    free(data);
    return ret;
}
