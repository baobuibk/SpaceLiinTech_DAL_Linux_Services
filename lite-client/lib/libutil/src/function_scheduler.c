/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <gs/util/function_scheduler.h>

#include <gs/util/check.h>
#include <gs/util/time.h>
#include <gs/util/minmax.h>
#include <gs/util/check.h>
#include <stdlib.h>
#include <limits.h>

typedef struct {
    // function to call
    gs_function_scheduler_function_t function;
    // function's user data
    void * user_data;
    // timeout in mS
    uint32_t timeout_ms;
    // last execution time in mS
    uint32_t last_exec_ms;
} gs_function_scheduler_entry_t;

struct gs_function_scheduler {
    // Max timeout in mS
    uint32_t max_timeout_ms;
    // allocated entries
    unsigned int max_entries;
    // entries
    gs_function_scheduler_entry_t * entries;
};

GS_CHECK_STATIC_ASSERT(sizeof(int) >= 2, int_must_be_at_least_16bit);

gs_error_t gs_function_scheduler_create(uint32_t max_timeout_ms, unsigned int max_entries, gs_function_scheduler_t ** return_scheduler)
{
    GS_CHECK_ARG(max_timeout_ms <= INT_MAX);
    GS_CHECK_ARG(max_entries > 0);
    GS_CHECK_ARG(return_scheduler != NULL);

    gs_function_scheduler_entry_t * entries = calloc(max_entries, sizeof(*entries));
    if (entries == NULL) {
        return GS_ERROR_ALLOC;
    }

    gs_function_scheduler_t * scheduler = calloc(1, sizeof(*scheduler));
    if (scheduler == NULL) {
        free (entries);
        return GS_ERROR_ALLOC;
    }

    scheduler->max_timeout_ms = max_timeout_ms;
    scheduler->entries     = entries;
    scheduler->max_entries = max_entries;

    *return_scheduler = scheduler;

    return GS_OK;
}

gs_error_t gs_function_scheduler_destroy(gs_function_scheduler_t * scheduler)
{
    GS_CHECK_HANDLE(scheduler);
    free(scheduler->entries);
    free(scheduler);
    return GS_OK;
}

gs_error_t gs_function_scheduler_register_ms(gs_function_scheduler_t * scheduler,
                                             uint32_t first_timeout_ms, gs_function_scheduler_function_t func, void * user_data)
{
    GS_CHECK_HANDLE(scheduler != NULL);
    GS_CHECK_ARG(func != NULL);

    gs_function_scheduler_entry_t * entry = scheduler->entries;
    for (unsigned int i = 0; i < scheduler->max_entries; ++i, ++entry) {
        if (entry->function == NULL) {
            entry->function = func;
            entry->user_data = user_data;
            entry->timeout_ms = first_timeout_ms;
            entry->last_exec_ms = gs_time_rel_ms();
            return GS_OK;
        }
    }

    return GS_ERROR_FULL;
}

int gs_function_scheduler_execute_ms(gs_function_scheduler_t * scheduler)
{
    uint32_t timeout_ms = 5000; // max timeout to ensure gs_time_rel_ms() works correctly (wrapping more than once is bad)

    if (scheduler) {
        timeout_ms = scheduler->max_timeout_ms;
        uint32_t now_ms = gs_time_rel_ms();

        gs_function_scheduler_entry_t * entry = scheduler->entries;
        for (unsigned int i = 0; i < scheduler->max_entries; ++i, ++entry) {
            if (entry->function) {
                uint32_t elapsed = gs_time_diff_ms(entry->last_exec_ms, now_ms);
                if (elapsed >= entry->timeout_ms) {
                    entry->timeout_ms = (entry->function)(entry->user_data);
                    entry->last_exec_ms = now_ms = gs_time_rel_ms();
                    elapsed = 0;
                }
                timeout_ms = gs_min(timeout_ms, (entry->timeout_ms - elapsed));
            }
        }
    }

    return (int)((timeout_ms < INT_MAX) ? timeout_ms : INT_MAX);
}
