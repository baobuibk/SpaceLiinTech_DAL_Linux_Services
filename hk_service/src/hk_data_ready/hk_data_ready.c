/*
 * hk_data_ready.c - MCU -> MPU data handoff over a shared SPI PSRAM
 */

#include "hk_data_ready.h"
#include <dirent.h>
#include "hk_csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* Bounded subprocess capture — see hk_can.c for the twin implementation. */
static int run_capture(char *const argv[], int timeout_ms,
                        char *out, size_t out_sz, int *exit_code)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    size_t used = 0;
    int    timed_out = 0;
    struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L
                        + (now.tv_nsec - start.tv_nsec) / 1000000L;
        int remaining = timeout_ms - (int)elapsed_ms;
        if (remaining <= 0) { timed_out = 1; break; }

        int pr = poll(&pfd, 1, remaining);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) { timed_out = 1; break; }

        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            if (used + 1 < out_sz) {
                ssize_t r = read(pipefd[0], out + used, out_sz - 1 - used);
                if (r > 0) { used += (size_t)r; continue; }
                if (r == 0) break;
                if (errno == EINTR) continue;
                break;
            } else {
                char scratch[256];
                ssize_t r = read(pipefd[0], scratch, sizeof(scratch));
                if (r <= 0) break;
                continue;
            }
        }
    }
    out[used] = '\0';
    close(pipefd[0]);

    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        if (exit_code) *exit_code = -1;
        return -1;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
}

static long long epoch_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* Parses:
 *   TypeName: DLS_24
 *   Size: 0x0728
 *   CRC16XModem: 0xC0DE
 * (order-independent, extra lines ignored). Returns 0 if TypeName and
 * Size were both found (CRC is reference-only, per spec no retry on it). */
static int parse_data_ready_response(const char *text, char *type_name, size_t tn_sz,
                                      uint32_t *size_bytes, uint32_t *crc)
{
    int have_type = 0, have_size = 0;
    *crc = 0;

    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", text);

    char *saveptr = NULL;
    for (char *line = strtok_r(buf, "\r\n", &saveptr); line;
         line = strtok_r(NULL, "\r\n", &saveptr)) {

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "TypeName:", 9) == 0) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            snprintf(type_name, tn_sz, "%s", p);
            /* trim trailing whitespace */
            size_t len = strlen(type_name);
            while (len > 0 && (type_name[len-1] == ' ' || type_name[len-1] == '\t'))
                type_name[--len] = '\0';
            have_type = (len > 0);
        } else if (strncmp(p, "Size:", 5) == 0) {
            p += 5;
            *size_bytes = (uint32_t)strtoul(p, NULL, 0);
            have_size = 1;
        } else if (strncmp(p, "CRC16XModem:", 12) == 0) {
            p += 12;
            *crc = (uint32_t)strtoul(p, NULL, 0);
        }
    }

    return (have_type && have_size) ? 0 : -1;
}

static int run_spi_dump(const HkDataReadyCfg *cfg, int spi_bus, uint32_t size_bytes,
                         const char *out_path)
{
    char bus_str[8], size_str[16];
    snprintf(bus_str, sizeof(bus_str), "%d", spi_bus);
    snprintf(size_str, sizeof(size_str), "%u", size_bytes);

    char *argv[] = {
        (char *)cfg->spi_flash_path, "-d", bus_str, "--dump",
        "-a", "0", "-s", size_str, "-o", (char *)out_path, NULL
    };

    /* Generous, size-scaled but bounded watchdog: base + ~50ms/KB, capped. */
    long timeout_ms = cfg->dump_timeout_base_ms + (long)(size_bytes / 1024) * 50L;
    if (timeout_ms > 120000L) timeout_ms = 120000L;

    char out[2048];
    int  exit_code = -1;
    if (run_capture(argv, (int)timeout_ms, out, sizeof(out), &exit_code) < 0)
        return -1;
    return (exit_code == 0) ? 0 : -1;
}

static void run_sequence(const HkDataReadyCfg *dr_cfg, const HkCanCfg *can_cfg,
                          const char *board_name, int node_addr, int spi_bus)
{
    char resp[4096];
    fprintf(stderr, "[data_ready] %s: pin confirmed high, sending data_ready_collect\n",
            board_name);

    if (hk_can_run(can_cfg, node_addr, "data_ready_collect", resp, sizeof(resp)) != 0) {
        fprintf(stderr, "[data_ready] %s: data_ready_collect failed (no reply/error) -- "
                "skipping, MCU not confirmed to have switched SEL\n", board_name);
        return;
    }

    char     type_name[64] = { 0 };
    uint32_t size_bytes = 0, crc = 0;
    int      parsed = (parse_data_ready_response(resp, type_name, sizeof(type_name),
                                                  &size_bytes, &crc) == 0);

    if (!parsed) {
        fprintf(stderr, "[data_ready] %s: could not parse data_ready_collect response:\n%s\n",
                board_name, resp);
    } else {
        fprintf(stderr, "[data_ready] %s: TypeName=%s Size=0x%X CRC16XModem=0x%X (reference only)\n",
                board_name, type_name, size_bytes, crc);

        const char *folder = "data/other", *prefix = "OTHER";
        if (!strncmp(type_name, "LWL", 3)) { folder = "logs/lwl"; prefix = "LWL"; }
        else if (!strncmp(type_name, "PC", 2)) { folder = "logs/pc"; prefix = "PC"; }
        else if (!strncmp(type_name, "LEV", 3)) { folder = "data/lev"; prefix = "LEV"; }
        char category_dir[300], source_dir[300];
        snprintf(category_dir, sizeof(category_dir), "%s/%s", dr_cfg->data_dir,
                 folder[0] == 'l' ? "logs" : "data");
        snprintf(source_dir, sizeof(source_dir), "%s/%s", dr_cfg->data_dir, folder);
        if ((mkdir(category_dir, 0755) == 0 || errno == EEXIST) &&
            (mkdir(source_dir, 0755) == 0 || errno == EEXIST)) {
            char out_path[512];
            unsigned sequence = (unsigned)(epoch_ms() % 100000000LL);
            for (unsigned tries = 0; tries < 100000000; tries++) {
                snprintf(out_path, sizeof(out_path), "%s/%s_%08u.bin",
                         source_dir, prefix, sequence);
                if (access(out_path, F_OK) != 0 && errno == ENOENT) break;
                sequence = (sequence + 1) % 100000000U;
            }

            if (run_spi_dump(dr_cfg, spi_bus, size_bytes, out_path) == 0)
                fprintf(stderr, "[data_ready] %s: dumped %u bytes -> %s\n",
                        board_name, size_bytes, out_path);
            else
                fprintf(stderr, "[data_ready] %s: spi_flash dump FAILED (%s)\n",
                        board_name, out_path);
        } else {
            fprintf(stderr, "[data_ready] %s: could not create source dir under %s\n",
                    board_name, dr_cfg->data_dir);
        }
    }

    /* Always tell the MCU we're done once collect succeeded, regardless
     * of dump outcome, so it never stays stuck holding SEL over to MPU. */
    if (hk_can_run(can_cfg, node_addr, "data_ready_done", resp, sizeof(resp)) != 0)
        fprintf(stderr, "[data_ready] %s: data_ready_done failed (best-effort, not retried)\n",
                board_name);
}

void hk_data_ready_ctx_init(HkDataReadyCtx *ctx)
{
    ctx->state = HK_DR_IDLE;
    ctx->high_ms = 0;
}

void hk_data_ready_tick(const HkDataReadyCfg *dr_cfg, const HkXioCfg *xio_cfg,
                         const HkCanCfg *can_cfg, HkDataReadyCtx *ctx,
                         const char *board_name, int node_addr,
                         const char *pin_name, int spi_bus, int online,
                         uint32_t elapsed_ms)
{
    if (!online) {
        ctx->state = HK_DR_IDLE;
        ctx->high_ms = 0;
        return;
    }

    int active = hk_xio_input_active(xio_cfg, pin_name);
    if (active < 0)
        return; /* read error this tick: skip, don't advance or reset */

    switch (ctx->state) {
    case HK_DR_IDLE:
        if (active) {
            ctx->state = HK_DR_DEBOUNCE;
            ctx->high_ms = 0;
        }
        break;

    case HK_DR_DEBOUNCE:
        if (active) {
            ctx->high_ms += elapsed_ms;
            if (ctx->high_ms >= dr_cfg->debounce_ms) {
                run_sequence(dr_cfg, can_cfg, board_name, node_addr, spi_bus);
                ctx->state = HK_DR_WAIT_LOW;
            }
        } else {
            ctx->state = HK_DR_IDLE;
            ctx->high_ms = 0;
        }
        break;

    case HK_DR_WAIT_LOW:
        if (!active) {
            ctx->state = HK_DR_IDLE;
            ctx->high_ms = 0;
        }
        break;
    }
}
