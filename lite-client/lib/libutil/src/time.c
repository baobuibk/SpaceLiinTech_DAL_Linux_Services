/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <gs/util/time.h>

uint32_t gs_time_diff_ms(uint32_t ref_ms, uint32_t now_ms)
{
    if (now_ms >= ref_ms) {
        return (now_ms - ref_ms);
    }

    // assuming time wrapped at max uint32_t
    return ((UINT32_MAX - ref_ms) + now_ms);
}

bool gs_time_sleep_until_ms(uint32_t * ref_ms, uint32_t sleep_ms)
{
    const uint32_t now = gs_time_rel_ms();
    *ref_ms += sleep_ms; // this is expected to be in the future
    uint32_t ms = gs_time_diff_ms(now, *ref_ms);
    if (ms > sleep_ms) {
        // we are behind - catch up, could be bad seed or too long processing
        *ref_ms = now;
        gs_time_sleep_ms(0); // yield - let others have a go
        return true;
    }
    gs_time_sleep_ms(ms);
    return false;
}
