#include "services/obc_snapshot.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JSON 8192
#define MAX_FIELDS 48

typedef struct {
    char name[64];
    const char *value;
    size_t length;
} field_t;

typedef struct {
    char text[MAX_JSON + 1];
    field_t fields[MAX_FIELDS];
    size_t count;
} json_t;

static void skip_ws(const char **p)
{
    while (isspace((unsigned char)**p)) ++*p;
}

static int scan_string(const char **p)
{
    if (*(*p)++ != '"') return -1;
    while (**p) {
        unsigned char c = (unsigned char)*(*p)++;
        if (c == '"') return 0;
        if (c == '\\') {
            c = (unsigned char)*(*p)++;
            if (!c) return -1;
            if (c == 'u') {
                for (int i = 0; i < 4; i++) {
                    if (!isxdigit((unsigned char)**p)) return -1;
                    ++*p;
                }
            } else if (!strchr("\"\\/bfnrt", c)) return -1;
        } else if (c < 0x20) return -1;
    }
    return -1;
}

static int scan_value(const char **p, unsigned depth)
{
    if (depth > 16) return -1;
    skip_ws(p);
    if (**p == '"') return scan_string(p);
    if (**p == '{' || **p == '[') {
        char close = **p == '{' ? '}' : ']';
        bool object = **p == '{';
        ++*p;
        skip_ws(p);
        if (**p == close) { ++*p; return 0; }
        for (;;) {
            if (object) {
                if (scan_string(p) < 0) return -1;
                skip_ws(p);
                if (*(*p)++ != ':') return -1;
            }
            if (scan_value(p, depth + 1) < 0) return -1;
            skip_ws(p);
            if (**p == close) { ++*p; return 0; }
            if (*(*p)++ != ',') return -1;
        }
    }
    if (!strncmp(*p, "true", 4)) { *p += 4; return 0; }
    if (!strncmp(*p, "false", 5)) { *p += 5; return 0; }
    if (!strncmp(*p, "null", 4)) { *p += 4; return 0; }
    const char *start = *p;
    if (**p == '-') ++*p;
    if (**p == '0') ++*p;
    else {
        if (**p < '1' || **p > '9') return -1;
        while (isdigit((unsigned char)**p)) ++*p;
    }
    if (**p == '.') {
        ++*p;
        if (!isdigit((unsigned char)**p)) return -1;
        while (isdigit((unsigned char)**p)) ++*p;
    }
    if (**p == 'e' || **p == 'E') {
        ++*p;
        if (**p == '+' || **p == '-') ++*p;
        if (!isdigit((unsigned char)**p)) return -1;
        while (isdigit((unsigned char)**p)) ++*p;
    }
    return *p == start ? -1 : 0;
}

static int parse_json(json_t *j)
{
    const char *p = j->text;
    skip_ws(&p);
    if (*p++ != '{') return -1;
    skip_ws(&p);
    if (*p != '}') for (;;) {
        if (*p != '"') return -1;
        const char *key = ++p;
        while (*p && *p != '"') {
            if (*p == '\\' || (unsigned char)*p < 0x20) return -1;
            ++p;
        }
        if (!*p || j->count >= MAX_FIELDS) return -1;
        size_t length = (size_t)(p - key);
        if (!length || length >= sizeof(j->fields[0].name)) return -1;
        for (size_t i = 0; i < j->count; i++)
            if (strlen(j->fields[i].name) == length &&
                !memcmp(j->fields[i].name, key, length)) return -1;
        field_t *f = &j->fields[j->count++];
        memcpy(f->name, key, length);
        f->name[length] = '\0';
        ++p;
        skip_ws(&p);
        if (*p++ != ':') return -1;
        skip_ws(&p);
        f->value = p;
        if (scan_value(&p, 0) < 0) return -1;
        f->length = (size_t)(p - f->value);
        skip_ws(&p);
        if (*p == '}') break;
        if (*p++ != ',') return -1;
        skip_ws(&p);
    }
    if (*p++ != '}') return -1;
    skip_ws(&p);
    return *p ? -1 : 0;
}

static const field_t *field(const json_t *j, const char *name)
{
    for (size_t i = 0; i < j->count; i++)
        if (!strcmp(j->fields[i].name, name)) return &j->fields[i];
    return NULL;
}

static int field_is(const json_t *j, const char *name, const char *literal)
{
    const field_t *f = field(j, name);
    return f && f->length == strlen(literal) &&
           !memcmp(f->value, literal, f->length);
}

static int parse_double_span(const char *p, size_t length, double *out)
{
    if (length == 0 || length >= 64) return -1;
    char buf[64], *end;
    memcpy(buf, p, length);
    buf[length] = '\0';
    errno = 0;
    double value = strtod(buf, &end);
    if (errno || end == buf || *end || !isfinite(value)) return -1;
    *out = value;
    return 0;
}

static int number(const json_t *j, const char *name, double *out)
{
    const field_t *f = field(j, name);
    return f ? parse_double_span(f->value, f->length, out) : -1;
}

static int u32(const json_t *j, const char *name, uint32_t *out)
{
    double value;
    if (number(j, name, &value) != 0 || value < 0 ||
        value > UINT32_MAX || trunc(value) != value) return -1;
    *out = (uint32_t)value;
    return 0;
}

static int flag(const json_t *j, const char *name, bool *out)
{
    uint32_t value;
    if (u32(j, name, &value) != 0 || value > 1) return -1;
    *out = value != 0;
    return 0;
}

static int number_array(const json_t *j, const char *name,
                        float *out, size_t count, bool allow_na)
{
    const field_t *f = field(j, name);
    if (!f || f->length < 2 || *f->value != '[') return -1;
    const char *p = f->value + 1;
    const char *end = f->value + f->length;
    for (size_t i = 0; i < count; i++) {
        skip_ws(&p);
        if (p >= end) return -1;
        const char *start = p;
        if (scan_value(&p, 0) < 0 || p > end) return -1;
        if (allow_na && (size_t)(p - start) == 5 &&
            !memcmp(start, "\"N/A\"", 5)) out[i] = -50.0f;
        else {
            double value;
            if (parse_double_span(start, (size_t)(p - start), &value) != 0 ||
                value > 1.0e9 || value < -1.0e9) return -1;
            out[i] = (float)value;
        }
        skip_ws(&p);
        if (i + 1 < count) {
            if (p >= end || *p++ != ',') return -1;
        }
    }
    return p + 1 == end && *p == ']' ? 0 : -1;
}

static int rail_names_match(const json_t *j)
{
    static const char *const names[8] = {
        "5V_SAMA7", "5V_FPGA", "3V3", "5V_AI",
        "PMIC_3V3", "PMIC_DDR_1V35", "PMIC_CORE_1V15", "PMIC_CPU_1V15"
    };
    const field_t *f = field(j, "pwr_rail_names");
    if (!f || f->length < 2 || *f->value != '[') return -1;
    const char *p = f->value + 1;
    const char *end = f->value + f->length;
    for (size_t i = 0; i < 8; i++) {
        skip_ws(&p);
        if (p >= end || *p++ != '"') return -1;
        size_t length = strlen(names[i]);
        if ((size_t)(end - p) < length + 1 ||
            memcmp(p, names[i], length) || p[length] != '"') return -1;
        p += length + 1;
        skip_ws(&p);
        if (i < 7 && (p >= end || *p++ != ',')) return -1;
    }
    return p + 1 == end && *p == ']' ? 0 : -1;
}

static int16_t temp_fixed(double value)
{
    value = round(value * 100.0);
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    return (int16_t)value;
}

static uint16_t u16_fixed(double value)
{
    value = round(value);
    if (value < 0) value = 0;
    if (value > UINT16_MAX) value = UINT16_MAX;
    return (uint16_t)value;
}

int obc_snapshot_read(const char *path, time_t now, unsigned max_age_sec,
                      bee_dal_telemetry_t *out)
{
    if (!path || !out || now <= 0) return -1;
    json_t j = {0};
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t length = fread(j.text, 1, MAX_JSON + 1, fp);
    int failed = ferror(fp);
    fclose(fp);
    if (failed || length == 0 || length > MAX_JSON) return -1;
    j.text[length] = '\0';
    if (parse_json(&j) != 0 || !field_is(&j, "board", "\"obc\"")) return -1;

    uint32_t schema, online, sample, boot = 0;
    if (u32(&j, "schema_version", &schema) != 0 || schema != 1 ||
        u32(&j, "online", &online) != 0 || online != 1 ||
        u32(&j, "obc_timestamp", &sample) != 0 || sample == 0 ||
        sample > (uint64_t)now + 2 ||
        (uint64_t)now > (uint64_t)sample + max_age_sec) return -1;
    if (!field_is(&j, "boot_count", "\"N/A\"") &&
        u32(&j, "boot_count", &boot) != 0) return -1;

    bee_dal_telemetry_t next = {0};
    next.obc_snapshot_valid = true;
    next.obc_sample_epoch = sample;
    next.sys_time_epoch = sample;
    next.bootcount = boot;
    if (flag(&j, "temp_valid", &next.temp_valid) != 0 ||
        flag(&j, "bme_valid", &next.bme_valid) != 0 ||
        flag(&j, "bme_gas_valid", &next.bme_gas_valid) != 0 ||
        flag(&j, "pac_u6_valid", &next.pac_valid[0]) != 0 ||
        flag(&j, "pac_u2_valid", &next.pac_valid[1]) != 0 ||
        u32(&j, "temp_sample_unix_s", &next.temp_sample_epoch) != 0 ||
        u32(&j, "bme_sample_unix_s", &next.bme_sample_epoch) != 0 ||
        u32(&j, "pac_sample_unix_s", &next.pac_sample_epoch) != 0) return -1;

    double value;
    if (number(&j, "temp_onboard_c", &value) != 0) return -1;
    if (next.temp_valid) {
        next.onboard_temp_full_c = (float)value;
        next.onboard_temp_c = temp_fixed(value);
    }
    if (number_array(&j, "temp_ntc_c", next.ntc_c, 4, true) != 0) return -1;
    if (number(&j, "bme_temp_c", &value) != 0) return -1;
    if (next.bme_valid) {
        next.bme_temp_full_c = (float)value;
        next.baro_temp_c = temp_fixed(value);
    }
    if (number(&j, "bme_pressure_pa", &value) != 0) return -1;
    if (next.bme_valid) {
        next.bme_pressure_pa = (float)value;
        next.baro_press_pa = u16_fixed(value / 10.0);
    }
    if (number(&j, "bme_humidity_rh", &value) != 0) return -1;
    if (next.bme_valid) {
        next.bme_humidity_rh = (float)value;
        next.baro_hum_rh = u16_fixed(value * 10.0);
    }
    if (next.bme_gas_valid && next.bme_valid) {
        if (number(&j, "bme_gas_resistance_ohm", &value) != 0 || value <= 0) return -1;
        next.bme_gas_resistance_ohm = (float)value;
    } else {
        const field_t *gas = field(&j, "bme_gas_resistance_ohm");
        if (!gas) return -1;
        next.bme_gas_valid = false;
    }
    if (rail_names_match(&j) != 0 ||
        number_array(&j, "pwr_volt_v", next.pwr_volt_v, 8, false) != 0 ||
        number_array(&j, "pwr_curr_a", next.pwr_curr_a, 8, false) != 0) return -1;
    for (unsigned chip = 0; chip < 2; chip++)
        if (!next.pac_valid[chip]) {
            memset(&next.pwr_volt_v[chip * 4], 0, 4 * sizeof(float));
            memset(&next.pwr_curr_a[chip * 4], 0, 4 * sizeof(float));
        }
    *out = next;
    return 0;
}
