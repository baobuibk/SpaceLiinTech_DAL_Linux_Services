/*
 * EXP CSP transport and RPARAM table 2 decoder, adapted from hk_service.
 */

#include "services/exp_csp_can.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
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

static unsigned client_args(const HkCanCfg *cfg, char **argv)
{
    unsigned n = 0;
    argv[n++] = (char *)cfg->lite_client_path;
    if (cfg->address_option[0]) argv[n++] = (char *)cfg->address_option;
    argv[n++] = (char *)cfg->transport_option;
    return n;
}

int hk_can_ping(const HkCanCfg *cfg, int node_addr)
{
    char node_str[16];
    snprintf(node_str, sizeof(node_str), "%d", node_addr);
    char *argv[6];
    unsigned n = client_args(cfg, argv);
    argv[n++] = "ping";
    argv[n++] = node_str;
    argv[n] = NULL;

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
            "rail_enabled", "sample_seq", "last_error", "desired", "applied",
            "imu_mask", "bme_gas_ohm", "bme_gas_valid"
        };
        for (int i = 0; i < 28; i++)
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
        case 21: count = 10; break;
        case 22: case 23: case 24: count = 10; bits = 8; break;
        case 25: case 27: bits = 8; break;
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
        case 21: for (int i=0;i<10;i++) out->sample_seq[i]=(uint32_t)v[i]; break;
        case 22: for (int i=0;i<10;i++) out->last_error[i]=(uint8_t)v[i]; break;
        case 23: for (int i=0;i<10;i++) out->desired[i]=(uint8_t)v[i]; break;
        case 24: for (int i=0;i<10;i++) out->applied[i]=(uint8_t)v[i]; break;
        case 25: out->imu_mask=(uint8_t)v[0]; break;
        case 26: out->bme_gas_ohm=(uint32_t)v[0]; break;
        case 27: out->bme_gas_valid=(uint8_t)v[0]; break;
        }
    }
    free(copy);
    /* Reject incomplete dumps, unexpected schema and unqualified valid bits. */
    if ((seen & (((1u << 21) - 1) | (1u << 25))) !=
        (((1u << 21) - 1) | (1u << 25)) ||
        (out->schema != 5 && out->schema != 6) ||
        (out->schema == 6 && seen != ((1u << 28) - 1)) ||
        (out->valid & ~out->supported)) return -1;
    return 0;
fail:
    free(copy);
    memset(out, 0, sizeof(*out));
    return -1;
}

static int getall_text(const HkCanCfg *cfg, int node_addr, int table,
                       char *out_buf, size_t out_size)
{
    char node_str[16], table_str[16];
    snprintf(node_str, sizeof(node_str), "%d", node_addr);
    snprintf(table_str, sizeof(table_str), "%d", table);
    char *argv[7];
    unsigned n = client_args(cfg, argv);
    argv[n++] = "getall";
    argv[n++] = node_str;
    argv[n++] = table_str;
    argv[n] = NULL;
    int exit_code = -1;
    if (run_capture(argv, cfg->getall_timeout_ms + 3000, out_buf,
                    out_size, &exit_code) < 0 || exit_code != 0)
        return -1;
    return 0;
}

int hk_can_getall_sensors(const HkCanCfg *cfg, int node_addr,
                          HkExpSensors *out)
{
    char text[8192];
    return getall_text(cfg, node_addr, 2, text, sizeof(text)) == 0 ?
           hk_can_parse_sensors(text, out) : -1;
}

int hk_can_parse_thermal(const char *text, HkExpThermal *out)
{
    if (!text || !out) return -1;
    static const struct { const char *name; unsigned addr, bits; const char *type; } rows[] = {
        {"schema", 0, 16, "U16"},
        {"seq", 2, 32, "U32"},
        {"tick_ms", 6, 32, "U32"},
        {"state", 10, 8, "U8"},
        {"enabled", 14, 8, "U8"},
        {"fault", 18, 8, "U8"},
        {"configured", 22, 8, "U8"},
        {"sensors_valid", 26, 8, "U8"},
        {"main_ntc", 30, 8, "U8"},
        {"sec_ntc", 34, 8, "U8"},
        {"main_mC", 38, 32, "I32"},
        {"sec_mC", 54, 32, "I32"},
        {"setpoint_mC", 70, 32, "I32"},
        {"request_pct", 86, 8, "U8"},
        {"applied_pct", 90, 8, "U8"},
        {"limit_pct", 94, 8, "U8"},
        {"main_age_ms", 98, 32, "U32"},
        {"sec_age_ms", 114, 32, "U32"},
        {"owner_kind", 130, 8, "U8"},
        {"owner_profile", 134, 8, "U8"},
        {"owner_valid", 138, 8, "U8"},
        {"pid_demand", 142, 0, "F32"},
        {"output_ms", 158, 32, "U32"},
        {"config_gen", 174, 32, "U32"},
    };
    HkExpThermal parsed = {0};
    char *copy = strdup(text);
    if (!copy) return -1;
    uint32_t seen = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        unsigned addr; char name[32], type[8]; int used = 0;
        if (sscanf(line, " 0x%x %31s %7s %n", &addr, name, type, &used) != 3 || used <= 0) continue;
        unsigned i = 0;
        while (i < sizeof(rows)/sizeof(rows[0]) && strcmp(name, rows[i].name)) i++;
        if (i == sizeof(rows)/sizeof(rows[0])) continue;
        if (addr != rows[i].addr || (seen & (1U << i)) ||
            (i != 21 && strcmp(type, rows[i].type))) goto fail;
        if (i == 21) {
            const char *pos = line + used;
            for (int j=0;j<4;j++) {
                char *end; errno = 0;
                float val = strtof(pos, &end);
                if (errno || end == pos || !isfinite(val)) goto fail;
                parsed.pid_demand[j] = val; pos = end;
            }
            while (*pos == ' ' || *pos == '\t' || *pos == '\r') pos++;
            if (*pos) goto fail;
        } else {
            int64_t v[4];
            if (parse_values(line + used, v, i < 3 ? 1 : 4,
                             rows[i].type[0] == 'I', rows[i].bits)) goto fail;
            if (i == 0) parsed.schema=(uint16_t)v[0];
            else if (i == 1) parsed.seq=(uint32_t)v[0];
            else if (i == 2) parsed.tick_ms=(uint32_t)v[0];
        else if (i == 3) for (int j=0;j<4;j++) parsed.state[j]=(uint8_t)v[j];
        else if (i == 4) for (int j=0;j<4;j++) parsed.enabled[j]=(uint8_t)v[j];
        else if (i == 5) for (int j=0;j<4;j++) parsed.fault[j]=(uint8_t)v[j];
        else if (i == 6) for (int j=0;j<4;j++) parsed.configured[j]=(uint8_t)v[j];
        else if (i == 7) for (int j=0;j<4;j++) parsed.sensors_valid[j]=(uint8_t)v[j];
        else if (i == 8) for (int j=0;j<4;j++) parsed.main_ntc[j]=(uint8_t)v[j];
        else if (i == 9) for (int j=0;j<4;j++) parsed.sec_ntc[j]=(uint8_t)v[j];
        else if (i == 10) for (int j=0;j<4;j++) parsed.main_mC[j]=(int32_t)v[j];
        else if (i == 11) for (int j=0;j<4;j++) parsed.sec_mC[j]=(int32_t)v[j];
        else if (i == 12) for (int j=0;j<4;j++) parsed.setpoint_mC[j]=(int32_t)v[j];
        else if (i == 13) for (int j=0;j<4;j++) parsed.request_pct[j]=(uint8_t)v[j];
        else if (i == 14) for (int j=0;j<4;j++) parsed.applied_pct[j]=(uint8_t)v[j];
        else if (i == 15) for (int j=0;j<4;j++) parsed.limit_pct[j]=(uint8_t)v[j];
        else if (i == 16) for (int j=0;j<4;j++) parsed.main_age_ms[j]=(uint32_t)v[j];
        else if (i == 17) for (int j=0;j<4;j++) parsed.sec_age_ms[j]=(uint32_t)v[j];
        else if (i == 18) for (int j=0;j<4;j++) parsed.owner_kind[j]=(uint8_t)v[j];
        else if (i == 19) for (int j=0;j<4;j++) parsed.owner_profile[j]=(uint8_t)v[j];
        else if (i == 20) for (int j=0;j<4;j++) parsed.owner_valid[j]=(uint8_t)v[j];
        else if (i == 22) for (int j=0;j<4;j++) parsed.output_ms[j]=(uint32_t)v[j];
        else if (i == 23) for (int j=0;j<4;j++) parsed.config_gen[j]=(uint32_t)v[j];
        }
        seen |= 1U << i;
    }
    free(copy);
    if (seen != ((1U << 24) - 1U) || parsed.schema != 6) return -1;
    for (int j=0;j<4;j++)
        if ((parsed.main_ntc[j] >= 4 && parsed.main_ntc[j] != 255) ||
            (parsed.sec_ntc[j] >= 4 && parsed.sec_ntc[j] != 255) ||
            parsed.configured[j] > 1 || parsed.enabled[j] > 1 ||
            parsed.sensors_valid[j] > 1 || parsed.applied_pct[j] > 100 ||
            parsed.request_pct[j] > 100) return -1;
    *out = parsed;
    return 0;
fail:
    free(copy);
    return -1;
}

int hk_can_getall_thermal(const HkCanCfg *cfg, int node_addr, HkExpThermal *out)
{
    char text[8192];
    return getall_text(cfg, node_addr, 3, text, sizeof(text)) == 0 ?
           hk_can_parse_thermal(text, out) : -1;
}

int hk_can_parse_inventory(const char *text, HkExpInventory *out)
{
    if (!text || !out) return -1;
    static const struct { const char *name; unsigned addr, count, bits; } rows[] = {
        {"schema",0,1,16}, {"seq",2,1,32}, {"tick_ms",6,1,32},
        {"boot_id",10,1,32}, {"ready",14,1,8}, {"result",15,1,8},
        {"pending",16,1,32}, {"published",20,1,32},
        {"acknowledged",24,1,32}, {"discarded",28,1,32},
        {"dropped",32,1,32}, {"type_count",36,3,8},
        {"capacity",39,1,8}, {"max_read",40,1,16},
        {"object_id",42,1,32}, {"length",46,1,32},
        {"crc32",50,1,32}, {"type",54,1,8},
        {"source",55,1,8}, {"encoding",56,1,8},
        {"width",57,1,16}, {"height",59,1,16},
        {"run_id",61,1,32}, {"object_seq",65,1,32},
        {"created_ms",69,1,32}, {"image_count",73,2,8}
    };
    memset(out, 0, sizeof(*out));
    char *copy = strdup(text);
    if (!copy) return -1;
    uint32_t seen = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        unsigned addr;
        char name[32], type[8];
        int used = 0;
        if (sscanf(line, " 0x%x %31s %7s %n", &addr, name, type, &used) != 3 ||
            used <= 0) continue;
        unsigned i;
        for (i = 0; i < sizeof(rows)/sizeof(rows[0]); ++i)
            if (!strcmp(name, rows[i].name)) break;
        if (i == sizeof(rows)/sizeof(rows[0])) continue;
        if (addr != rows[i].addr || (seen & (1u << i))) goto fail_inventory;
        if (strcmp(type, rows[i].bits == 8 ? "U8" : rows[i].bits == 16 ? "U16" : "U32"))
            goto fail_inventory;
        int64_t v[3];
        if (parse_values(line + used, v, (int)rows[i].count, 0, rows[i].bits) < 0)
            goto fail_inventory;
        seen |= 1u << i;
        switch (i) {
        case 0: out->schema=v[0]; break; case 1: out->seq=v[0]; break;
        case 2: out->tick_ms=v[0]; break; case 3: out->boot_id=v[0]; break;
        case 4: out->ready=v[0]; break; case 5: out->result=v[0]; break;
        case 6: out->pending=v[0]; break; case 7: out->published=v[0]; break;
        case 8: out->acknowledged=v[0]; break; case 9: out->discarded=v[0]; break;
        case 10: out->dropped=v[0]; break;
        case 11: for (int j=0;j<3;j++) out->type_count[j]=v[j]; break;
        case 12: out->capacity=v[0]; break; case 13: out->max_read=v[0]; break;
        case 14: out->object_id=v[0]; break; case 15: out->length=v[0]; break;
        case 16: out->crc32=v[0]; break; case 17: out->type=v[0]; break;
        case 18: out->source=v[0]; break; case 19: out->encoding=v[0]; break;
        case 20: out->width=v[0]; break; case 21: out->height=v[0]; break;
        case 22: out->run_id=v[0]; break; case 23: out->object_seq=v[0]; break;
        case 24: out->created_ms=v[0]; break;
        case 25: for (int j=0;j<2;j++) out->image_count[j]=v[j]; break;
        }
    }
    free(copy);
    if (seen != (1u << 26)-1u || out->schema != 6 || out->ready > 1 ||
        out->ready != (out->pending != 0)) { memset(out, 0, sizeof(*out)); return -1; }
    return 0;
fail_inventory:
    free(copy);
    memset(out, 0, sizeof(*out));
    return -1;
}

int hk_can_getall_inventory(const HkCanCfg *cfg, int node_addr,
                            HkExpInventory *out)
{
    char text[4096];
    return getall_text(cfg, node_addr, 6, text, sizeof(text)) == 0 ?
           hk_can_parse_inventory(text, out) : -1;
}

int hk_can_parse_icd_status(const char *text, HkExpIcdStatus *out)
{
    static const struct { const char *name, *type; unsigned addr, count, bits; } rows[] = {
        {"schema", "U16", 0, 1, 16}, {"seq", "U32", 2, 1, 32},
        {"tick_ms", "U32", 6, 1, 32}, {"boot_id", "U32", 10, 1, 32},
        {"layout_ver", "U8", 14, 1, 8}, {"valid_mask", "U8", 15, 1, 8},
        {"group_tick_ms", "U32", 16, 4, 32},
        {"thm_stat", "U8", 32, 6, 8}, {"fld_stat", "U8", 38, 16, 8},
        {"rba_stat", "U8", 54, 16, 8}, {"pc_stat", "U8", 70, 1, 8},
        {"build_id", "STR", 71, 1, 0}
    };
    if (!text || !out) return -1;
    memset(out, 0, sizeof(*out));
    char *copy = strdup(text);
    if (!copy) return -1;
    uint16_t seen = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        unsigned addr;
        char name[32], type[8];
        int used = 0;
        if (sscanf(line, " 0x%x %31s %7s %n", &addr, name, type, &used) != 3 ||
            used <= 0) continue;
        unsigned i;
        for (i = 0; i < sizeof(rows)/sizeof(rows[0]); ++i)
            if (!strcmp(name, rows[i].name)) break;
        if (i == sizeof(rows)/sizeof(rows[0])) continue;
        if (addr != rows[i].addr || strcmp(type, rows[i].type) || (seen & (1u << i)))
            goto fail_status;
        if (i == 11) {
            const char *s = line + used;
            size_t n = strlen(s);
            while (n && (s[n-1] == ' ' || s[n-1] == '\r' || s[n-1] == '\t')) n--;
            if (n < 2 || n > 22 || *s != '"' || s[n-1] != '"') goto fail_status;
            for (size_t j = 1; j + 1 < n; ++j)
                if ((unsigned char)s[j] < 0x20 || s[j] == '"' || s[j] == '\\')
                    goto fail_status;
            memcpy(out->build_id, s + 1, n - 2);
        } else {
            int64_t v[16];
            if (parse_values(line + used, v, (int)rows[i].count, 0, rows[i].bits))
                goto fail_status;
            switch (i) {
            case 0: out->schema = (uint16_t)v[0]; break;
            case 1: out->seq = (uint32_t)v[0]; break;
            case 2: out->tick_ms = (uint32_t)v[0]; break;
            case 3: out->boot_id = (uint32_t)v[0]; break;
            case 4: out->layout_ver = (uint8_t)v[0]; break;
            case 5: out->valid_mask = (uint8_t)v[0]; break;
            case 6: for (int j=0;j<4;j++) out->group_tick_ms[j]=(uint32_t)v[j]; break;
            case 7: for (int j=0;j<6;j++) out->thm_stat[j]=(uint8_t)v[j]; break;
            case 8: for (int j=0;j<16;j++) out->fld_stat[j]=(uint8_t)v[j]; break;
            case 9: for (int j=0;j<16;j++) out->rba_stat[j]=(uint8_t)v[j]; break;
            case 10: out->pc_stat=(uint8_t)v[0]; break;
            }
        }
        seen |= (uint16_t)(1u << i);
    }
    free(copy);
    if (seen != 0x0fff || out->schema != 6 || out->layout_ver != 1 ||
        (out->valid_mask & ~0x1fU)) goto invalid_status;
    return 0;
fail_status:
    free(copy);
invalid_status:
    memset(out, 0, sizeof(*out));
    return -1;
}

int hk_can_getall_icd_status(const HkCanCfg *cfg, int node_addr,
                             HkExpIcdStatus *out)
{
    char text[4096];
    return getall_text(cfg, node_addr, 12, text, sizeof(text)) == 0 ?
           hk_can_parse_icd_status(text, out) : -1;
}

static int parse_identity_string(const char *text, char *out, size_t cap)
{
    size_t len = strlen(text);
    while (len && (text[len-1] == ' ' || text[len-1] == '\t' || text[len-1] == '\r')) len--;
    if (len < 3 || len > cap + 1 || text[0] != '"' || text[len-1] != '"') return -1;
    for (size_t i = 1; i + 1 < len; i++)
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e ||
            text[i] == '"' || text[i] == '\\') return -1;
    memcpy(out, text + 1, len - 2);
    return 0;
}

int hk_can_parse_identity(const char *text, HkExpIdentity *out)
{
    static const struct { const char *name, *type; unsigned addr; } rows[] = {
        {"schema", "U16", 0}, {"board", "STR", 14},
        {"firmware", "STR", 30}, {"hardware", "STR", 46},
        {"boot_id", "U32", 103}
    };
    if (!text || !out) return -1;
    memset(out, 0, sizeof(*out));
    char *copy = strdup(text);
    if (!copy) return -1;
    unsigned seen = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        unsigned addr;
        char name[32], type[8];
        int used = 0;
        if (sscanf(line, " 0x%x %31s %7s %n", &addr, name, type, &used) != 3 ||
            used <= 0) continue;
        unsigned i;
        for (i = 0; i < sizeof(rows)/sizeof(rows[0]); ++i)
            if (!strcmp(name, rows[i].name)) break;
        if (i == sizeof(rows)/sizeof(rows[0])) continue;
        if (addr != rows[i].addr || strcmp(type, rows[i].type) || (seen & (1U << i)))
            goto fail_identity;
        if (i == 1) {
            if (parse_identity_string(line + used, out->board, sizeof(out->board)))
                goto fail_identity;
        } else if (i == 2) {
            if (parse_identity_string(line + used, out->firmware, sizeof(out->firmware)))
                goto fail_identity;
        } else if (i == 3) {
            if (parse_identity_string(line + used, out->hardware, sizeof(out->hardware)))
                goto fail_identity;
        } else {
            int64_t value;
            if (parse_values(line + used, &value, 1, 0, i == 0 ? 16 : 32))
                goto fail_identity;
            if (i == 0) out->schema = (uint16_t)value;
            else out->boot_id = (uint32_t)value;
        }
        seen |= 1U << i;
    }
    free(copy);
    if (seen != 0x1f || out->schema != 6 || strcmp(out->board, "DAL_EXP") ||
        !out->firmware[0] || !out->hardware[0]) goto invalid_identity;
    return 0;
fail_identity:
    free(copy);
invalid_identity:
    memset(out, 0, sizeof(*out));
    return -1;
}

int hk_can_getall_identity(const HkCanCfg *cfg, int node_addr,
                           HkExpIdentity *out)
{
    char text[8192];
    return getall_text(cfg, node_addr, 0, text, sizeof(text)) == 0 ?
           hk_can_parse_identity(text, out) : -1;
}

int hk_can_run(const HkCanCfg *cfg, int node_addr, const char *command,
               char *out_buf, size_t out_sz)
{
    char node_str[16];
    snprintf(node_str, sizeof(node_str), "%d", node_addr);
    char *argv[7];
    unsigned n = client_args(cfg, argv);
    argv[n++] = "run";
    argv[n++] = node_str;
    argv[n++] = (char *)command;
    argv[n] = NULL;

    int exit_code = -1;
    if (run_capture(argv, cfg->rgosh_timeout_ms, out_buf, out_sz, &exit_code) < 0)
        return -1;

    return (exit_code == 0) ? 0 : -1;
}

int hk_can_rparam(const HkCanCfg *cfg, int node_addr, int table_id,
                  const char *verb, const char *field, const char *value,
                  char *out_buf, size_t out_sz)
{
    if (!cfg || !verb || !field || !out_buf || !out_sz ||
        (strcmp(verb, "getval") && strcmp(verb, "setval")) ||
        (strcmp(verb, "setval") == 0 && !value)) return -1;
    char node_str[16], table_str[16];
    snprintf(node_str, sizeof(node_str), "%d", node_addr);
    snprintf(table_str, sizeof(table_str), "%d", table_id);
    char values[128] = {0};
    char *argv[14];
    unsigned argc = client_args(cfg, argv);
    argv[argc++] = (char *)verb;
    argv[argc++] = node_str;
    argv[argc++] = table_str;
    argv[argc++] = (char *)field;
    argv[argc] = NULL;
    if (value) {
        if (strlen(value) >= sizeof(values)) return -1;
        strcpy(values, value);
        char *save = NULL;
        for (char *part = strtok_r(values, " ", &save); part;
             part = strtok_r(NULL, " ", &save)) {
            if (argc >= 13) return -1;
            argv[argc++] = part;
        }
        if ((cfg->address_option[0] && argc == 7) ||
            (!cfg->address_option[0] && argc == 6)) return -1;
        argv[argc] = NULL;
    }
    int exit_code = -1;
    if (run_capture(argv, cfg->getall_timeout_ms + 3000,
                    out_buf, out_sz, &exit_code) < 0) return -1;
    return exit_code == 0 ? 0 : -1;
}
