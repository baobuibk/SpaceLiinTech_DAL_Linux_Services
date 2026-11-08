#include "services/exp_uart_service.h"

#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define EXP_PROMPT "DEBUG@MCU:~ $ "
#define EXP_REPLY_CAP 4096U
static pthread_mutex_t exp_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t snapshot_thread;
static atomic_bool snapshot_running;
static char snapshot_sections[3][16384];
static time_t snapshot_time[3];
static const char *const status_commands[] = {
    "thm_wire_stat all", "fld_wire_stat", "rba_wire_stat", "pc_wire_stat"
};
static char status_replies[4][1024];
static time_t status_time[4];

uint8_t exp_uart_cached_command(const char *command, char *output, size_t capacity)
{
    if (!command || !output || !capacity) return 1;
    size_t index;
    for (index = 0; index < 4; ++index)
        if (!strcmp(command, status_commands[index])) break;
    if (index == 4) return 1;
    pthread_mutex_lock(&snapshot_lock);
    time_t now = time(NULL);
    int fresh = status_time[index] && now >= status_time[index] &&
        now - status_time[index] <= 15 && status_replies[index][0];
    if (fresh && strlen(status_replies[index]) < capacity)
        strcpy(output, status_replies[index]);
    else fresh = 0;
    pthread_mutex_unlock(&snapshot_lock);
    return fresh ? 0 : 1;
}

static long milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static int open_port(void)
{
    const char *configured = getenv("DAL_EXP_UART_PORT");
    const char *path = configured && *configured ? configured : "/dev/ttyS4";
    const char *rate_text = getenv("DAL_EXP_UART_BAUD");
    char *end = NULL;
    long rate = rate_text && *rate_text ? strtol(rate_text, &end, 10) : 250000L;
    if (rate < 9600 || rate > 3000000 || (rate_text && *rate_text && *end)) return -2;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios2 tty;
    if (ioctl(fd, TCGETS2, &tty) < 0) { close(fd); return -1; }
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cflag &= ~(CBAUD | CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= BOTHER | CS8 | CLOCAL | CREAD;
    tty.c_ispeed = (unsigned int)rate;
    tty.c_ospeed = (unsigned int)rate;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    if (ioctl(fd, TCSETS2, &tty) < 0) { close(fd); return -1; }
    return fd;
}

static uint8_t request_locked(int fd, const char *command, char *out,
                              size_t cap, long timeout_ms)
{
    if (cap == 0) return 4;
    out[0] = 0;
    uint8_t discard[256];
    while (read(fd, discard, sizeof(discard)) > 0) {}
    char line[128];
    size_t length = strlen(command);
    if (length + 1 > sizeof(line)) return 1;
    memcpy(line, command, length);
    line[length++] = '\r';
    size_t written = 0, used = 0;
    long deadline = milliseconds() + timeout_ms;
    while (written < length) {
        if (milliseconds() >= deadline) return 2;
        struct pollfd pfd = {.fd = fd, .events = POLLOUT};
        if (poll(&pfd, 1, 100) < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
        ssize_t n = write(fd, line + written, length - written);
        if (n > 0) written += (size_t)n;
        else if (n < 0 && errno != EAGAIN && errno != EINTR) return 1;
    }
    /* The MCU reset command does not return a CLI prompt. Drain transmission
     * before closing the UART, but do not claim that the reboot completed. */
    if (strcmp(command, "reset") == 0) {
        int drained;
        do { drained = ioctl(fd, TCSBRK, 1); } while (drained < 0 && errno == EINTR);
        if (drained < 0) return 1;
        snprintf(out, cap, "EXP reset command sent; reboot not confirmed");
        return 0;
    }
    while (milliseconds() < deadline) {
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int ready = poll(&pfd, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
        if (ready == 0) continue;
        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (n <= 0) return 1;
        for (ssize_t i = 0; i < n; ++i) {
            if (used + 1 >= cap) return 4;
            out[used++] = buf[i];
            out[used] = 0;
            size_t prompt = sizeof(EXP_PROMPT) - 1;
            if (used > prompt && out[used - prompt - 1] == '\n' &&
                !memcmp(out + used - prompt, EXP_PROMPT, prompt))
                return 0;
        }
    }
    return 2;
}

static int json_integer(const char *text, const char *key, unsigned *value)
{
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(text, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), ':');
    if (!p) return -1;
    ++p;
    while (*p == ' ') ++p;
    char *end = NULL;
    unsigned long n = strtoul(p, &end, 10);
    if (end == p || n > 65535) return -1;
    *value = (unsigned)n;
    return 0;
}

static int decode_chunk(const char *reply, char *out, size_t cap, size_t *count)
{
    const char *p = strstr(reply, "\"data\"");
    if (!p || !(p = strchr(p + 6, ':'))) return -1;
    ++p;
    while (*p == ' ') ++p;
    if (*p++ != '"') return -1;
    size_t used = 0;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            c = (unsigned char)*p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'u') {
                if (p[0] != '0' || p[1] != '0') return -1;
                char hex[3] = {p[2], p[3], 0};
                char *end = NULL;
                unsigned long n = strtoul(hex, &end, 16);
                if (end != hex + 2) return -1;
                c = (unsigned char)n;
                p += 4;
            } else if (c != '"' && c != '\\' && c != '/') return -1;
        }
        if (used >= cap) return -1;
        out[used++] = (char)c;
    }
    if (*p != '"') return -1;
    *count = used;
    return 0;
}

static uint8_t command_timeout(const char *command, char *output,
                               size_t capacity, long timeout_ms)
{
    if (!command || !*command || strlen(command) > 95 || strchr(command, '\n') ||
        strchr(command, '\r')) return 1;
    pthread_mutex_lock(&exp_lock);
    int fd = open_port();
    uint8_t result;
    if (fd == -2) result = 3;
    else if (fd < 0) result = 1;
    else {
        result = request_locked(fd, command, output, capacity, timeout_ms);
        close(fd);
    }
    if (fd < 0 && capacity) snprintf(output, capacity, "EXP UART %s",
                                      fd == -2 ? "port not configured" : "open failed");
    pthread_mutex_unlock(&exp_lock);
    return result;
}

uint8_t exp_uart_command(const char *command, char *output, size_t capacity)
{
    return command_timeout(command, output, capacity, 15000);
}

uint8_t exp_uart_sensor_command(const char *command, char *output, size_t capacity)
{
    return command_timeout(command, output, capacity, 2000);
}

uint8_t exp_uart_param_show(unsigned table, char *output, size_t capacity)
{
    if (table != 0 && table != 2 && table != 3 && table != 6 && table != 12) return 1;
    char command[32];
    snprintf(command, sizeof(command), "param_show %u", table);
    return command_timeout(command, output, capacity, 4000);
}

uint8_t exp_uart_telemetry(const char *section, char *output, size_t capacity)
{
    static const char *sections[] = {
        "system", "sensors", "power", "pc", "heaters", "pump", "valves",
        "motors", "burn", "ir", "cameras", "data", "all"
    };
    int valid = 0;
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i)
        if (!strcmp(section, sections[i])) valid = 1;
    if (!valid || capacity < 2) return 1;
    pthread_mutex_lock(&exp_lock);
    int fd = open_port();
    if (fd < 0) {
        snprintf(output, capacity, "EXP UART %s",
                 fd == -2 ? "port not configured" : "open failed");
        pthread_mutex_unlock(&exp_lock);
        return fd == -2 ? 3 : 1;
    }
    char command[32], reply[EXP_REPLY_CAP];
    snprintf(command, sizeof(command), "tlm_json %s", section);
    uint8_t result = request_locked(fd, command, reply, sizeof(reply), 15000);
    if (result) goto done;
    const char *json = strchr(reply, '{');
    if (!json) { result = 1; goto done; }
    if (!strstr(json, "\"CHUNK_REQUIRED\"")) {
        const char *end = strrchr(json, '}');
        if (!end || (size_t)(end - json + 2) > capacity) { result = 4; goto done; }
        size_t n = (size_t)(end - json + 1);
        memcpy(output, json, n);
        output[n] = 0;
        goto done;
    }
    unsigned report_id, total, next;
    if (json_integer(json, "report_id", &report_id) ||
        json_integer(json, "total", &total) ||
        json_integer(json, "next", &next) ||
        total + 1 > capacity || next != 0) { result = 4; goto done; }
    size_t used = 0;
    while (used < total) {
        snprintf(command, sizeof(command), "json_chunk %u %zu", report_id, used);
        result = request_locked(fd, command, reply, sizeof(reply), 15000);
        if (result) goto done;
        size_t count = 0;
        unsigned chunk_id, offset, chunk_next, chunk_total;
        if (json_integer(reply, "report_id", &chunk_id) || chunk_id != report_id ||
            json_integer(reply, "offset", &offset) || offset != used ||
            json_integer(reply, "next", &chunk_next) ||
            json_integer(reply, "total", &chunk_total) || chunk_total != total ||
            decode_chunk(reply, output + used, capacity - used - 1, &count) ||
            count == 0 || used + count > total || chunk_next != used + count) {
            result = 1; goto done;
        }
        used += count;
    }
    output[used] = 0;
done:
    if (result && capacity) snprintf(output, capacity, "EXP telemetry failed (%u)", result);
    close(fd);
    pthread_mutex_unlock(&exp_lock);
    return result;
}

static void *poll_snapshots(void *unused)
{
    (void)unused;
    const char *sections[] = {"system", "sensors", "power"};
    size_t index = 0, json_index = 0;
    char report[1024];
    char json_report[16384];
    while (atomic_load(&snapshot_running)) {
        const char *csp_enable = getenv("DAL_EXP_CSP_ENABLE");
        bool legacy_status = csp_enable && !strcmp(csp_enable, "0");
        uint8_t status = 0;
        if (legacy_status) {
            status = command_timeout(status_commands[index], report, sizeof(report), 2000);
            pthread_mutex_lock(&snapshot_lock);
            if (!status) {
                strcpy(status_replies[index], report);
                status_time[index] = time(NULL);
            } else status_time[index] = 0;
            pthread_mutex_unlock(&snapshot_lock);
            index = (index + 1) % 4;
        }
        const char *poll_json = getenv("DAL_EXP_UART_POLL");
        if (poll_json && !strcmp(poll_json, "1")) {
            status = exp_uart_telemetry(sections[json_index], json_report, sizeof(json_report));
            if (!status && json_report[0] == '{') {
            pthread_mutex_lock(&snapshot_lock);
            snprintf(snapshot_sections[json_index], sizeof(snapshot_sections[json_index]), "%s", json_report);
            snapshot_time[json_index] = time(NULL);
            pthread_mutex_unlock(&snapshot_lock);
            }
            json_index = (json_index + 1) % 3;
        }
        if (index == 0)
            for (int i = 0; i < 50 && atomic_load(&snapshot_running); ++i)
                usleep(100000);
    }
    return NULL;
}

void exp_uart_service_start(void)
{
    const char *csp_enable = getenv("DAL_EXP_CSP_ENABLE");
    const char *json_poll = getenv("DAL_EXP_UART_POLL");
    if ((!csp_enable || strcmp(csp_enable, "0")) &&
        (!json_poll || strcmp(json_poll, "1"))) return;
    if (atomic_exchange(&snapshot_running, true)) return;
    if (pthread_create(&snapshot_thread, NULL, poll_snapshots, NULL) != 0)
        atomic_store(&snapshot_running, false);
}

void exp_uart_service_stop(void)
{
    if (!atomic_exchange(&snapshot_running, false)) return;
    pthread_join(snapshot_thread, NULL);
}

size_t exp_uart_snapshot(char *output, size_t capacity)
{
    if (!output || capacity < 128) return 0;
    const char *names[] = {"system", "sensors", "power"};
    size_t used = 0;
    pthread_mutex_lock(&snapshot_lock);
    used += (size_t)snprintf(output + used, capacity - used, "{");
    for (size_t i = 0; i < 3 && used < capacity; ++i) {
        const char *data = snapshot_sections[i][0] ? snapshot_sections[i] : "null";
        time_t age = snapshot_time[i] ? time(NULL) - snapshot_time[i] : -1;
        int n = snprintf(output + used, capacity - used,
                         "%s\"%s\":%s,\"%s_age_s\":%ld",
                         i ? "," : "", names[i], age >= 0 && age <= 30 ? data : "null",
                         names[i], (long)age);
        if (n < 0 || (size_t)n >= capacity - used) { used = 0; break; }
        used += (size_t)n;
    }
    pthread_mutex_unlock(&snapshot_lock);
    if (!used || used + 2 > capacity) { output[0] = 0; return 0; }
    output[used++] = '}';
    output[used] = 0;
    return used;
}
