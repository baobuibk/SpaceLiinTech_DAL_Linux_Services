/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
   @file

   Required by libcsp.
   Proto-typed in ./libcsp/include/csp/arch/csp_clock.h, but with different argumet!

   __attribute__((weak)) extern void clock_get_time(csp_timestamp_t * time);
   __attribute__((weak)) extern void clock_set_time(csp_timestamp_t * time);
*/

#include <csp/arch/csp_clock.h>
#include <gs/util/clock.h>

void clock_get_time(csp_timestamp_t * time)
{
    gs_clock_get_time((gs_timestamp_t*)time);
}

void clock_set_time(csp_timestamp_t * time)
{
    gs_clock_set_time((gs_timestamp_t*)time);
}
