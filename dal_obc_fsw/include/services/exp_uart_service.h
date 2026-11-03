#ifndef DAL_EXP_UART_SERVICE_H
#define DAL_EXP_UART_SERVICE_H

#include <stddef.h>
#include <stdint.h>

/* Status 0=success, 1=EXP/serial error, 2=timeout, 3=unconfigured,
 * 4=output too large. Output is NUL-terminated for local consumers. */
uint8_t exp_uart_command(const char *command, char *output, size_t capacity);
/* Read an MCU RPARAM table through the UART0 CLI's param_show command. */
uint8_t exp_uart_param_show(unsigned table, char *output, size_t capacity);
/* Return a recent background sample for a read-only wire-status command. */
uint8_t exp_uart_cached_command(const char *command, char *output, size_t capacity);
uint8_t exp_uart_telemetry(const char *section, char *output, size_t capacity);
void exp_uart_service_start(void);
void exp_uart_service_stop(void);
size_t exp_uart_snapshot(char *output, size_t capacity);

#endif
