#include "services/exp_csp_service.h"
#include "services/exp_data_service.h"
#include "services/exp_csp_can.h"
#include "services/exp_csp_uart.h"
#include "hk_csv.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_SIZE 8U
#define RESULT_SIZE 16U
#define RESULT_TEXT 1024U
#define DEFAULT_SNAPSHOT "/run/dal/dal_obc_fsw/exp_data.json"
#define EXP_CSV_HEADER \
    "obc_timestamp,online,data_valid,sensor_seq,supported_mask,valid_mask," \
    "ntc1_c,ntc2_c,ntc3_c,ntc4_c,board_temp_c,bme_temp_c,bme_press_pa,bme_hum_rh," \
    "volt_12v_motor_mv,volt_12v_exp_mv,volt_5v_exp_mv,volt_5v_sln_hd_mv," \
    "curr_12v_motor_ma,curr_12v_exp_ma,curr_5v_exp_ma,curr_5v_sln_hd_ma," \
    "power_12v_motor_mw,power_12v_exp_mw,power_5v_exp_mw,power_5v_sln_hd_mw"

typedef struct {
    uint32_t ticket;
    exp_csp_op_t op;
    unsigned table;
    char field[96];
    char value[128];
} request_t;

typedef struct {
    uint32_t ticket;
    exp_csp_state_t state;
    char output[RESULT_TEXT];
} result_t;

static pthread_t worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool running;
static request_t queue[QUEUE_SIZE];
static unsigned queue_head, queue_count;
static result_t results[RESULT_SIZE];
static uint32_t next_ticket;
static HkCanCfg cfg;
static HkCanCfg cfg_can, cfg_rs485;
static bool rs485_enabled, on_rs485, on_uart;
static unsigned can_failures;
static time_t next_rs485_probe, next_can_probe, next_uart_probe;
static int node_addr;
static char snapshot_path[512];
static char sensor_cache[4096], inventory_cache[1024];
static char icd_cache[1024], thermal_cache[2048];
static struct timespec thermal_received;
static bool thermal_poll_ok;
static uint32_t thermal_seq, thermal_tick;
static struct timespec sensor_received, inventory_received;
static bool sensor_poll_ok, inventory_poll_ok;
static HkExpInventory inventory_state;
static struct timespec icd_received;
static bool icd_poll_ok;
static HkExpIcdStatus icd_status;
static HkExpIdentity identity;
static char identity_cache[256];
static struct timespec identity_received;
static bool identity_valid, identity_refresh_needed;
static time_t identity_retry_at;
static uint8_t rail_cache[3];

static void cache_snapshot(unsigned table, const char *json)
{
    pthread_mutex_lock(&lock);
    char *dst = table == 2 ? sensor_cache : inventory_cache;
    size_t cap = table == 2 ? sizeof(sensor_cache) : sizeof(inventory_cache);
    bool *ok = table == 2 ? &sensor_poll_ok : &inventory_poll_ok;
    struct timespec *received = table == 2 ? &sensor_received : &inventory_received;
    *ok = json != NULL;
    if (json) {
        snprintf(dst, cap, "%s", json);
        clock_gettime(CLOCK_MONOTONIC, received);
    }
    pthread_mutex_unlock(&lock);
}

static const char *setting(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value && *value ? value : fallback;
}

static void append(char *out, size_t cap, size_t *used, const char *fmt, ...)
{
    if (*used >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *used, cap - *used, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= cap - *used) *used = cap;
    else *used += (size_t)n;
}

static void publish(const HkExpSensors *data)
{
    pthread_mutex_lock(&lock);
    if (data) memcpy(rail_cache, data->rail_enabled, sizeof(rail_cache));
    pthread_mutex_unlock(&lock);
    char json[4096];
    size_t used = 0;
    time_t now = time(NULL);
    append(json, sizeof(json), &used,
           "{\"board\":\"exp\",\"schema_version\":1,\"node_addr\":%d,"
           "\"online\":%d,\"data_valid\":%d,\"obc_timestamp\":%ld",
           node_addr, data ? 1 : 0, data ? 1 : 0, (long)now);
    if (data) {
        append(json, sizeof(json), &used,
               ",\"rparam_schema\":%u,\"sensor_seq\":%u,\"sensor_tick_ms\":%u,"
               "\"sensor_supported_mask\":%u,\"sensor_valid_mask\":%u,\"sensor_status\":[",
               data->schema, data->seq, data->tick_ms, data->supported, data->valid);
        for (unsigned i = 0; i < HK_EXP_GROUP_COUNT; i++)
            append(json, sizeof(json), &used, "%s%u", i ? "," : "", data->status[i]);
        append(json, sizeof(json), &used, "],\"sensor_age_ms\":[");
        for (unsigned i = 0; i < HK_EXP_GROUP_COUNT; i++)
            append(json, sizeof(json), &used, "%s%u", i ? "," : "", data->age_ms[i]);
        append(json, sizeof(json), &used, "],\"sensor_sample_seq\":[");
        for (unsigned i = 0; i < HK_EXP_GROUP_COUNT; i++)
            append(json, sizeof(json), &used, "%s%u", i ? "," : "", data->sample_seq[i]);
        append(json, sizeof(json), &used, "],\"sensor_last_error\":[");
        for (unsigned i = 0; i < HK_EXP_GROUP_COUNT; i++)
            append(json, sizeof(json), &used, "%s%u", i ? "," : "", data->last_error[i]);
        append(json, sizeof(json), &used, "],\"sensor_desired\":[");
        for (unsigned i = 0; i < HK_EXP_GROUP_COUNT; i++)
            append(json, sizeof(json), &used, "%s%u", i ? "," : "", data->desired[i]);
        append(json, sizeof(json), &used, "],\"sensor_applied\":[");
        for (unsigned i = 0; i < HK_EXP_GROUP_COUNT; i++)
            append(json, sizeof(json), &used, "%s%u", i ? "," : "", data->applied[i]);
        append(json, sizeof(json), &used,
               "],\"ntc_c\":[%.3f,%.3f,%.3f,%.3f],\"board_temp_c\":%.3f,"
               "\"bme_temp_c\":%.3f,\"bme_press_pa\":%u,\"bme_hum_rh\":%.3f,"
               "\"bme_gas_ohm\":%u,\"bme_gas_valid\":%u,\"imu_mask\":%u,"
               "\"imu_accel_mg\":[%d,%d,%d],\"imu_gyro_dps\":[%.3f,%.3f,%.3f],"
               "\"imu_temp_c\":%.3f,\"flow_ul_min\":[%.2f,%.2f],"
               "\"flow_temp_c\":[%.3f,%.3f],"
               "\"pwr_rail_names\":[\"12V_MOTOR\",\"12V_EXP\",\"5V_EXP\",\"5V_SLN_HD\"],"
               "\"pwr_volt_mv\":[%u,%u,%u,%u],\"pwr_curr_ma\":[%d,%d,%d,%d],"
               "\"pwr_mw\":[%d,%d,%d,%d],\"rail_enabled\":[%u,%u,%u]",
               data->ntc_c[0], data->ntc_c[1], data->ntc_c[2], data->ntc_c[3],
               data->board_temp_c, data->bme_temp_c, data->bme_press_pa,
               data->bme_hum_rh, data->bme_gas_ohm, data->bme_gas_valid,
               data->imu_mask, data->accel_mg[0], data->accel_mg[1], data->accel_mg[2],
               data->gyro_dps[0], data->gyro_dps[1], data->gyro_dps[2],
               data->imu_temp_c, data->flow_ul_min[0], data->flow_ul_min[1],
               data->flow_temp_c[0], data->flow_temp_c[1],
               data->pwr_volt_mv[0], data->pwr_volt_mv[1],
               data->pwr_volt_mv[2], data->pwr_volt_mv[3],
               data->pwr_curr_ma[0], data->pwr_curr_ma[1],
               data->pwr_curr_ma[2], data->pwr_curr_ma[3],
               data->pwr_mw[0], data->pwr_mw[1], data->pwr_mw[2], data->pwr_mw[3],
               data->rail_enabled[0], data->rail_enabled[1], data->rail_enabled[2]);
    }
    append(json, sizeof(json), &used, "}\n");
    if (used >= sizeof(json)) return;
    cache_snapshot(2, data ? json : NULL);

    char tmp[sizeof(snapshot_path) + 16];
    if (snprintf(tmp, sizeof(tmp), "%s.XXXXXX", snapshot_path) >= (int)sizeof(tmp)) return;
    int fd = mkstemp(tmp);
    if (fd < 0) return;
    fchmod(fd, 0644);
    size_t written = 0;
    while (written < used) {
        ssize_t n = write(fd, json + written, used - written);
        if (n > 0) { written += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    int ok = written == used && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(tmp, snapshot_path) != 0) unlink(tmp);
}

int exp_csp_rail_state(uint16_t *known, uint16_t *on)
{
    if (!known || !on) return -1;
    *known = *on = 0;
    pthread_mutex_lock(&lock);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    bool fresh = sensor_poll_ok && sensor_received.tv_sec &&
        now.tv_sec >= sensor_received.tv_sec &&
        now.tv_sec - sensor_received.tv_sec <= 3;
    if (fresh) {
        *known = 7;
        for (unsigned i = 0; i < 3; ++i)
            if (rail_cache[i]) *on |= (uint16_t)(1U << i);
    }
    pthread_mutex_unlock(&lock);
    return fresh ? 0 : -1;
}

static bool icd_fresh_locked(unsigned group)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t age_ms = (int64_t)(now.tv_sec - icd_received.tv_sec) * 1000 +
        (now.tv_nsec - icd_received.tv_nsec) / 1000000;
    if (!icd_poll_ok || !icd_received.tv_sec ||
        age_ms < 0 || age_ms > 3000)
        return false;
    return group == 4 ? (icd_status.valid_mask & 0x10U) != 0 :
        group < 4 && (icd_status.valid_mask & (1U << group)) &&
        (uint32_t)(icd_status.tick_ms - icd_status.group_tick_ms[group]) <= 3000U;
}

int exp_csp_icd_status(unsigned group, uint8_t *output, size_t output_size)
{
    static const size_t sizes[] = {6, 16, 16, 1};
    if (group >= 4 || !output || output_size < sizes[group]) return -1;
    pthread_mutex_lock(&lock);
    bool fresh = icd_fresh_locked(group);
    if (fresh) {
        const uint8_t *data = group == 0 ? icd_status.thm_stat :
            group == 1 ? icd_status.fld_stat :
            group == 2 ? icd_status.rba_stat : &icd_status.pc_stat;
        memcpy(output, data, sizes[group]);
    }
    pthread_mutex_unlock(&lock);
    return fresh ? 0 : -1;
}

static int parse_version(const char *text, uint8_t result[3])
{
    unsigned a, b, c = 0;
    char tail;
    if (sscanf(text, "%u.%u.%u%c", &a, &b, &c, &tail) != 3 &&
        sscanf(text, "%u.%u%c", &a, &b, &tail) != 2)
        return -1;
    if (a > 255 || b > 255 || c > 255) return -1;
    result[0] = (uint8_t)a;
    result[1] = (uint8_t)b;
    result[2] = (uint8_t)c;
    return 0;
}

int exp_csp_identity(uint8_t firmware[3], uint8_t hardware[3],
                     char *build_id, size_t build_size)
{
    if (!firmware || !hardware || !build_id || build_size < 21) return -1;
    pthread_mutex_lock(&lock);
    bool available = identity_valid && icd_fresh_locked(4) &&
        identity.boot_id == icd_status.boot_id &&
        parse_version(identity.firmware, firmware) == 0 &&
        parse_version(identity.hardware, hardware) == 0;
    if (available) memcpy(build_id, icd_status.build_id, 21);
    pthread_mutex_unlock(&lock);
    return available ? 0 : -1;
}

static bool publish_identity(const HkExpIdentity *data)
{
    char json[256];
    int n = data ? snprintf(json, sizeof(json),
        "{\"schema\":%u,\"boot_id\":%u,\"board\":\"%s\","
        "\"firmware\":\"%s\",\"hardware\":\"%s\"}",
        data->schema, data->boot_id, data->board, data->firmware,
        data->hardware) : -1;
    pthread_mutex_lock(&lock);
    bool valid = data && n > 0 && (size_t)n < sizeof(json) &&
        (!icd_poll_ok || !icd_received.tv_sec || data->boot_id == icd_status.boot_id);
    identity_valid = valid;
    if (valid) {
        identity = *data;
        strcpy(identity_cache, json);
        clock_gettime(CLOCK_MONOTONIC, &identity_received);
    }
    identity_refresh_needed = !valid;
    identity_retry_at = valid ? 0 : time(NULL) + 5;
    pthread_mutex_unlock(&lock);
    return valid;
}

static void publish_icd(const HkExpIcdStatus *d)
{
    char json[1024] = {0};
    size_t used = 0;
    if (d) {
        append(json, sizeof(json), &used,
               "{\"schema\":%u,\"seq\":%u,\"tick_ms\":%u,\"boot_id\":%u,"
               "\"layout_ver\":%u,\"valid_mask\":%u,\"group_tick_ms\":[%u,%u,%u,%u],"
               "\"thm_stat\":[", d->schema, d->seq, d->tick_ms, d->boot_id,
               d->layout_ver, d->valid_mask, d->group_tick_ms[0],
               d->group_tick_ms[1], d->group_tick_ms[2], d->group_tick_ms[3]);
        for (unsigned i=0;i<6;i++) append(json,sizeof(json),&used,"%s%u",i?",":"",d->thm_stat[i]);
        append(json,sizeof(json),&used,"],\"fld_stat\":[");
        for (unsigned i=0;i<16;i++) append(json,sizeof(json),&used,"%s%u",i?",":"",d->fld_stat[i]);
        append(json,sizeof(json),&used,"],\"rba_stat\":[");
        for (unsigned i=0;i<16;i++) append(json,sizeof(json),&used,"%s%u",i?",":"",d->rba_stat[i]);
        append(json,sizeof(json),&used,"],\"pc_stat\":%u,\"build_id\":\"%s\"}",
               d->pc_stat,d->build_id);
    }
    pthread_mutex_lock(&lock);
    icd_poll_ok = d != NULL && used < sizeof(json);
    if (icd_poll_ok) {
        if (icd_status.boot_id != d->boot_id || icd_status.seq != d->seq)
            clock_gettime(CLOCK_MONOTONIC, &icd_received);
        icd_status = *d;
        strcpy(icd_cache, json);
        if (identity_valid && identity.boot_id != d->boot_id) {
            identity_valid = false;
            identity_refresh_needed = true;
            identity_retry_at = 0;
        }
    }
    pthread_mutex_unlock(&lock);
}

static void publish_thermal(const HkExpThermal *d)
{
    char json[2048] = {0};
    size_t used = 0;
    if (d) {
        append(json, sizeof(json), &used,
               "{\"schema\":%u,\"seq\":%u,\"tick_ms\":%u",
               d->schema, d->seq, d->tick_ms);
#define ARRAY4(field, format) do { \
    append(json, sizeof(json), &used, ",\"" #field "\":["); \
    for (unsigned i=0;i<4;i++) \
        append(json, sizeof(json), &used, "%s" format, i ? "," : "", d->field[i]); \
    append(json, sizeof(json), &used, "]"); \
} while (0)
        ARRAY4(state, "%u"); ARRAY4(enabled, "%u");
        ARRAY4(fault, "%u"); ARRAY4(configured, "%u");
        ARRAY4(sensors_valid, "%u"); ARRAY4(main_ntc, "%u");
        ARRAY4(sec_ntc, "%u"); ARRAY4(main_mC, "%d");
        ARRAY4(sec_mC, "%d"); ARRAY4(setpoint_mC, "%d");
        ARRAY4(request_pct, "%u"); ARRAY4(applied_pct, "%u");
        ARRAY4(limit_pct, "%u"); ARRAY4(main_age_ms, "%u");
        ARRAY4(sec_age_ms, "%u"); ARRAY4(owner_kind, "%u");
        ARRAY4(owner_profile, "%u"); ARRAY4(owner_valid, "%u");
        ARRAY4(pid_demand, "%.3f"); ARRAY4(output_ms, "%u");
        ARRAY4(config_gen, "%u");
#undef ARRAY4
        append(json, sizeof(json), &used, "}");
    }
    pthread_mutex_lock(&lock);
    thermal_poll_ok = d != NULL && used < sizeof(json);
    if (thermal_poll_ok) {
        if (thermal_seq != d->seq || thermal_tick != d->tick_ms)
            clock_gettime(CLOCK_MONOTONIC, &thermal_received);
        thermal_seq = d->seq;
        thermal_tick = d->tick_ms;
        strcpy(thermal_cache, json);
    }
    pthread_mutex_unlock(&lock);
}

static void publish_inventory(const HkExpInventory *d)
{
    if (!d) { cache_snapshot(6, NULL); return; }
    char json[1024];
    int n = snprintf(json, sizeof(json),
        "{\"schema\":%u,\"seq\":%u,\"tick_ms\":%u,\"boot_id\":%u,"
        "\"ready\":%u,\"result\":%u,\"pending\":%u,\"published\":%u,"
        "\"acknowledged\":%u,\"discarded\":%u,\"dropped\":%u,"
        "\"type_count\":[%u,%u,%u],\"capacity\":%u,\"max_read\":%u,"
        "\"object_id\":%u,\"length\":%u,\"crc32\":%u,\"type\":%u,"
        "\"source\":%u,\"encoding\":%u,\"width\":%u,\"height\":%u,"
        "\"run_id\":%u,\"object_seq\":%u,\"created_ms\":%u,"
        "\"image_count\":[%u,%u]}",
        d->schema,d->seq,d->tick_ms,d->boot_id,d->ready,d->result,
        d->pending,d->published,d->acknowledged,d->discarded,d->dropped,
        d->type_count[0],d->type_count[1],d->type_count[2],d->capacity,d->max_read,
        d->object_id,d->length,d->crc32,d->type,d->source,d->encoding,
        d->width,d->height,d->run_id,d->object_seq,d->created_ms,
        d->image_count[0],d->image_count[1]);
    pthread_mutex_lock(&lock);
    inventory_poll_ok = n > 0 && (size_t)n < sizeof(json);
    if (inventory_poll_ok) {
        snprintf(inventory_cache, sizeof(inventory_cache), "%s", json);
        inventory_state = *d;
        clock_gettime(CLOCK_MONOTONIC, &inventory_received);
    } else inventory_cache[0] = '\0';
    pthread_mutex_unlock(&lock);
}

int exp_csp_inventory(HkExpInventory *out)
{
    if (!out) return -1;
    pthread_mutex_lock(&lock);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t age = (int64_t)(now.tv_sec - inventory_received.tv_sec) * 1000000000LL +
        (now.tv_nsec - inventory_received.tv_nsec);
    int ok = inventory_poll_ok && inventory_cache[0] && age >= 0 && age <= 3000000000LL;
    if (ok) *out = inventory_state;
    pthread_mutex_unlock(&lock);
    return ok ? 0 : -1;
}

int exp_csp_snapshot_json(unsigned table, char *output, size_t output_size)
{
    if ((table != 0 && table != 2 && table != 3 && table != 6 && table != 12) || !output || !output_size) return -1;
    pthread_mutex_lock(&lock);
    const char *data = table == 0 ? identity_cache : table == 2 ? sensor_cache : table == 3 ? thermal_cache :
        table == 6 ? inventory_cache : icd_cache;
    const struct timespec *received = table == 0 ? &identity_received :
        table == 2 ? &sensor_received : table == 3 ? &thermal_received :
        table == 6 ? &inventory_received : &icd_received;
    bool ok = table == 0 ? identity_valid && icd_fresh_locked(4) &&
        identity.boot_id == icd_status.boot_id :
        table == 2 ? sensor_poll_ok : table == 3 ? thermal_poll_ok :
        table == 6 ? inventory_poll_ok : icd_poll_ok;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t elapsed = (int64_t)(now.tv_sec - received->tv_sec) * 1000000000LL +
        (now.tv_nsec - received->tv_nsec);
    uint64_t age = elapsed < 0 ? UINT64_MAX : (uint64_t)elapsed / 1000000U;
    int n;
    if (!data[0])
        n = snprintf(output, output_size,
                     "{\"table\":%u,\"fresh\":false,\"age_ms\":null,\"data\":null}", table);
    else
        n = snprintf(output, output_size,
                     "{\"table\":%u,\"fresh\":%s,\"age_ms\":%llu,\"data\":%s}",
                     table, ok && (table == 0 || age <= 3000) ? "true" : "false",
                     (unsigned long long)age, data);
    pthread_mutex_unlock(&lock);
    if (table == 6 && n > 0 && (size_t)n < output_size) {
        char saved[256];
        if (exp_data_last_saved_json(saved, sizeof(saved)) != 0) return -1;
        n += snprintf(output + n - 1, output_size - (size_t)n + 1,
                      ",\"download\":%s}", saved) - 1;
    }
    return n >= 0 && (size_t)n < output_size ? 0 : -1;
}

static void append_csv(HkCsv *csv, const HkExpSensors *data, time_t now)
{
    char row[512];
    if (!data) {
        int n = snprintf(row, sizeof(row), "0,0");
        for (int i = 0; i < 23 && n > 0 && (size_t)n < sizeof(row); i++)
            n += snprintf(row + n, sizeof(row) - (size_t)n, ",");
    } else {
        snprintf(row, sizeof(row),
            "1,1,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%.3f,"
            "%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d",
            data->seq, data->supported, data->valid,
            data->ntc_c[0], data->ntc_c[1], data->ntc_c[2], data->ntc_c[3],
            data->board_temp_c, data->bme_temp_c, data->bme_press_pa,
            data->bme_hum_rh, data->pwr_volt_mv[0], data->pwr_volt_mv[1],
            data->pwr_volt_mv[2], data->pwr_volt_mv[3],
            data->pwr_curr_ma[0], data->pwr_curr_ma[1],
            data->pwr_curr_ma[2], data->pwr_curr_ma[3],
            data->pwr_mw[0], data->pwr_mw[1], data->pwr_mw[2], data->pwr_mw[3]);
    }
    hk_csv_append(csv, EXP_CSV_HEADER, now, row);
}

static void process_request(const request_t *req)
{
    char output[RESULT_TEXT] = {0};
    int rc;
    if (on_uart && req->op != EXP_CSP_REFRESH) {
        rc = -1;
        snprintf(output, sizeof(output), "CSP unavailable; UART param_show fallback is read-only");
    } else
    if (req->op == EXP_CSP_REFRESH) {
        HkExpIdentity fresh;
        rc = on_uart ? exp_uart_getall_identity(&fresh) :
                       hk_can_getall_identity(&cfg, node_addr, &fresh);
        if (rc == 0 && !publish_identity(&fresh)) rc = -1;
        if (rc != 0) publish_identity(NULL);
        snprintf(output, sizeof(output), "%s", rc == 0 ?
                 "EXP identity refreshed from RPARAM table 0" :
                 "EXP identity refresh failed");
    } else if (req->op == EXP_CSP_RUN)
        rc = hk_can_run(&cfg, node_addr, req->field, output, sizeof(output));
    else
        rc = hk_can_rparam(&cfg, node_addr, (int)req->table,
                            req->op == EXP_CSP_SET ? "setval" : "getval",
                            req->field, req->op == EXP_CSP_SET ? req->value : NULL,
                            output, sizeof(output));
    pthread_mutex_lock(&lock);
    result_t *result = &results[req->ticket % RESULT_SIZE];
    if (result->ticket == req->ticket) {
        result->state = rc == 0 ? EXP_CSP_DONE : EXP_CSP_FAILED;
        snprintf(result->output, sizeof(result->output), "%s", output);
    }
    pthread_mutex_unlock(&lock);
}

static void *thread_main(void *unused)
{
    (void)unused;
    HkCsv csv;
    char csv_dir[512];
    if (snprintf(csv_dir, sizeof(csv_dir), "%s/logs/hk",
                 setting("DAL_DATA_DIR", "/home/bee/data")) >= (int)sizeof(csv_dir))
        return NULL;
    hk_csv_init(&csv, csv_dir, "HK1", 60);
    struct timespec next_poll = {0};
    time_t last_csv = 0;
    while (true) {
        request_t req;
        bool have_request = false;
        pthread_mutex_lock(&lock);
        for (;;) {
            if (!running) { pthread_mutex_unlock(&lock); hk_csv_close(&csv); return NULL; }
            if (queue_count) {
                req = queue[queue_head];
                queue_head = (queue_head + 1U) % QUEUE_SIZE;
                queue_count--;
                results[req.ticket % RESULT_SIZE].state = EXP_CSP_RUNNING;
                have_request = true;
                break;
            }
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > next_poll.tv_sec ||
                (now.tv_sec == next_poll.tv_sec && now.tv_nsec >= next_poll.tv_nsec)) break;
            pthread_cond_timedwait(&wake, &lock, &next_poll);
        }
        pthread_mutex_unlock(&lock);
        if (have_request) { process_request(&req); continue; }
        clock_gettime(CLOCK_REALTIME, &next_poll);
        next_poll.tv_sec += 1;
        time_t cycle_time = time(NULL);
        if ((on_rs485 || on_uart) && cycle_time >= next_can_probe) {
            HkExpSensors probe;
            next_can_probe = cycle_time + 10;
            if (hk_can_ping(&cfg_can, node_addr) == 1 &&
                hk_can_getall_sensors(&cfg_can, node_addr, &probe) == 0) {
                cfg = cfg_can;
                on_rs485 = on_uart = false;
                can_failures = 0;
                fprintf(stderr, "[CSP] CAN recovered; using %s\n", cfg.transport_option);
            } else if (on_uart && rs485_enabled &&
                       hk_can_ping(&cfg_rs485, node_addr) == 1 &&
                       hk_can_getall_sensors(&cfg_rs485, node_addr, &probe) == 0) {
                cfg = cfg_rs485;
                on_rs485 = true;
                on_uart = false;
                fprintf(stderr, "[CSP] RS485 recovered; using %s\n", cfg.transport_option);
            }
        }
        HkExpSensors sensors;
        HkExpThermal thermal;
        HkExpInventory inventory;
        HkExpIcdStatus icd;
        bool online = on_uart ? false : hk_can_ping(&cfg, node_addr) == 1;
        bool sensors_valid = on_uart ? exp_uart_getall_sensors(&sensors) == 0 :
            online && hk_can_getall_sensors(&cfg, node_addr, &sensors) == 0;
        if (!on_rs485 && !on_uart) {
            if (sensors_valid) can_failures = 0;
            else if (can_failures < 2) can_failures++;
            if (can_failures >= 2 && rs485_enabled && cycle_time >= next_rs485_probe) {
                next_rs485_probe = cycle_time + 5;
                HkExpSensors probe;
                if (hk_can_ping(&cfg_rs485, node_addr) == 1 &&
                    hk_can_getall_sensors(&cfg_rs485, node_addr, &probe) == 0) {
                    cfg = cfg_rs485;
                    on_rs485 = true;
                    on_uart = false;
                    online = true;
                    sensors = probe;
                    sensors_valid = true;
                    next_can_probe = cycle_time + 10;
                    fprintf(stderr, "[CSP] CAN unavailable; using RS485 KISS %s\n",
                            cfg.transport_option);
                }
            }
        } else if (on_rs485 && !sensors_valid) {
            next_can_probe = 0;
        }
        if (!sensors_valid && !on_uart && cycle_time >= next_uart_probe &&
            (on_rs485 || can_failures >= 2)) {
            next_uart_probe = cycle_time + 5;
            if (exp_uart_getall_sensors(&sensors) == 0) {
                on_uart = true;
                on_rs485 = false;
                sensors_valid = true;
                next_can_probe = cycle_time + 10;
                fprintf(stderr, "[CSP] CAN/RS485 unavailable; using UART param_show\n");
            }
        }
        online = sensors_valid;
        pthread_mutex_lock(&lock);
        bool refresh_identity = identity_refresh_needed &&
            time(NULL) >= identity_retry_at;
        pthread_mutex_unlock(&lock);
        if (online && refresh_identity) {
            HkExpIdentity fresh;
            if ((on_uart ? exp_uart_getall_identity(&fresh) :
                           hk_can_getall_identity(&cfg, node_addr, &fresh)) == 0)
                publish_identity(&fresh);
            else publish_identity(NULL);
        }
        bool thermal_valid = online && (on_uart ?
            exp_uart_getall_thermal(&thermal) == 0 :
            hk_can_getall_thermal(&cfg, node_addr, &thermal) == 0);
        bool inventory_valid = online && (on_uart ?
            exp_uart_getall_inventory(&inventory) == 0 :
            hk_can_getall_inventory(&cfg, node_addr, &inventory) == 0);
        bool icd_valid = online && (on_uart ?
            exp_uart_getall_icd_status(&icd) == 0 :
            hk_can_getall_icd_status(&cfg, node_addr, &icd) == 0);
        publish(sensors_valid ? &sensors : NULL);
        publish_thermal(thermal_valid ? &thermal : NULL);
        publish_inventory(inventory_valid ? &inventory : NULL);
        publish_icd(icd_valid ? &icd : NULL);
        time_t wall = time(NULL);
        if (wall - last_csv >= 5) {
            append_csv(&csv, sensors_valid ? &sensors : NULL, wall);
            last_csv = wall;
        }
        struct timespec finished;
        clock_gettime(CLOCK_REALTIME, &finished);
        if (finished.tv_sec > next_poll.tv_sec ||
            (finished.tv_sec == next_poll.tv_sec && finished.tv_nsec >= next_poll.tv_nsec)) {
            next_poll = finished;
            next_poll.tv_sec += 1;
        }
    }
}

int exp_csp_service_start(void)
{
    pthread_mutex_lock(&lock);
    if (running) { pthread_mutex_unlock(&lock); return -1; }
    if (!strcmp(setting("DAL_EXP_CSP_ENABLE", "1"), "0")) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    const char *path = setting("DAL_EXP_HK_SNAPSHOT_PATH", DEFAULT_SNAPSHOT);
    if (strlen(path) >= sizeof(snapshot_path)) { pthread_mutex_unlock(&lock); return -1; }
    strcpy(snapshot_path, path);
    cfg_can = (HkCanCfg){
        .lite_client_path = setting("DAL_EXP_CSP_CLIENT", "/home/root/tools/bin/lite-client"),
        .ping_timeout_ms = 1000,
        .getall_timeout_ms = 10000,
        .rgosh_timeout_ms = 13000,
        .table_id = 2
    };
    const char *can_if = setting("DAL_EXP_CSP_IF", "can0");
    if (strchr(can_if, ',') || snprintf(cfg_can.transport_option,
        sizeof(cfg_can.transport_option), "-c=%s", can_if) >=
        (int)sizeof(cfg_can.transport_option)) { pthread_mutex_unlock(&lock); return -1; }
    cfg_rs485 = cfg_can;
    const char *rs485_port = setting("DAL_EXP_CSP_RS485_PORT", "/dev/ttyS2");
    const char *baud_text = setting("DAL_EXP_CSP_RS485_BAUD", "115200");
    const char *addr_text = setting("DAL_EXP_CSP_RS485_ADDRESS", "2");
    char *baud_end, *addr_end;
    long baud = strtol(baud_text, &baud_end, 10);
    long address = strtol(addr_text, &addr_end, 10);
    if (*baud_end || baud < 9600 || baud > 1000000 || *addr_end ||
        address < 1 || address > 30 || strchr(rs485_port, ',') ||
        snprintf(cfg_rs485.transport_option, sizeof(cfg_rs485.transport_option),
                 "-k%s,speed=%ld", rs485_port, baud) >=
                 (int)sizeof(cfg_rs485.transport_option) ||
        snprintf(cfg_rs485.address_option, sizeof(cfg_rs485.address_option),
                 "-a%ld", address) >= (int)sizeof(cfg_rs485.address_option)) {
        pthread_mutex_unlock(&lock); return -1;
    }
    rs485_enabled = strcmp(setting("DAL_EXP_CSP_RS485_ENABLE", "1"), "0") != 0;
    if (rs485_enabled &&
        strcmp(rs485_port, setting("DAL_GUI_PDU_PORT", "/dev/ttyS3")) == 0) {
        fprintf(stderr, "[CSP] RS485 fallback disabled: %s is assigned to PDU Modbus\n",
                rs485_port);
        rs485_enabled = false;
    }
    cfg = cfg_can;
    on_rs485 = false;
    on_uart = false;
    can_failures = 0;
    next_rs485_probe = next_can_probe = next_uart_probe = 0;
    const char *node = setting("DAL_EXP_CSP_NODE", "11");
    char *end;
    long parsed = strtol(node, &end, 10);
    if (*end || parsed < 1 || parsed > 31 || parsed == address) {
        pthread_mutex_unlock(&lock); return -1;
    }
    node_addr = (int)parsed;
    queue_head = queue_count = 0;
    memset(results, 0, sizeof(results));
    sensor_cache[0] = thermal_cache[0] = inventory_cache[0] = icd_cache[0] = '\0';
    identity_cache[0] = '\0';
    sensor_poll_ok = thermal_poll_ok = inventory_poll_ok = icd_poll_ok = false;
    memset(&icd_status, 0, sizeof(icd_status));
    icd_received = thermal_received = (struct timespec){0};
    thermal_seq = thermal_tick = 0;
    identity_received = (struct timespec){0};
    memset(&identity, 0, sizeof(identity));
    identity_valid = false;
    identity_refresh_needed = true;
    identity_retry_at = 0;
    next_ticket = 0;
    running = true;
    int rc = pthread_create(&worker, NULL, thread_main, NULL);
    if (rc) running = false;
    pthread_mutex_unlock(&lock);
    return rc ? -1 : 0;
}

void exp_csp_service_stop(void)
{
    pthread_mutex_lock(&lock);
    bool was_running = running;
    running = false;
    sensor_poll_ok = thermal_poll_ok = inventory_poll_ok = icd_poll_ok = identity_valid = false;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&lock);
    if (was_running) pthread_join(worker, NULL);
    pthread_mutex_lock(&lock);
    sensor_poll_ok = thermal_poll_ok = inventory_poll_ok = icd_poll_ok = identity_valid = false;
    pthread_mutex_unlock(&lock);
}

static bool safe_field(const char *text, size_t max)
{
    size_t n = text ? strlen(text) : 0;
    if (!n || n >= max) return false;
    for (size_t i = 0; i < n; i++)
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= 'A' && text[i] <= 'Z') ||
              (text[i] >= '0' && text[i] <= '9') || text[i] == '_')) return false;
    return true;
}

int exp_csp_submit(exp_csp_op_t op, unsigned table, const char *field,
                   const char *value, uint32_t *ticket)
{
    if (!ticket || (op != EXP_CSP_GET && op != EXP_CSP_SET &&
                    op != EXP_CSP_RUN && op != EXP_CSP_REFRESH) ||
        (op != EXP_CSP_RUN && op != EXP_CSP_REFRESH &&
         (table > 12 || !safe_field(field, sizeof(queue[0].field)))) ||
        (op == EXP_CSP_REFRESH && (table != 0 || field || value)) ||
        (op == EXP_CSP_SET && (table != 5 || !value || !*value ||
                               strlen(value) >= sizeof(queue[0].value))) ||
        (op == EXP_CSP_RUN && (!field || !*field || strlen(field) >= sizeof(queue[0].field) ||
                               strchr(field, '\n') || strchr(field, '"') || strchr(field, '\\'))))
        return -1;
    if (op == EXP_CSP_SET)
        for (const char *p = value; *p; p++)
            if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' ||
                  *p == '.' || *p == ' ')) return -1;
    pthread_mutex_lock(&lock);
    uint32_t id = next_ticket + 1U;
    result_t *result = &results[id % RESULT_SIZE];
    if (!running || queue_count == QUEUE_SIZE ||
        (result->ticket && (result->state == EXP_CSP_QUEUED ||
                            result->state == EXP_CSP_RUNNING))) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    next_ticket = id;
    queue[(queue_head + queue_count) % QUEUE_SIZE] = (request_t){.ticket=id,.op=op,.table=table};
    request_t *req = &queue[(queue_head + queue_count) % QUEUE_SIZE];
    if (field) strcpy(req->field, field);
    if (value) strcpy(req->value, value);
    queue_count++;
    *result = (result_t){.ticket=id,.state=EXP_CSP_QUEUED};
    *ticket = id;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    return 0;
}

int exp_csp_result(uint32_t ticket, exp_csp_state_t *state,
                   char *output, size_t output_size)
{
    if (!ticket || !state || !output || !output_size) return -1;
    pthread_mutex_lock(&lock);
    result_t *result = &results[ticket % RESULT_SIZE];
    int rc = result->ticket == ticket ? 0 : -1;
    if (!rc) {
        *state = result->state;
        snprintf(output, output_size, "%s", result->output);
    }
    pthread_mutex_unlock(&lock);
    return rc;
}
