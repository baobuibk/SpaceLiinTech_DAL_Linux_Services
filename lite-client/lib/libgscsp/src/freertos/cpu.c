/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

#include <gs/embed/drivers/sys/reset.h>

void cpu_reset(void)
{
    gs_sys_reset(GS_SYS_RESET_CSP);
}
