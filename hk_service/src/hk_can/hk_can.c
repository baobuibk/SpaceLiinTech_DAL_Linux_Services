/*
 * hk_can.c - CAN access to the single DAL EXP via lite-client
 */

#include "hk_can.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/wait.h>

/* -- Bounded subprocess capture ---------------------------------------
 * Runs argv, capturing merged stdout+stderr into out (NUL-terminated).
 * If the child does not finish within timeout_ms, it is SIGKILLed and
 * this function returns -1 (watchdog fired). Otherwise returns 0 and
 * *exit_code is the child's exit status (-1 if it didn't exit normally).
 */
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
                if (r == 0) break; /* EOF: child closed pipe */
                if (errno == EINTR) continue;
                break;
            } else {
                /* Output buffer full: drain without storing so the
                 * child never blocks on a full pipe. */
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

int hk_can_ping(const HkCanCfg *cfg, int node_addr)
{
    char can_opt[64], node_str[16];
    snprintf(can_opt, sizeof(can_opt), "-c=%s", cfg->can_if);
    snprintf(node_str, sizeof(node_str), "%d", node_addr);

    char *argv[] = {
        (char *)cfg->lite_client_path, can_opt, "ping", node_str, NULL
    };

    char out[2048];
    int  exit_code = -1;
    int  watchdog_ms = cfg->ping_timeout_ms + 2000;

    if (run_capture(argv, watchdog_ms, out, sizeof(out), &exit_code) < 0)
        return -1;

    if (exit_code == 0 && strstr(out, "reply in") != NULL)
        return 1;

    return 0;
}

static int parse_values(const char *s, int64_t *values, int count,
                        int is_signed, unsigned bits)
{
    for (int i = 0; i < count; i++) {
        char *end;
        errno = 0;
        long long v = strtoll(s, &end, 0);
        if (errno || end == s ||
            (is_signed ? (v < -(1LL << (bits - 1)) ||
                          v > (1LL << (bits - 1)) - 1) :
                         (v < 0 || (uint64_t)v > ((1ULL << bits) - 1))))
            return -1;
        values[i] = v;
        s = end;
    }
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    return *s == '\0' ? 0 : -1;
}

/* Bit positions correspond to the generated DAL RPARAM table 2 rows. */
int hk_can_parse_sensors(const char *text, HkExpSensors *out)
{
    if (!text || !out) return -1;
    memset(out, 0, sizeof(*out));
    char *copy = strdup(text);
    if (!copy) return -1;
    uint32_t seen = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "0x", 2) != 0) continue;
        char addr[16], name[32], type[8];
        int used = 0;
        if (sscanf(p, "%15s %31s %7s %n", addr, name, type, &used) < 3 ||
            used <= 0) continue;
        const char *s = p + used;
        int64_t v[10];
        int index = -1, count = 1, signed_value = 0, bits = 32;
        const char *names[] = {
            "schema", "seq", "tick_ms", "supported", "valid", "status",
            "age_ms", "ntc_mC", "board_mC", "bme_mC", "bme_Pa",
            "bme_mRH", "accel_mg", "gyro_mdps", "imu_mC", "flow_x100",
            "flow_mC", "voltage_mV", "current_mA", "power_mW",
            "rail_enabled", "imu_mask"
        };
        for (int i = 0; i < 22; i++)
            if (strcmp(name, names[i]) == 0) { index = i; break; }
        if (index < 0) continue;
        if (seen & (1u << index)) goto fail;
        switch (index) {
        case 0: case 3: case 4: bits = 16; break;
        case 5: count = 10; bits = 8; break;
        case 6: count = 10; break;
        case 7: count = 4; signed_value = 1; break;
        case 8: case 9: case 14: signed_value = 1; break;
        case 12: case 13: count = 3; signed_value = 1; break;
        case 15: case 16: count = 2; signed_value = 1; break;
        case 17: count = 4; break;
        case 18: case 19: count = 4; signed_value = 1; break;
        case 20: count = 3; bits = 8; break;
        case 21: bits = 8; break;
        default: break;
        }
        if (parse_values(s, v, count, signed_value, (unsigned)bits) < 0)
            goto fail;
        seen |= 1u << index;
        switch (index) {
        case 0: out->schema = (uint16_t)v[0]; break;
        case 1: out->seq = (uint32_t)v[0]; break;
        case 2: out->tick_ms = (uint32_t)v[0]; break;
        case 3: out->supported = (uint16_t)v[0]; break;
        case 4: out->valid = (uint16_t)v[0]; break;
        case 5: for (int i=0;i<10;i++) out->status[i]=(uint8_t)v[i]; break;
        case 6: for (int i=0;i<10;i++) out->age_ms[i]=(uint32_t)v[i]; break;
        case 7: for (int i=0;i<4;i++) out->ntc_c[i]=(float)v[i]/1000; break;
        case 8: out->board_temp_c=(float)v[0]/1000; break;
        case 9: out->bme_temp_c=(float)v[0]/1000; break;
        case 10: out->bme_press_pa=(uint32_t)v[0]; break;
        case 11: out->bme_hum_rh=(float)v[0]/1000; break;
        case 12: for (int i=0;i<3;i++) out->accel_mg[i]=(int32_t)v[i]; break;
        case 13: for (int i=0;i<3;i++) out->gyro_dps[i]=(float)v[i]/1000; break;
        case 14: out->imu_temp_c=(float)v[0]/1000; break;
        case 15: for (int i=0;i<2;i++) out->flow_ul_min[i]=(float)v[i]/100; break;
        case 16: for (int i=0;i<2;i++) out->flow_temp_c[i]=(float)v[i]/1000; break;
        case 17: for (int i=0;i<4;i++) out->pwr_volt_mv[i]=(uint32_t)v[i]; break;
        case 18: for (int i=0;i<4;i++) out->pwr_curr_ma[i]=(int32_t)v[i]; break;
        case 19: for (int i=0;i<4;i++) out->pwr_mw[i]=(int32_t)v[i]; break;
        case 20: for (int i=0;i<3;i++) out->rail_enabled[i]=(uint8_t)v[i]; break;
        case 21: out->imu_mask=(uint8_t)v[0]; break;
        }
    }
    free(copy);
    /* Reject incomplete dumps, unexpected schema and unqualified valid bits. */
    if (seen != ((1u << 22) - 1) || out->schema != 5 ||
        (out->valid & ~out->supported)) return -1;
    return 0;
fail:
    free(copy);
    memset(out, 0, sizeof(*out));
    return -1;
}

int hk_can_getall_sensors(const HkCanCfg *cfg, int node_addr,
                          HkExpSensors *out)
{
    char can_opt[64], node_str[16], table_str[16];
    snprintf(can_opt, sizeof(can_opt), "-c=%s", cfg->can_if);
    snprintf(node_str, sizeof(node_str), "%d", node_addr);
    snprintf(table_str, sizeof(table_str), "%d", cfg->table_id);
    char *argv[] = {
        (char *)cfg->lite_client_path, can_opt, "getall", node_str, table_str, NULL
    };
    char out_buf[8192];
    int exit_code = -1;
    if (run_capture(argv, cfg->getall_timeout_ms + 3000, out_buf,
                    sizeof(out_buf), &exit_code) < 0 || exit_code != 0)
        return -1;
    return hk_can_parse_sensors(out_buf, out);
}

int hk_can_run(const HkCanCfg *cfg, int node_addr, const char *command,
               char *out_buf, size_t out_sz)
{
    char can_opt[64], node_str[16];
    snprintf(can_opt, sizeof(can_opt), "-c=%s", cfg->can_if);
    snprintf(node_str, sizeof(node_str), "%d", node_addr);

    char *argv[] = {
        (char *)cfg->lite_client_path, can_opt, "run", node_str,
        (char *)command, NULL
    };

    int exit_code = -1;
    if (run_capture(argv, cfg->rgosh_timeout_ms, out_buf, out_sz, &exit_code) < 0)
        return -1;

    return (exit_code == 0) ? 0 : -1;
}
