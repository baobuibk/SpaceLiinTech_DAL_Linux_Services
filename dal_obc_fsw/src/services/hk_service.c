#include "services/hk_service.h"
#include "services/obc_snapshot.h"
#include "dal_obc_config.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_t g_thread;
static atomic_bool g_running;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bee_dal_telemetry_t g_tm;

static const char *snapshot_path(void)
{
    const char *path = getenv("DAL_OBC_HK_SNAPSHOT_PATH");
    return path && *path ? path : DAL_OBC_HK_SNAPSHOT_PATH;
}

static void poll_snapshot(void)
{
    time_t now = time(NULL);
    bee_dal_telemetry_t next;
    if (obc_snapshot_read(snapshot_path(), now,
                          DAL_OBC_HK_MAX_AGE_SEC, &next) == 0) {
        pthread_mutex_lock(&g_lock);
        g_tm = next;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    pthread_mutex_lock(&g_lock);
    /* A transient read failure can occur during service restart. Retain the
     * last complete sample until its timestamp exceeds the age limit. */
    bool expired = g_tm.obc_snapshot_valid &&
        (now < (time_t)g_tm.obc_sample_epoch ||
         (uint64_t)now > (uint64_t)g_tm.obc_sample_epoch + DAL_OBC_HK_MAX_AGE_SEC);
    if (expired)
        memset(&g_tm, 0, sizeof(g_tm));
    pthread_mutex_unlock(&g_lock);
    if (expired)
        fprintf(stderr, "[HK] OBC snapshot unavailable or stale\n");
}

static void *hk_thread(void *arg)
{
    (void)arg;
    while (atomic_load(&g_running)) {
        sleep(1);
        if (atomic_load(&g_running)) poll_snapshot();
    }
    return NULL;
}

xerr_t hk_service_init(void)
{
    memset(&g_tm, 0, sizeof(g_tm));
    return XERR_OK;
}

xerr_t hk_service_start(void)
{
    if (atomic_load(&g_running)) return XERR_STATE;
    poll_snapshot();
    atomic_store(&g_running, true);
    if (pthread_create(&g_thread, NULL, hk_thread, NULL) != 0) {
        atomic_store(&g_running, false);
        return XERR_IO;
    }
    printf("[HK] Polling %s every second\n", snapshot_path());
    return XERR_OK;
}

void hk_service_stop(void)
{
    if (!atomic_load(&g_running)) return;
    atomic_store(&g_running, false);
    pthread_join(g_thread, NULL);
}

bool hk_service_is_running(void) { return atomic_load(&g_running); }

void hk_service_get_telemetry(bee_dal_telemetry_t *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_tm;
    pthread_mutex_unlock(&g_lock);
}
