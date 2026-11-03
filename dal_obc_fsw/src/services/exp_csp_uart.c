#include "services/exp_csp_uart.h"
#include "services/exp_uart_service.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name, *type; unsigned addr, count; } Row;
#define R(n,t,a,c) {n,t,a,c}
static const Row identity_rows[] = {
    R("schema","U16",0,1), R("board","STR",14,1),
    R("firmware","STR",30,1), R("hardware","STR",46,1),
    R("boot_id","U32",103,1)
};
static const Row sensor_rows[] = {
    R("schema","U16",0,1), R("seq","U32",2,1), R("tick_ms","U32",6,1),
    R("supported","U16",10,1), R("valid","U16",12,1),
    R("status","U8",14,10), R("age_ms","U32",24,10),
    R("ntc_mC","I32",64,4), R("board_mC","I32",80,1),
    R("bme_mC","I32",84,1), R("bme_Pa","U32",88,1),
    R("bme_mRH","U32",92,1), R("accel_mg","I32",96,3),
    R("gyro_mdps","I32",108,3), R("imu_mC","I32",120,1),
    R("flow_x100","I32",124,2), R("flow_mC","I32",132,2),
    R("voltage_mV","U32",140,4), R("current_mA","I32",156,4),
    R("power_mW","I32",172,4), R("rail_enabled","U8",188,3),
    R("sample_seq","U32",191,10), R("last_error","U8",231,10),
    R("desired","U8",241,10), R("applied","U8",251,10),
    R("imu_mask","U8",261,1), R("bme_gas_ohm","U32",262,1),
    R("bme_gas_valid","U8",266,1)
};
static const Row thermal_rows[] = {
    R("schema","U16",0,1),
    R("seq","U32",2,1),
    R("tick_ms","U32",6,1),
    R("state","U8",10,4),
    R("enabled","U8",14,4),
    R("fault","U8",18,4),
    R("configured","U8",22,4),
    R("sensors_valid","U8",26,4),
    R("main_ntc","U8",30,4),
    R("sec_ntc","U8",34,4),
    R("main_mC","I32",38,4),
    R("sec_mC","I32",54,4),
    R("setpoint_mC","I32",70,4),
    R("request_pct","U8",86,4),
    R("applied_pct","U8",90,4),
    R("limit_pct","U8",94,4),
    R("main_age_ms","U32",98,4),
    R("sec_age_ms","U32",114,4),
    R("owner_kind","U8",130,4),
    R("owner_profile","U8",134,4),
    R("owner_valid","U8",138,4),
    R("pid_demand","F32",142,4),
    R("output_ms","U32",158,4),
    R("config_gen","U32",174,4),
};
static const Row inventory_rows[] = {
    R("schema","U16",0,1), R("seq","U32",2,1), R("tick_ms","U32",6,1),
    R("boot_id","U32",10,1), R("ready","U8",14,1), R("result","U8",15,1),
    R("pending","U32",16,1), R("published","U32",20,1),
    R("acknowledged","U32",24,1), R("discarded","U32",28,1),
    R("dropped","U32",32,1), R("type_count","U8",36,3),
    R("capacity","U8",39,1), R("max_read","U16",40,1),
    R("object_id","U32",42,1), R("length","U32",46,1),
    R("crc32","U32",50,1), R("type","U8",54,1),
    R("source","U8",55,1), R("encoding","U8",56,1),
    R("width","U16",57,1), R("height","U16",59,1),
    R("run_id","U32",61,1), R("object_seq","U32",65,1),
    R("created_ms","U32",69,1), R("image_count","U8",73,2)
};
static const Row icd_rows[] = {
    R("schema","U16",0,1), R("seq","U32",2,1), R("tick_ms","U32",6,1),
    R("boot_id","U32",10,1), R("layout_ver","U8",14,1),
    R("valid_mask","U8",15,1), R("group_tick_ms","U32",16,4),
    R("thm_stat","U8",32,6), R("fld_stat","U8",38,16),
    R("rba_stat","U8",54,16), R("pc_stat","U8",70,1),
    R("build_id","STR",71,1)
};
#undef R

static int append(char *out, size_t cap, size_t *used, const char *text)
{
    size_t n = strlen(text);
    if (n >= cap - *used) return -1;
    memcpy(out + *used, text, n + 1);
    *used += n;
    return 0;
}

/* Convert the MCU's one-element-per-line CLI display to lite-client rows. */
static int show_as_rows(unsigned table, const Row *rows, size_t count,
                        char *output, size_t capacity)
{
    if (count > 28 || !capacity) return -1;
    char reply[16384];
    if (exp_uart_param_show(table, reply, sizeof(reply)) != 0) return -1;
    char header[32];
    snprintf(header, sizeof(header), "=== Table %u:", table);
    if (!strstr(reply, header) || !strstr(reply, "===================")) return -1;
    char value[28][16][64] = {{{0}}};
    uint16_t seen[28] = {0};
    char *save = NULL;
    for (char *line = strtok_r(reply, "\r\n", &save); line;
         line = strtok_r(NULL, "\r\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        char *key = line;
        while (isspace((unsigned char)*key)) key++;
        char *end = eq;
        while (end > key && isspace((unsigned char)end[-1])) end--;
        *end = 0;
        unsigned index = 0;
        char *bracket = strchr(key, '[');
        if (bracket) {
            char *close = strchr(bracket, ']');
            if (!close || close[1]) return -1;
            char *index_text = bracket + 1;
            *bracket = 0;
            while (bracket > key && isspace((unsigned char)bracket[-1]))
                *--bracket = 0;
            char *number_end;
            unsigned long parsed = strtoul(index_text, &number_end, 10);
            if (number_end != close || parsed >= 16) return -1;
            index = (unsigned)parsed;
        }
        size_t row = 0;
        while (row < count && strcmp(key, rows[row].name)) row++;
        if (row == count) continue;
        if (index >= rows[row].count || (rows[row].count > 1) != (bracket != NULL) ||
            (seen[row] & (1U << index))) return -1;
        char *first = eq + 1;
        while (isspace((unsigned char)*first)) first++;
        char *last = first + strlen(first);
        while (last > first && isspace((unsigned char)last[-1])) last--;
        *last = 0;
        if (!*first || strlen(first) >= sizeof(value[0][0])) return -1;
        strcpy(value[row][index], first);
        seen[row] |= (uint16_t)(1U << index);
    }
    output[0] = 0;
    size_t used = 0;
    for (size_t row = 0; row < count; row++) {
        if (seen[row] != (uint16_t)((1U << rows[row].count) - 1U)) return -1;
        char prefix[80];
        snprintf(prefix, sizeof(prefix), "0x%04X %s %s", rows[row].addr,
                 rows[row].name, rows[row].type);
        if (append(output, capacity, &used, prefix)) return -1;
        for (unsigned i = 0; i < rows[row].count; i++) {
            if (append(output, capacity, &used, " ") ||
                append(output, capacity, &used, value[row][i])) return -1;
        }
        if (append(output, capacity, &used, "\n")) return -1;
    }
    return 0;
}

#define READ_TABLE(fn, tid, spec, parser, type) \
int fn(type *out) { \
    char text[8192]; \
    return out && show_as_rows(tid, spec, sizeof(spec)/sizeof(spec[0]), \
                               text, sizeof(text)) == 0 ? parser(text, out) : -1; \
}
READ_TABLE(exp_uart_getall_identity, 0, identity_rows, hk_can_parse_identity, HkExpIdentity)
READ_TABLE(exp_uart_getall_sensors, 2, sensor_rows, hk_can_parse_sensors, HkExpSensors)
READ_TABLE(exp_uart_getall_thermal, 3, thermal_rows, hk_can_parse_thermal, HkExpThermal)
READ_TABLE(exp_uart_getall_inventory, 6, inventory_rows, hk_can_parse_inventory, HkExpInventory)
READ_TABLE(exp_uart_getall_icd_status, 12, icd_rows, hk_can_parse_icd_status, HkExpIcdStatus)
#undef READ_TABLE
