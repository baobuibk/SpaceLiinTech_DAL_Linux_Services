#include "services/exp_data_service.h"
#include "services/exp_csp_service.h"
#include "services/exp_uart_service.h"
#include "bee_dal_crc32.h"
#include "dal_obc_config.h"

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define READ_CHUNK 32U /* MCU UART CLI data_read limit. */
#define MAX_OBJECT 10242U

static pthread_t worker;
static atomic_bool running;
static char data_root[512];
static pthread_mutex_t saved_lock = PTHREAD_MUTEX_INITIALIZER;
static bool saved_valid;
static uint32_t saved_boot, saved_object;
static unsigned saved_type, saved_source;
static char saved_file[160];

int exp_data_last_saved_json(char *output, size_t output_size)
{
    if (!output || !output_size) return -1;
    pthread_mutex_lock(&saved_lock);
    int n = saved_valid ?
        snprintf(output, output_size,
                 "{\"boot_id\":%u,\"object_id\":%u,\"type\":%u,\"source\":%u,\"file\":\"%s\"}",
                 saved_boot, saved_object, saved_type, saved_source, saved_file) :
        snprintf(output, output_size, "null");
    pthread_mutex_unlock(&saved_lock);
    return n >= 0 && (size_t)n < output_size ? 0 : -1;
}

typedef struct {
    uint32_t boot, object, length, crc32, run, sequence, created_ms;
    unsigned type, source, encoding, width, height;
} descriptor_t;

static int mkdirs(const char *path)
{
    char copy[512];
    if (!path || path[0] != '/' || strlen(path) >= sizeof(copy)) return -1;
    strcpy(copy, path);
    for (char *p = copy + 1; ; ++p) {
        if (*p != '/' && *p != '\0') continue;
        char save = *p;
        *p = '\0';
        if (mkdir(copy, 0755) && errno != EEXIST) return -1;
        struct stat st;
        if (stat(copy, &st) || !S_ISDIR(st.st_mode)) return -1;
        *p = save;
        if (!save) return 0;
    }
}

/* Keep UART control characters visible in a single journal record. */
static void log_reply(const char *reply)
{
    for (size_t i = 0; reply[i] && i < 1024; ++i) {
        unsigned char c = (unsigned char)reply[i];
        if (c == '\r') fputs("\\r", stderr);
        else if (c == '\n') fputs("\\n", stderr);
        else if (c < 32 || c >= 127) fprintf(stderr, "\\x%02X", c);
        else fputc(c, stderr);
    }
    fputc('\n', stderr);
}

static int command(const char *line, char *reply, size_t cap)
{
    reply[0] = '\0';
    int status = exp_uart_command(line, reply, cap);
    if (status != 0 || strstr(reply, "ERROR")) {
        fprintf(stderr, "[EXP-DATA] command failed: %s; UART status=%d reply=",
                line, status);
        log_reply(reply);
        return -1;
    }
    return 0;
}

static int parse_descriptor(const char *reply, descriptor_t *d)
{
    const char *p = strstr(reply, "DATA boot=");
    if (!p || !d) return -1;
    return sscanf(p, "DATA boot=%u object=%u type=%u source=%u encoding=%u width=%u "
                  "height=%u length=%u crc32=0x%x run=%u seq=%u ms=%u",
                  &d->boot, &d->object, &d->type, &d->source, &d->encoding,
                  &d->width, &d->height, &d->length, &d->crc32,
                  &d->run, &d->sequence, &d->created_ms) == 12 ? 0 : -1;
}

static int parse_hex_line(const char *line, unsigned length, uint8_t out[READ_CHUNK])
{
    for (unsigned i = 0; i < length; ++i) {
        unsigned x;
        if (!isxdigit((unsigned char)line[0]) ||
            !isxdigit((unsigned char)line[1]) ||
            sscanf(line, "%2x", &x) != 1) return -1;
        out[i] = (uint8_t)x;
        line += 2;
        if (i + 1 < length) {
            if (*line++ != ' ') return -1;
        }
    }
    return *line == '\0' || *line == '\r' ? 0 : -1;
}

static int read_chunk_once(const descriptor_t *d, uint32_t offset,
                      unsigned length, uint8_t out[READ_CHUNK])
{
    char line[96], reply[4096];
    snprintf(line, sizeof(line), "data_read %u %u %u %u", d->boot, d->object,
             offset, length);
    if (command(line, reply, sizeof(reply))) return -1;
    for (char *p = reply; *p;) {
        char *end = strchr(p, '\n');
        if (!end) {
            if (parse_hex_line(p, length, out) == 0) return 0;
            break;
        }
        char save = *end;
        *end = '\0';
        if (parse_hex_line(p, length, out) == 0) { *end = save; return 0; }
        *end = save;
        p = end + 1;
    }
    fprintf(stderr, "[EXP-DATA] invalid read reply: boot=%u object=%u offset=%u length=%u reply=",
            d->boot, d->object, offset, length);
    log_reply(reply);
    return -1;
}

/* data_read is read-only: retry the same range, never advance on failure. */
static int read_chunk(const descriptor_t *d, uint32_t offset,
                      unsigned length, uint8_t out[READ_CHUNK])
{
    for (unsigned attempt = 0; attempt < 3 && atomic_load(&running); ++attempt) {
        if (read_chunk_once(d, offset, length, out) == 0) return 0;
        if (attempt < 2) {
            fprintf(stderr, "[EXP-DATA] retry read: object=%u offset=%u attempt=%u/3\n",
                    d->object, offset, attempt + 2);
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
            nanosleep(&delay, NULL);
        }
    }
    return -1;
}

static int crc_file(const char *path, uint32_t length, uint32_t expected)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat st;
    uint32_t crc = bee_dal_crc32_init();
    uint8_t bytes[512];
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size != (off_t)length) {
        close(fd); return -1;
    }
    for (uint32_t done = 0; done < length;) {
        size_t want = length - done < sizeof(bytes) ? length - done : sizeof(bytes);
        ssize_t n = read(fd, bytes, want);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; close(fd); return -1; }
        crc = bee_dal_crc32_update(crc, bytes, (size_t)n);
        done += (uint32_t)n;
    }
    close(fd);
    return bee_dal_crc32_finalize(crc) == expected ? 0 : -1;
}

static int save_object(const descriptor_t *d, const char *path)
{
    if (access(path, F_OK) == 0) return crc_file(path, d->length, d->crc32);
    char temp[640];
    if (snprintf(temp, sizeof(temp), "%s.part.XXXXXX", path) >= (int)sizeof(temp)) return -1;
    int fd = mkstemp(temp);
    if (fd < 0) {
        fprintf(stderr, "[EXP-DATA] cannot create %s: %s\n", temp, strerror(errno));
        return -1;
    }
    fchmod(fd, 0644);
    uint32_t crc = bee_dal_crc32_init();
    int ok = 1;
    for (uint32_t offset = 0; ok && offset < d->length;) {
        unsigned count = d->length - offset < READ_CHUNK ? d->length - offset : READ_CHUNK;
        uint8_t bytes[READ_CHUNK];
        if (read_chunk(d, offset, count, bytes)) { ok = 0; break; }
        for (unsigned sent = 0; sent < count;) {
            ssize_t n = write(fd, bytes + sent, count - sent);
            if (n > 0) sent += (unsigned)n;
            else if (n < 0 && errno == EINTR) continue;
            else { ok = 0; break; }
        }
        crc = bee_dal_crc32_update(crc, bytes, count);
        offset += count;
    }
    if (ok && bee_dal_crc32_finalize(crc) != d->crc32) {
        fprintf(stderr, "[EXP-DATA] CRC mismatch: object=%u expected=%08X calculated=%08X\n",
                d->object, d->crc32, bee_dal_crc32_finalize(crc));
        ok = 0;
    }
    if (fsync(fd)) { fprintf(stderr, "[EXP-DATA] fsync %s: %s\n", temp, strerror(errno)); ok = 0; }
    if (close(fd)) ok = 0;
    if (ok && rename(temp, path) == 0) {
        char parent[640];
        snprintf(parent, sizeof(parent), "%s", path);
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            int dir = open(parent, O_RDONLY | O_DIRECTORY);
            if (dir >= 0) { fsync(dir); close(dir); }
        }
        return 0;
    }
    unlink(temp);
    return -1;
}

static int collect(const HkExpInventory *inventory)
{
    char line[96], reply[1024], folder[640], path[640], name[80];
    snprintf(line, sizeof(line), "data_ready_collect %u", inventory->object_id);
    if (command(line, reply, sizeof(reply))) return -1;
    descriptor_t d;
    if (parse_descriptor(reply, &d) || d.boot != inventory->boot_id ||
        d.object != inventory->object_id || d.length != inventory->length ||
        d.crc32 != inventory->crc32 || d.type != inventory->type ||
        d.source != inventory->source || d.encoding != inventory->encoding ||
        d.width != inventory->width || d.height != inventory->height ||
        d.length == 0 || d.length > MAX_OBJECT || d.type < 1 || d.type > 3 ||
        (d.type == 3 && d.source > 1)) {
        fprintf(stderr, "[EXP-DATA] descriptor invalid or differs from table 6: boot=%u object=%u length=%u CRC=%08X type=%u source=%u encoding=%u size=%ux%u reply=%.350s\n",
                inventory->boot_id, inventory->object_id, inventory->length, inventory->crc32,
                inventory->type, inventory->source, inventory->encoding, inventory->width, inventory->height, reply);
        return -1;
    }
    const char *subdir = d.type == 3 ? "images/exp" :
                         d.type == 2 ? "logs/pc" : "logs/lwl";
    if (snprintf(folder, sizeof(folder), "%s/%s", data_root, subdir) >= (int)sizeof(folder) ||
        mkdirs(folder)) {
        fprintf(stderr, "[EXP-DATA] cannot create destination %s: %s\n", folder, strerror(errno));
        return -1;
    }
    if (d.type == 3)
        snprintf(name, sizeof(name), "EXP_C%u_B%08X_O%08X_E%u.bin",
                 d.source, d.boot, d.object, d.encoding);
    else snprintf(name, sizeof(name), "EXP_B%08X_O%08X.bin", d.boot, d.object);
    if (snprintf(path, sizeof(path), "%s/%s", folder, name) >= (int)sizeof(path) ||
        save_object(&d, path)) return -1;
    pthread_mutex_lock(&saved_lock);
    saved_boot = d.boot;
    saved_object = d.object;
    saved_type = d.type;
    saved_source = d.source;
    snprintf(saved_file, sizeof(saved_file), "%s/%s", subdir, name);
    saved_valid = true;
    pthread_mutex_unlock(&saved_lock);
    snprintf(line, sizeof(line), "data_ready_done %u %u %u %u",
             d.boot, d.object, d.length, d.crc32);
    if (command(line, reply, sizeof(reply)) || !strstr(reply, "DATA result=0")) {
        fprintf(stderr, "[EXP-DATA] file saved but ACK failed: %s reply=%.300s\n", path, reply);
        return -1;
    }
    fprintf(stderr, "[EXP-DATA] saved %s (%u bytes, CRC32=%08X)\n",
            path, d.length, d.crc32);
    return 0;
}

static void *run(void *unused)
{
    (void)unused;
    uint32_t failed_boot = 0, failed_object = 0;
    uint32_t done_boot = 0, done_object = 0;
    time_t retry_at = 0;
    while (atomic_load(&running)) {
        HkExpInventory inventory;
        if (exp_csp_inventory(&inventory) == 0 && inventory.ready &&
            inventory.pending && inventory.object_id &&
            (inventory.boot_id != done_boot || inventory.object_id != done_object) &&
            (inventory.boot_id != failed_boot || inventory.object_id != failed_object ||
             time(NULL) >= retry_at)) {
            if (collect(&inventory)) {
                failed_boot = inventory.boot_id;
                failed_object = inventory.object_id;
                retry_at = time(NULL) + 5;
                fprintf(stderr, "[EXP-DATA] transfer failed for boot=%u object=%u; retrying\n",
                        failed_boot, failed_object);
            } else {
                done_boot = inventory.boot_id;
                done_object = inventory.object_id;
                failed_boot = failed_object = 0;
            }
        }
        for (int i = 0; i < 10 && atomic_load(&running); ++i) usleep(100000);
    }
    return NULL;
}

int exp_data_service_start(void)
{
    pthread_mutex_lock(&saved_lock);
    saved_valid = false;
    pthread_mutex_unlock(&saved_lock);
    const char *enable = getenv("DAL_EXP_DATA_ENABLE");
    if (enable && !strcmp(enable, "0")) return 0;
    const char *root = getenv("DAL_EXP_DATA_DIR");
    if (!root || !*root) root = getenv("DAL_DATA_DIR");
    if (!root || !*root) root = BEEIF_DATA_DIR;
    if (root[0] != '/' || strlen(root) >= sizeof(data_root)) return -1;
    strcpy(data_root, root);
    if (mkdirs(data_root)) return -1;
    atomic_store(&running, true);
    if (pthread_create(&worker, NULL, run, NULL)) {
        atomic_store(&running, false); return -1;
    }
    fprintf(stderr, "[EXP-DATA] collecting table-6 objects to %s\n", data_root);
    return 0;
}

void exp_data_service_stop(void)
{
    if (!atomic_exchange(&running, false)) return;
    pthread_join(worker, NULL);
}
