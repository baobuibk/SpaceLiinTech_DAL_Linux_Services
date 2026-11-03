#ifndef LIBGSCSP_INCLUDE_GS_CSP_ROUTER_H
#define LIBGSCSP_INCLUDE_GS_CSP_ROUTER_H
/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */
/**
   @file

   CSP router task, with support for stopping/terminating router task.
*/

#include <gs/util/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   Start CSP router (task/thread).
   @param[in] stack_size stack size in bytes, minimum 300 bytes.
   @param[in] priority task priority, normally GS_THREAD_PRIORITY_HIGH.
   @return_gs_error_t
*/
gs_error_t gs_csp_router_task_start(size_t stack_size, gs_thread_priority_t priority);

/**
   Stop CSP router task (for testing).

   Signal stop to the router and waits for it to terminate (join).
   @note Join is performed, which may hang forever if the router doesn't respond to the stop request.
   @return_gs_error_t
*/
gs_error_t gs_csp_router_task_stop(void);

#ifdef __cplusplus
}
#endif
#endif
