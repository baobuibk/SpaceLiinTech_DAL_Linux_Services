#include "services/uvc_capture.h"
#include "services/gui_hw.h"
#include "dal_obc_config.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define UVC_HISTORY 8
typedef struct { uint32_t id; uint8_t state, error; } job_state_t;
typedef struct { uint8_t camera, pwm_duty; uint32_t id; char path[512]; } job_arg_t;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static job_state_t history[4][UVC_HISTORY];
static unsigned next_slot[4];
static bool busy;

static const char *base_dir(void)
{
    const char *value = getenv("DAL_UVC_DATA_DIR");
    return value && *value ? value : BEEIF_DATA_DIR "/images/usb";
}

static int profile_pwm(uint8_t camera, uint8_t *duty)
{
    const char *root = getenv("DAL_UVC_PROFILE_DIR");
    if (!root || !*root) root = "/var/lib/dal/uvc";
    char path[512];
    if (snprintf(path, sizeof(path), "%s/camera%u.bin", root, camera) >= (int)sizeof(path)) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) { if (errno == ENOENT) { *duty = 0; return 0; } return -1; }
    uint8_t profile[32];
    size_t n = fread(profile, 1, sizeof(profile), fp);
    int extra = fgetc(fp);
    fclose(fp);
    if (n != sizeof(profile) || extra != EOF || profile[0] != camera || profile[1] != 1) return -1;
    uint16_t mask = (uint16_t)profile[2] | ((uint16_t)profile[3] << 8);
    if (mask & 0xe000 || profile[31] > 100 || (profile[31] && !(mask & (1u << 12)))) return -1;
    *duty = (mask & (1u << 12)) ? profile[31] : 0;
    return 0;
}

static int image_path(uint8_t camera, uint32_t id, char *path, size_t capacity)
{
    int n = snprintf(path, capacity, "%s/USB%u_%08u.jpg", base_dir(), camera - 1, id);
    return n > 0 && (size_t)n < capacity ? 0 : -1;
}

static void encode(uint8_t camera, uint8_t state, uint8_t error, uint32_t id, uint8_t out[8])
{
    out[0] = camera; out[1] = state; out[2] = error; out[3] = 0;
    out[4] = (uint8_t)id; out[5] = (uint8_t)(id >> 8);
    out[6] = (uint8_t)(id >> 16); out[7] = (uint8_t)(id >> 24);
}

static job_state_t *find_job(uint8_t camera, uint32_t id)
{
    for (unsigned i = 0; i < UVC_HISTORY; ++i)
        if (history[camera - 1][i].id == id) return &history[camera - 1][i];
    return NULL;
}

static void *capture_worker(void *opaque)
{
    job_arg_t *job = opaque;
    char camera_arg[2] = {(char)('0' + job->camera), 0};
    const char *tool = getenv("DAL_USB_CAM_TOOL");
    if (!tool || !*tool) tool = "/home/root/tools/bin/usb_cam";
    pthread_mutex_lock(&lock);
    job_state_t *entry = find_job(job->camera, job->id);
    if (entry) entry->state = 2;
    pthread_mutex_unlock(&lock);

    bool lit = job->pwm_duty > 0;
    bool led_ok = !lit || gui_hw_set_led(job->pwm_duty) == 0;
    pid_t pid = led_ok ? fork() : -1;
    int status = 0;
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        execl(tool, tool, camera_arg, "-o", job->path, (char *)NULL);
        _exit(127);
    }
    if (pid < 0 || waitpid(pid, &status, 0) < 0) status = -1;
    bool led_off_ok = !lit || gui_hw_set_led(0) == 0;
    bool ok = led_ok && led_off_ok && status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    struct stat st;
    bool saved = ok && stat(job->path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
    pthread_mutex_lock(&lock);
    entry = find_job(job->camera, job->id);
    if (entry) {
        entry->state = saved ? 3 : 4;
        entry->error = saved ? 0 : ok ? 2 : 1;
    }
    busy = false;
    pthread_mutex_unlock(&lock);
    free(job);
    return NULL;
}

int uvc_capture_queue(uint8_t camera, uint32_t request_id, uint8_t reply[8])
{
    if (camera < 1 || camera > 4 || !request_id) return -2;
    char directory[512], path[512];
    int n = snprintf(directory, sizeof(directory), "%s", base_dir());
    if (n <= 0 || (size_t)n >= sizeof(directory) || image_path(camera, request_id, path, sizeof(path))) return -2;
    struct stat st;
    if (stat(path, &st) == 0 || errno != ENOENT) return -2;
    if (mkdir(BEEIF_DATA_DIR, 0755) && errno != EEXIST && !getenv("DAL_UVC_DATA_DIR")) return -1;
    if (!getenv("DAL_UVC_DATA_DIR")) {
        if (mkdir(BEEIF_DATA_DIR "/images", 0755) && errno != EEXIST) return -1;
    }
    if (mkdir(base_dir(), 0755) && errno != EEXIST) return -1;
    job_arg_t *job = calloc(1, sizeof(*job));
    if (!job) return -1;
    job->camera = camera; job->id = request_id;
    if (profile_pwm(camera, &job->pwm_duty)) { free(job); return -1; }
    memcpy(job->path, path, strlen(path) + 1);
    pthread_mutex_lock(&lock);
    if (busy || find_job(camera, request_id)) {
        pthread_mutex_unlock(&lock); free(job); return -2;
    }
    unsigned slot = next_slot[camera - 1]++ % UVC_HISTORY;
    history[camera - 1][slot] = (job_state_t){.id = request_id, .state = 1};
    busy = true;
    pthread_t thread;
    if (pthread_create(&thread, NULL, capture_worker, job) != 0) {
        history[camera - 1][slot] = (job_state_t){0};
        busy = false;
        pthread_mutex_unlock(&lock); free(job); return -1;
    }
    pthread_detach(thread);
    encode(camera, 1, 0, request_id, reply);
    pthread_mutex_unlock(&lock);
    return 0;
}

int uvc_capture_status(uint8_t camera, uint32_t request_id, uint8_t reply[8])
{
    if (camera < 1 || camera > 4 || !request_id) return -2;
    pthread_mutex_lock(&lock);
    job_state_t *entry = find_job(camera, request_id);
    uint8_t state = entry ? entry->state : 0, error = entry ? entry->error : 0;
    pthread_mutex_unlock(&lock);
    if (!entry) {
        char path[512]; struct stat st;
        if (!image_path(camera, request_id, path, sizeof(path)) && stat(path, &st) == 0 &&
            S_ISREG(st.st_mode) && st.st_size > 0) state = 3;
    }
    encode(camera, state, error, request_id, reply);
    return 0;
}
