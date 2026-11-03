/* SPDX-License-Identifier: MIT
 * CAT93C46B Microwire EEPROM tool (128 bytes), via Linux spidev.
 * DAL FLEXCOM5 / spidev0.0 with sel_conf_spi_fpga; no automatic switching.
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

#define CAPACITY 128U
static const char *mux_path = "/sys/class/leds/sel_conf_spi_fpga/brightness";
#ifndef POWER_PATH
#define POWER_PATH "/sys/class/leds/pow_onoff_fpga/brightness"
#endif
#ifndef LOCK_PATH
/* Coordinate switch/power access with spi_flash_s25. */
#define LOCK_PATH "/run/lock/spi_flash_s25.lock"
#endif

typedef struct {
    int fd;
    uint32_t speed;
    unsigned org;
} Cat;

static int sleep_ms(unsigned ms);
static int require_mpu(void);

static bool valid_range(uint32_t address, size_t size)
{
    return size && address < CAPACITY && size <= CAPACITY - address;
}

static int transfer(Cat *cat, const uint8_t *tx, uint8_t *rx, size_t size)
{
    struct spi_ioc_transfer tr = {
        .tx_buf = (uintptr_t)tx, .rx_buf = (uintptr_t)rx,
        .len = (uint32_t)size, .speed_hz = cat->speed,
        .bits_per_word = 8, .delay_usecs = 1
    };
    int ret = ioctl(cat->fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret != (int)size) {
        if (ret < 0) perror("SPI_IOC_MESSAGE");
        else fprintf(stderr, "Short Microwire transfer: %d/%zu\n", ret, size);
        return -1;
    }
    /* At least 250 ns CS inactive between instructions (at low supply voltage). */
    struct timespec delay = {0, 1000};
    while (nanosleep(&delay, &delay) < 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

static unsigned address_bits(const Cat *cat)
{
    return cat->org == 8 ? 7U : 6U;
}

static void header(const Cat *cat, uint8_t *tx, unsigned opcode, unsigned address)
{
    /* Microwire ignores leading zeros until the start bit. Align the final
     * address bit at the end of byte 2; no trailing padding before write data.
     */
    unsigned command = (opcode << address_bits(cat)) | address;
    tx[0] = (uint8_t)(command >> 8);
    tx[1] = (uint8_t)command;
}

static int read_memory(Cat *cat, uint8_t data[CAPACITY])
{
    /* CS remains asserted for header + dummy zero + 1024 data bits.
     * Mode 0 samples before DO changes following each rising SK edge.
     */
    uint8_t tx[CAPACITY + 3] = {0}, rx[CAPACITY + 3];
    header(cat, tx, 6, 0); /* start=1, READ=10 */
    if (transfer(cat, tx, rx, sizeof(tx)) < 0) return -1;
    if (rx[2] & 0x80) {
        fprintf(stderr, "Invalid READ dummy bit; check CS polarity, ORG, power and wiring.\n");
        return -1;
    }
    for (unsigned i = 0; i < CAPACITY; i++)
        data[i] = (uint8_t)((rx[2 + i] << 1) | (rx[3 + i] >> 7));
    return 0;
}

static int write_control(Cat *cat, bool enable)
{
    uint8_t tx[2];
    unsigned address = enable ? 3U << (address_bits(cat) - 2) : 0;
    header(cat, tx, 4, address); /* EWEN: 1 00 11..., EWDS: 1 00 00... */
    return transfer(cat, tx, NULL, sizeof(tx));
}

static int write_unit(Cat *cat, unsigned byte_address, const uint8_t *data)
{
    unsigned bytes = cat->org / 8;
    uint8_t tx[4];
    header(cat, tx, 5, byte_address / bytes); /* start=1, WRITE=01 */
    memcpy(tx + 2, data, bytes);
    if (transfer(cat, tx, NULL, bytes + 2) < 0) return -1;
    /* Datasheet WRITE max 3 ms. CS is inactive while the internal write runs.
     * Conservative fixed wait avoids polling DO through an unsuitable CS path.
     */
    return sleep_ms(5);
}

static int compare_memory(const uint8_t *actual, const uint8_t *expected,
                          uint32_t address, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (actual[address + i] != expected[i]) {
            fprintf(stderr, "Verify failed at byte 0x%02x: expected %02x, got %02x\n",
                    address + (unsigned)i, expected[i], actual[address + i]);
            return -1;
        }
    }
    return 0;
}

static int update_memory(Cat *cat, uint32_t address, const uint8_t *data, size_t size)
{
    uint8_t before[CAPACITY], expected[CAPACITY], actual[CAPACITY];
    if (!valid_range(address, size) || read_memory(cat, before) < 0) return -1;
    memcpy(expected, before, CAPACITY);
    memcpy(expected + address, data, size);
    if (!memcmp(before, expected, CAPACITY)) return 0;
    /* Disable again even if EWEN transfer reports failure: it may have arrived. */
    int ret = write_control(cat, true);
    if (!ret) {
        unsigned bytes = cat->org / 8;
        for (unsigned pos = 0; pos < CAPACITY; pos += bytes) {
            if (!memcmp(before + pos, expected + pos, bytes)) continue;
            /* Never continue programming if another owner changes the switch. */
            if (require_mpu() < 0 || write_unit(cat, pos, expected + pos) < 0) {
                ret = -1;
                break;
            }
        }
    }
    if (require_mpu() == 0) {
        if (write_control(cat, false) < 0) {
            fprintf(stderr, "EWDS failed; EEPROM write-enable state is uncertain.\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "Switch ownership lost; cannot send EWDS.\n");
        ret = -1;
    }
    if (ret < 0) return -1;
    if (read_memory(cat, actual) < 0) return -1;
    return compare_memory(actual, expected, 0, CAPACITY);
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
    if (gpio_set(mux_path, fpga) < 0 || sleep_ms(1) < 0) return -1;
    fprintf(stderr, "SPI switch -> %s: selected GPIO=%d (%s); retained on exit.\n",
            fpga ? "FPGA" : "MPU", fpga, fpga ? "HIGH" : "LOW");
    return 0;
}

static int require_mpu(void)
{
    int state = gpio_read(mux_path);
    if (state < 0) return -1;
    if (state != 0) {
        fprintf(stderr, "SPI switch is routed to FPGA (GPIO=HIGH). "
                "Access refused; run spi_eeprom_cat93 --sw mpu explicitly first.\n");
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
    puts("Usage: spi_eeprom_cat93 --org 8|16 [options] read|write|verify|erase\n"
         "       spi_eeprom_cat93 --sw mpu|fpga\n"
         "Fixed SPI: /dev/spidev0.0 (FLEXCOM5), switch: sel_conf_spi_fpga\n"
         "      --org 8|16      Required: ORG LOW=8, HIGH/unconnected=16\n"
         "      --sw mpu|fpga   Switch only: LOW=MPU, HIGH=FPGA\n"
         "      --cs-inverted   ONLY if hardware inverts controller CS before EEPROM\n"
         "      --speed HZ      1..1000000, default 100000 (mode 0)\n"
         "  -a, --address N     Byte offset, default 0 (0..127)\n"
         "  -s, --size N        Byte count required for read/erase\n"
         "  -f, --file PATH     Input file for write/verify\n"
         "  -o, --output PATH   Binary output for read; otherwise hex\n"
         "  -y, --yes           Required for write/erase\n"
         "  -h, --help          Show help\n"
         "EEPROM has NO JEDEC ID. ORG and wiring cannot be auto-detected.\n"
         "Flash-style id command is intentionally unsupported.\n"
         "Data commands require MPU switch, ensure FPGA power ON, wait 10 ms, recheck.\n"
         "They never switch channels automatically. CS at EEPROM is active HIGH.\n"
         "x16 file format: high byte then low byte per word; offsets remain bytes.\n"
         "write preserves other bytes and verifies all 128 bytes; erase writes FF.\n"
         "Stop other users of the EEPROM before switching to MPU.");
}

int main(int argc, char **argv)
{
    const char *device = "/dev/spidev0.0", *input = NULL, *output = NULL;
    uint32_t address = 0, size = 0, speed = 100000;
    unsigned org = 0;
    int sw = -1;
    bool yes = false, inverted = false;
    static const struct option options[] = {
        {"address", 1, NULL, 'a'},
        {"size", 1, NULL, 's'}, {"file", 1, NULL, 'f'},
        {"output", 1, NULL, 'o'}, {"yes", 0, NULL, 'y'}, {"help", 0, NULL, 'h'},
        {"org", 1, NULL, 1000},
        {"sw", 1, NULL, 1002}, {"speed", 1, NULL, 1003},
        {"cs-inverted", 0, NULL, 1004}, {NULL, 0, NULL, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "a:s:f:o:yh", options, NULL)) != -1) {
        switch (c) {
        case 'a': address = number(optarg); break;
        case 's': size = number(optarg); break;
        case 'f': input = optarg; break;
        case 'o': output = optarg; break;
        case 'y': yes = true; break;
        case 'h': usage(); return 0;
        case 1000: org = number(optarg); break;
        case 1002:
            if (sw >= 0 || (strcmp(optarg, "mpu") && strcmp(optarg, "fpga"))) return 2;
            sw = !strcmp(optarg, "fpga"); break;
        case 1003: speed = number(optarg); break;
        case 1004: inverted = true; break;
        default: return 2;
        }
    }
    if ((sw < 0 && optind != argc - 1) || (sw >= 0 && optind != argc)) {
        usage(); return 2;
    }
    const char *cmd = sw >= 0 ? "switch" : argv[optind];
    bool rd = !strcmp(cmd, "read"), wr = !strcmp(cmd, "write");
    bool verify = !strcmp(cmd, "verify"), erase = !strcmp(cmd, "erase");
    if (!speed || speed > 1000000 ||
        (sw < 0 && ((org != 8 && org != 16) || (!rd && !wr && !verify && !erase))) ||
        ((wr || erase) && !yes) || ((wr || verify) && (!input || size)) ||
        (input && !wr && !verify) || (output && !rd) ||
        (sw >= 0 && (address || size)) || ((rd || erase) && !valid_range(address, size))) {
        fprintf(stderr, "Invalid arguments; confirm --org and see --help.\n");
        return 2;
    }
    uint8_t data[CAPACITY], actual[CAPACITY];
    if (wr || verify) {
        FILE *fp = fopen(input, "rb");
        struct stat st;
        if (!fp) { perror(input); return 1; }
        if (fstat(fileno(fp), &st) < 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
            st.st_size > CAPACITY || !valid_range(address, (size_t)st.st_size)) {
            fprintf(stderr, "Input must be a nonempty regular file fitting in 128 bytes.\n");
            fclose(fp); return 2;
        }
        size = (uint32_t)st.st_size;
        if (fread(data, 1, size, fp) != size) { fclose(fp); return 1; }
        fclose(fp);
    }
    int ret = 1;
    Cat cat = {.fd = -1, .speed = speed, .org = org};
    int lock = open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) { perror("Lock"); goto done; }
    if (sw >= 0) { ret = switch_to(sw) < 0; goto done; }
    if (prepare_access() < 0) goto done;
    cat.fd = open(device, O_RDWR | O_CLOEXEC);
    if (cat.fd < 0) { perror(device); goto done; }
    if (flock(cat.fd, LOCK_EX | LOCK_NB) < 0) { perror("SPI lock"); goto done; }
    uint8_t mode = (uint8_t)(SPI_MODE_0 | (inverted ? 0 : SPI_CS_HIGH)), bits = 8;
    if (ioctl(cat.fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(cat.fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(cat.fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI setup");
        fprintf(stderr, "Active-HIGH CS on Atmel SPI requires GPIO CS in Device Tree; "
                        "--cs-inverted is only for a real hardware inverter.\n");
        goto done;
    }
    if (require_mpu() < 0) goto done;
    fprintf(stderr, "CAT93C46B configured x%u, 128 bytes; no hardware ID is available.\n", org);
    if (wr || erase) {
        if (erase) memset(data, 0xff, size);
        ret = update_memory(&cat, address, data, size) < 0;
    } else {
        if (read_memory(&cat, actual) < 0) goto done;
        if (verify) ret = compare_memory(actual, data, address, size) < 0;
        else if (output) {
            int fd = open(output, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
            if (fd < 0) { perror(output); goto done; }
            ssize_t n = write(fd, actual + address, size);
            int closed = close(fd);
            if (n != (ssize_t)size || closed < 0) {
                fprintf(stderr, "Output write failed; file may be partial.\n"); goto done;
            }
            ret = 0;
        } else {
            for (unsigned i = 0; i < size; i++) {
                if (i % 16 == 0) printf("%02x:", address + i);
                printf(" %02x", actual[address + i]);
                if (i % 16 == 15 || i + 1 == size) putchar('\n');
            }
            if (fflush(stdout) || ferror(stdout)) goto done;
            ret = 0;
        }
    }
    if (!ret) fprintf(stderr, "%s: OK\n", cmd);
done:
    if (cat.fd >= 0) close(cat.fd);
    if (lock >= 0) close(lock);
    return ret;
}
