#ifndef FLIGHT_CONSOLE_SERVICE_H
#define FLIGHT_CONSOLE_SERVICE_H

#include <stdbool.h>
#include "utils/error.h"

xerr_t flight_console_service_init(void);
xerr_t flight_console_service_start(void);
void flight_console_service_stop(void);
bool flight_console_service_is_running(void);

#endif
