#include "services/port_config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { const char *key; const char *default_value; unsigned min, max; } setting_t;
static const setting_t settings[] = {
    {"DAL_OBC_RS422_PORT", "/dev/ttyS1", 0, 0},
    {"DAL_EXP_UART_PORT", "/dev/ttyS4", 0, 0},
    {"DAL_EXP_UART_BAUD", "250000", 9600, 3000000},
    {"DAL_GUI_PDU_PORT", "/dev/ttyS3", 0, 0},
    {"DAL_GUI_PDU_UNIT", "1", 1, 247},
    {"DAL_EXP_CSP_RS485_PORT", "/dev/ttyS2", 0, 0},
    {"DAL_EXP_CSP_RS485_BAUD", "115200", 9600, 1000000},
    {"DAL_EXP_CSP_RS485_ADDRESS", "2", 1, 30},
    {"DAL_EXP_CSP_RS485_ENABLE", "1", 0, 1},
};
#define SETTING_COUNT (sizeof(settings) / sizeof(settings[0]))
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *config_path(void)
{
    const char *path = getenv("DAL_OBC_PORT_CONFIG_FILE");
    return path && *path ? path : "/etc/default/dal-obc-fsw";
}

static int setting_index(const char *key)
{
    for (size_t i = 0; i < SETTING_COUNT; i++)
        if (strcmp(key, settings[i].key) == 0) return (int)i;
    return -1;
}

static bool valid_value(const setting_t *setting, const char *value)
{
    if (setting->max) {
        if (!*value || *value == '-') return false;
        for (const char *p = value; *p; p++) if (!isdigit((unsigned char)*p)) return false;
        errno = 0;
        unsigned long number = strtoul(value, NULL, 10);
        return !errno && number >= setting->min && number <= setting->max;
    }
    if (!*value || strncmp(value, "/dev/", 5) != 0 || strlen(value) > 127) return false;
    for (const char *p = value + 5; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '/' && *p != '_' && *p != '-' && *p != '.') return false;
    return true;
}

static void read_values(char values[SETTING_COUNT][128])
{
    for (size_t i = 0; i < SETTING_COUNT; i++)
        snprintf(values[i], 128, "%s", settings[i].default_value);
    FILE *file = fopen(config_path(), "r");
    if (!file) return;
    char *line = NULL;
    size_t size = 0;
    while (getline(&line, &size, file) >= 0) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        int index = setting_index(line);
        if (index < 0) continue;
        char *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        if (valid_value(&settings[index], value)) snprintf(values[index], 128, "%s", value);
    }
    free(line);
    fclose(file);
}

int port_config_get(char *out, size_t capacity)
{
    char values[SETTING_COUNT][128];
    pthread_mutex_lock(&config_lock);
    read_values(values);
    pthread_mutex_unlock(&config_lock);
    size_t used = 0;
    for (size_t i = 0; i < SETTING_COUNT; i++) {
        int n = snprintf(out + used, capacity - used, "%s=%s\n", settings[i].key, values[i]);
        if (n < 0 || (size_t)n >= capacity - used) return -1;
        used += (size_t)n;
    }
    return 0;
}

int port_config_set(const char *key, const char *value, char *error, size_t capacity)
{
    int index = setting_index(key);
    if (index < 0 || !valid_value(index < 0 ? NULL : &settings[index], value)) {
        snprintf(error, capacity, "invalid port setting or value");
        return -1;
    }
    pthread_mutex_lock(&config_lock);
    char values[SETTING_COUNT][128];
    read_values(values);
    snprintf(values[index], 128, "%s", value);
    if (strcmp(values[8], "1") == 0 && strcmp(values[3], values[5]) == 0) {
        snprintf(error, capacity, "PDU and EXP CSP RS-485 cannot share one port");
        pthread_mutex_unlock(&config_lock);
        return -1;
    }
    const char *path = config_path();
    size_t length = strlen(path);
    char *temp = malloc(length + 12);
    if (!temp) goto fail;
    snprintf(temp, length + 12, "%s.tmp.XXXXXX", path);
    int fd = mkstemp(temp);
    if (fd < 0) { free(temp); goto fail; }
    FILE *output = fdopen(fd, "w");
    if (!output) { close(fd); unlink(temp); free(temp); goto fail; }
    FILE *input = fopen(path, "r");
    struct stat st;
    if (input && fstat(fileno(input), &st) == 0) fchmod(fd, st.st_mode & 0777);
    else fchmod(fd, 0644);
    bool replaced = false;
    char *line = NULL;
    size_t size = 0;
    if (input) {
        while (getline(&line, &size, input) >= 0) {
            char *eq = strchr(line, '=');
            if (eq && (size_t)(eq - line) == strlen(key) && !strncmp(line, key, strlen(key))) {
                if (!replaced) fprintf(output, "%s=%s\n", key, value);
                replaced = true;
            } else {
                fputs(line, output);
                size_t len = strlen(line);
                if (len && line[len - 1] != '\n') fputc('\n', output);
            }
        }
        fclose(input);
    }
    free(line);
    if (!replaced) fprintf(output, "%s=%s\n", key, value);
    int failed = ferror(output) || fflush(output) || fsync(fd);
    if (fclose(output) != 0) failed = 1;
    if (!failed && rename(temp, path) != 0) failed = 1;
    if (failed) unlink(temp);
    free(temp);
    if (!failed) { pthread_mutex_unlock(&config_lock); return 0; }
fail:
    snprintf(error, capacity, "cannot write %s: %s", config_path(), strerror(errno));
    pthread_mutex_unlock(&config_lock);
    return -1;
}
