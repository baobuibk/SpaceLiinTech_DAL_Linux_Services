#ifndef FP_CLIENT_H
#define FP_CLIENT_H
/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */
/**
   @file

   Flight planner - client interface.
*/

#include <gs/fp/fp_timer.h>
#include <gs/util/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   Command line initialisation.
*/
void cmd_fp_setup(void);

/**
   General Flight Planner functions
   @{
*/

/**
   Set which node and port the flight planner server is listening on

   @param[in] node csp node
   @param[in] port csp port number
   @return_gs_error_t
*/
gs_error_t fp_client_host(uint8_t node, uint8_t port);

/**
   Flush current flight plan.
   This deletes all sequences, timers, triggers and dynamic events.
   @return_gs_error_t
*/
gs_error_t fp_client_fp_flush(void);

/**
   Store current flight plan to file.

   @param[in] filename Path where the flight plan should be stored.
   @return_gs_error_t
*/
gs_error_t fp_client_fp_store(const char * filename);

/**
   Load flight plan from file.
   This flushes the current flight plan.

   @param[in] filename Path form where the flight plan should be loaded.
   @return_gs_error_t
*/
gs_error_t fp_client_fp_load(const char * filename);

/**@}*/

/**
   Timer functions.
   @{
*/

/**
   Create new timer.

   @param[in] key Timer to create.
   @param[in] cmd Cmd to trigger when timer expires.
   @param[in] basis Must either be FP_TIME_ABSOLUTE or FP_TIME_INTERVAL.
   @param[in] state Initial sate of timer FP_TIME_ACTIVE or FP_TIME_DORMANT
   @param[in] when Initial timer value.
   @param[in] repeat Number of repetitions. Use FP_TIME_UNLIMITED for recurring timer.
   @return_gs_error_t
*/
gs_error_t fp_client_timer_create(const char * key, const char * cmd, fp_timer_basis_t basis, fp_timer_state_t state, gs_timestamp_t *when, uint16_t repeat);

/**
   Delete timer.

   @param[in] key Timer to delete.
   @return_gs_error_t
*/
gs_error_t fp_client_timer_delete(const char * key);

/**
   Set timer active.

   @param[in] key Timer to activate.
   @return_gs_error_t
*/
gs_error_t fp_client_timer_set_active(const char * key);

/**
   Set timer dormant.

   @param[in] key Timer to set dormant.
   @return_gs_error_t
*/
gs_error_t fp_client_timer_set_dormant(const char * key);

gs_error_t fp_client_timer_set_repeat(const char * key, uint16_t count);
gs_error_t fp_client_timer_set_time(const char * key, fp_timer_basis_t basis, const gs_timestamp_t * when);
gs_error_t fp_client_timer_set_all_active(void);

/**
   List current timers on stdout.
   @return_gs_error_t
*/
gs_error_t fp_client_timer_list(void);

/**@}*/

/**
   Register commands.
   @return_gs_error_t
*/
gs_error_t gs_fp_register_commands(void);

/**
   Register commands.
   @deprecated use gs_fp_register_commands()
   @return_gs_error_t
*/
static inline gs_error_t fp_client_register_commands(void)
{
    return gs_fp_register_commands();
}
    
#ifdef __cplusplus
}
#endif
#endif
