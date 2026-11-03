/* DAL console client for the OBC housekeeping snapshot. No hardware polling. */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_SOURCE "/run/dal/hk_service/obc_data.json"
#define MAX_JSON 8192
#define MAX_FIELDS 64

typedef struct {
    char name[64];
    const char *value;
    size_t length;
} Field;

typedef struct {
    char text[MAX_JSON + 1];
    Field fields[MAX_FIELDS];
    size_t count;
} Snapshot;

typedef struct {
    const char *text;
    size_t length;
} Span;

static const char *const temperature[] = {
    "temp_valid", "temp_sample_unix_s", "temp_onboard_c", "temp_ntc_c", NULL
};
static const char *const environment[] = {
    "bme_valid", "bme_sample_unix_s", "bme_temp_c", "bme_pressure_pa",
    "bme_humidity_rh", "bme_gas_valid", "bme_gas_resistance_ohm", NULL
};
static const char *const power[] = {
    "pac_valid", "pac_sample_unix_s", "pac_u6_valid", "pac_u2_valid",
    "pwr_rail_names", "pwr_volt_v", "pwr_curr_a", NULL
};
static const char *const status[] = {
    "board", "schema_version", "online", "boot_count", "obc_timestamp",
    "temp_valid", "bme_valid", "pac_valid", "pac_u6_valid", "pac_u2_valid", NULL
};

static void skip_ws(const char **p) {
    while (isspace((unsigned char)**p)) ++*p;
}

static int scan_string(const char **p) {
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

static int scan_value(const char **p, unsigned depth) {
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
            skip_ws(p);
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

static int parse_snapshot(Snapshot *s) {
    const char *p = s->text;
    skip_ws(&p);
    if (*p++ != '{') return -1;
    skip_ws(&p);
    if (*p != '}') for (;;) {
        if (*p != '"') return -1;
        const char *key = ++p;
        while (*p && *p != '"') {
            /* hk_service's field names are plain ASCII. */
            if (*p == '\\' || (unsigned char)*p < 0x20) return -1;
            ++p;
        }
        if (!*p || s->count >= MAX_FIELDS) return -1;
        size_t n = (size_t)(p - key);
        if (!n || n >= sizeof(s->fields[0].name)) return -1;
        for (size_t j = 0; j < s->count; j++)
            if (strlen(s->fields[j].name) == n &&
                !memcmp(s->fields[j].name, key, n)) return -1;
        Field *f = &s->fields[s->count++];
        memcpy(f->name, key, n);
        f->name[n] = 0;
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

static const Field *find_field(const Snapshot *s, const char *name) {
    for (size_t i = 0; i < s->count; i++)
        if (!strcmp(s->fields[i].name, name)) return &s->fields[i];
    return NULL;
}

static int array_items(const Snapshot *s, const char *name, Span *items, size_t capacity) {
    const Field *f = find_field(s, name);
    if (!f || !f->length || *f->value != '[') return -1;
    const char *p = f->value + 1;
    const char *end = f->value + f->length;
    size_t n = 0;
    skip_ws(&p);
    if (*p == ']') return 0;
    while (p < end && n < capacity) {
        items[n].text = p;
        if (scan_value(&p, 0) < 0) return -1;
        items[n].length = (size_t)(p - items[n].text);
        n++;
        skip_ws(&p);
        if (*p == ']') return (int)n;
        if (*p++ != ',') return -1;
        skip_ws(&p);
    }
    return -1;
}

static void emit_span(const Span *span) {
    printf("%.*s", (int)span->length, span->text);
}

static void emit_four(const Span *items, size_t offset) {
    putchar('[');
    for (size_t j = 0; j < 4; j++) {
        if (j) putchar(',');
        emit_span(&items[offset + j]);
    }
    putchar(']');
}

static int read_snapshot(const char *path, Snapshot *s) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(s->text, 1, MAX_JSON + 1, fp);
    int failed = ferror(fp);
    fclose(fp);
    if (failed || n == 0 || n > MAX_JSON) return -2;
    s->text[n] = 0;
    s->count = 0;
    return parse_snapshot(s) == 0 ? 0 : -2;
}

static void json_error(const char *code) {
    printf("{\"schema_version\":1,\"ok\":false,\"board\":\"obc\",\"error\":\"%s\"}\n", code);
}

static bool emit_field(const Snapshot *s, const char *name, bool comma) {
    const Field *f = find_field(s, name);
    if (!f) return false;
    printf("%s\"%s\":%.*s", comma ? "," : "", name, (int)f->length, f->value);
    return true;
}

static long long timestamp(const Snapshot *s) {
    const Field *f = find_field(s, "obc_timestamp");
    if (!f || f->length >= 32) return -1;
    char buf[32], *end;
    memcpy(buf, f->value, f->length);
    buf[f->length] = 0;
    errno = 0;
    long long value = strtoll(buf, &end, 10);
    return errno || *end || value <= 0 ? -1 : value;
}

static int show_get(const Snapshot *s, const char *selection, unsigned max_age) {
    const char *const *names = NULL;
    int ntc = -1, pac = -1;
    bool onboard = !strcmp(selection, "onboard_temp");
    bool gas = !strcmp(selection, "bme_gas");
    if (strlen(selection) == 4 && !strncmp(selection, "ntc", 3) &&
        selection[3] >= '1' && selection[3] <= '4')
        ntc = selection[3] - '1';
    if (!strcmp(selection, "pac_u6")) pac = 0;
    if (!strcmp(selection, "pac_u2")) pac = 1;
    if (!strcmp(selection, "temperature")) names = temperature;
    else if (!strcmp(selection, "environment") || !strcmp(selection, "bme")) names = environment;
    else if (!strcmp(selection, "power")) names = power;
    else if (!strcmp(selection, "status")) names = status;
    else if (strcmp(selection, "all") && ntc < 0 && pac < 0 &&
             !onboard && !gas && !find_field(s, selection)) {
        json_error("UNKNOWN_SENSOR");
        return 2;
    }
    Span ntcs[4], rails[8], volts[8], currents[8];
    if ((ntc >= 0 && array_items(s, "temp_ntc_c", ntcs, 4) != 4) ||
        (pac >= 0 && (array_items(s, "pwr_rail_names", rails, 8) != 8 ||
                      array_items(s, "pwr_volt_v", volts, 8) != 8 ||
                      array_items(s, "pwr_curr_a", currents, 8) != 8)) ||
        (onboard && !find_field(s, "temp_onboard_c")) ||
        (gas && (!find_field(s, "bme_gas_valid") ||
                 !find_field(s, "bme_gas_resistance_ohm")))) {
        json_error("SENSOR_UNAVAILABLE");
        return 1;
    }
    long long ts = timestamp(s);
    time_t now = time(NULL);
    long long age = ts < 0 || now < 0 || (long long)now < ts ? -1 : (long long)now - ts;
    bool stale = age < 0 || (unsigned long long)age > max_age;
    printf("{\"schema_version\":1,\"ok\":true,\"board\":\"obc\","
           "\"sample_time\":");
    if (ts < 0) printf("null"); else printf("%lld", ts);
    printf(",\"age_s\":");
    if (age < 0) printf("null"); else printf("%lld", age);
    printf(",\"stale\":%s,\"quality\":{", stale ? "true" : "false");
    const char *const quality[] = {
        "temp_valid", "bme_valid", "bme_gas_valid", "pac_u6_valid", "pac_u2_valid", NULL
    };
    bool comma = false;
    for (size_t i = 0; quality[i]; i++)
        if (emit_field(s, quality[i], comma)) comma = true;
    printf("},\"data\":{");
    comma = false;
    if (!strcmp(selection, "all")) {
        for (size_t i = 0; i < s->count; i++)
            if (emit_field(s, s->fields[i].name, comma)) comma = true;
    } else if (names) {
        for (size_t i = 0; names[i]; i++)
            if (emit_field(s, names[i], comma)) comma = true;
    } else if (ntc >= 0) {
        printf("\"sensor\":\"ntc%d\",\"value_c\":", ntc + 1);
        emit_span(&ntcs[ntc]);
    } else if (pac >= 0) {
        size_t offset = (size_t)pac * 4;
        printf("\"sensor\":\"pac_%s\",\"rail_names\":", pac ? "u2" : "u6");
        emit_four(rails, offset);
        printf(",\"voltage_v\":");
        emit_four(volts, offset);
        printf(",\"current_a\":");
        emit_four(currents, offset);
    } else if (onboard) emit_field(s, "temp_onboard_c", false);
    else if (gas) {
        emit_field(s, "bme_gas_valid", false);
        printf(",");
        emit_field(s, "bme_gas_resistance_ohm", false);
    } else emit_field(s, selection, false);
    puts("}}");
    return 0;
}

static void show_discovery(const Snapshot *s, bool available) {
    printf("{\"schema_version\":1,\"ok\":true,\"board\":\"obc\","
           "\"available\":%s,\"source\":\"hk_service\","
           "\"groups\":[\"all\",\"temperature\",\"environment\",\"power\",\"status\"],"
           "\"selectors\":[\"onboard_temp\",\"ntc1\",\"ntc2\",\"ntc3\",\"ntc4\","
           "\"bme\",\"bme_gas\",\"pac_u6\",\"pac_u2\"],"
           "\"fields\":[", available ? "true" : "false");
    if (available) for (size_t i = 0; i < s->count; i++)
        printf("%s\"%s\"", i ? "," : "", s->fields[i].name);
    puts("]}");
}

static void usage(FILE *out) {
    fprintf(out,
        "Usage: dal [--source PATH] [--max-age SECONDS] discovery [obc]\n"
        "       dal [--source PATH] [--max-age SECONDS] get obc_sensor [all|temperature|environment|power|status|SENSOR|FIELD]\n"
        "       dal [--source PATH] [--max-age SECONDS] telemetry obc [SELECTION]\n"
        "Reads the hk_service OBC snapshot and prints one JSON object.\n");
}

int main(int argc, char **argv) {
    const char *path = DEFAULT_SOURCE;
    unsigned max_age = 15;
    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--source") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--max-age") && i + 1 < argc) {
            char *end;
            errno = 0;
            unsigned long n = strtoul(argv[++i], &end, 10);
            if (errno || *end || argv[i][0] == '-' || n > UINT_MAX) {
                usage(stderr); return 2;
            }
            max_age = (unsigned)n;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(stdout); return 0;
        } else break;
    }
    if (i >= argc) { usage(stderr); return 2; }
    const char *verb = argv[i++];
    bool discovery = !strcmp(verb, "discovery") || !strcmp(verb, "discover");
    bool get = !strcmp(verb, "get") || !strcmp(verb, "telemetry");
    if (!discovery && !get) { usage(stderr); return 2; }
    if (i < argc && strcmp(argv[i], "obc") && strcmp(argv[i], "obc_sensor")) {
        puts("{\"schema_version\":1,\"ok\":false,\"error\":\"UNSUPPORTED_BOARD\","
             "\"supported_boards\":[\"obc\"]}");
        return 2;
    }
    if (i < argc) i++;
    const char *selection = i < argc ? argv[i++] : "all";
    if (i != argc || (discovery && strcmp(selection, "all"))) {
        usage(stderr); return 2;
    }
    Snapshot *s = calloc(1, sizeof(*s));
    if (!s) { json_error("OUT_OF_MEMORY"); return 1; }
    int result = read_snapshot(path, s);
    if (discovery) {
        show_discovery(s, result == 0);
        free(s); return 0;
    }
    if (result != 0) {
        json_error(result == -1 ? "SNAPSHOT_UNAVAILABLE" : "SNAPSHOT_INVALID");
        free(s); return 1;
    }
    int ret = show_get(s, selection, max_age);
    free(s);
    return ret;
}
