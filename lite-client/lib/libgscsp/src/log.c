/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

#include <gs/csp/log.h>
#include <gs/util/log/log.h>
#include <csp/csp.h>
#include "local.h"

GS_LOG_GROUP(gs_csp_log, "csp", GS_LOG_CAT_CSP, LOG_DEFAULT_MASK);

static void gs_log_csp_debug_hook(csp_debug_level_t level, const char *format, va_list args)
{
    gs_log_level_t mapped_level;

    switch (level) {
	/* Regular log levels */
	default:
	case CSP_ERROR:
            mapped_level = LOG_ERROR;
            break;
	case CSP_WARN:
            mapped_level = LOG_WARNING;
            break;
	case CSP_INFO:
            mapped_level = LOG_INFO;
            break;
            /* Extended log levels */
	case CSP_BUFFER:
            mapped_level = LOG_TRACE;
            break;
	case CSP_PACKET:
            mapped_level = LOG_INFO;
            break;
	case CSP_PROTOCOL:
            mapped_level = LOG_DEBUG;
            break;
	case CSP_LOCK:
            mapped_level = LOG_TRACE;
            break;
    }

    const int do_log = ((LOG_DEFAULT->mask & (1 << mapped_level)) > 0);

    if (do_log) {
        /* forward to log system */
        gs_log_va(mapped_level, LOG_DEFAULT, format, args);
    }
}

gs_error_t gs_csp_log_init(void)
{
    gs_log_group_register(LOG_DEFAULT);

    csp_debug_set_level(CSP_ERROR,    true);
    csp_debug_set_level(CSP_WARN,     true);
    csp_debug_set_level(CSP_INFO,     true);
    csp_debug_set_level(CSP_BUFFER,   true);
    csp_debug_set_level(CSP_PACKET,   true);
    csp_debug_set_level(CSP_PROTOCOL, true);
    csp_debug_set_level(CSP_LOCK,     true);

    csp_debug_hook_set(gs_log_csp_debug_hook);

    return GS_OK;
}
