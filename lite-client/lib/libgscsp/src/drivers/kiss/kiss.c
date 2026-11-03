/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

#include <gs/embed/drivers/uart/uart.h>
#include <csp/csp.h>
#include <gs/csp/drivers/kiss/kiss.h>

static csp_iface_t csp_if_kiss;
static uint8_t uart_csp_device;

static void usart_rx_callback(void * user_data, const uint8_t * data, size_t data_size, gs_context_switch_t * cswitch)
{
    csp_kiss_rx(&csp_if_kiss, (uint8_t *)data, data_size, cswitch);
}

static void csp_kiss_putc(char c)
{
    gs_uart_write(uart_csp_device, -1, c);
}

static void csp_kiss_discard(char c, void *pxTaskWoken)
{
    // Do nothing with discarded characters
}

gs_error_t gs_csp_kiss_init(uint8_t device)
{
    static csp_kiss_handle_t csp_kiss_driver;
    static const char * kiss_name = "KISS";
    csp_route_set(CSP_DEFAULT_ROUTE, &csp_if_kiss, CSP_NODE_MAC);

    csp_kiss_init(&csp_if_kiss, &csp_kiss_driver, csp_kiss_putc, csp_kiss_discard, kiss_name);

    uart_csp_device = device;

    return gs_uart_set_rx_callback(device, usart_rx_callback, NULL);
}
