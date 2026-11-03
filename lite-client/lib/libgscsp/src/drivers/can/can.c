/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <gs/csp/drivers/can/can.h>

#include <csp/csp.h>
#include <csp/interfaces/csp_if_can.h>
#include <gs/util/error.h>
#include <gs/util/check.h>
#include <gs/util/log.h>
#include <gs/util/drivers/can/can.h>
#include <gs/embed/drivers/can/can.h>
#include <gs/embed/freertos.h>

#define NO_OF_CAN_CHANNELS 2
#define MAX_NAME_LENGTH 10 // It says in csp_types.h, that it should be below 10 bytes

// change default log group
#define LOG_DEFAULT gs_can_log

typedef struct {
    // self reference device handle
    uint8_t can_ch;
    // CSP interface name
    char interface_name[MAX_NAME_LENGTH];
    // CSP interface
    csp_iface_t interface;
} gs_csp_can_interface_t;

static gs_csp_can_interface_t csp_can_interfaces[NO_OF_CAN_CHANNELS];

static void gs_csp_can_rxdata_callback_isr(int hdl,
                                           uint32_t canMsgId,
                                           bool extendedMsgId,
                                           const void * data,
                                           size_t data_size,
                                           uint32_t nowMs,
                                           void * user_data,
                                           gs_context_switch_t * cswitch)
{
    csp_can_rx(&csp_can_interfaces[hdl].interface, canMsgId, data, data_size, &cswitch->task_woken);
}

// Required by libcsp
int csp_can_tx_frame(csp_iface_t *interface, uint32_t id, const uint8_t * data, uint8_t dlc)
{
    return gs_can_send_extended(((gs_csp_can_interface_t *)interface->driver)->can_ch, id, data, dlc, 1000);
}

gs_error_t gs_csp_can_init2(uint8_t device, uint8_t csp_addr, uint32_t mtu, const char * name, bool set_default_route, csp_iface_t ** csp_if)
{
    GS_CHECK_HANDLE(device < NO_OF_CAN_CHANNELS);
    gs_csp_can_interface_t * interface = &csp_can_interfaces[device];

    // Register/subscribe to CAN frames for CSP
    const uint32_t can_id = CFP_MAKE_DST(csp_get_address());
    const uint32_t can_mask = CFP_MAKE_DST((1 << CFP_HOST_SIZE) - 1);

    log_debug("%s(%u): id=0x%" PRIx32 ", mask=0x%" PRIx32, __FUNCTION__, device, can_id, can_mask);

    if (gs_string_empty(name)) {
        name = GS_CSP_CAN_DEFAULT_IF_NAME;
    }
    if (strlen(name) >= MAX_NAME_LENGTH) {
        return GS_ERROR_ARG;
    }

    if (csp_iflist_get_by_name(name)) {
        log_error("%s(%u): name: [%s] - already exists", __FUNCTION__, device, name);
        return GS_ERROR_EXIST;
    }

    // hook CAN into CSP
    GS_STRNCPY(interface->interface_name, name);
    interface->interface.name = interface->interface_name;
    interface->interface.nexthop = csp_can_tx;
    interface->interface.mtu = mtu;
    interface->interface.driver = interface;

    csp_iflist_add(&interface->interface);

    if (csp_if) {
        *csp_if = &interface->interface;
    }

    gs_error_t error = gs_can_set_extended_filter_mask(0, can_id, can_mask, gs_csp_can_rxdata_callback_isr, NULL);
    if (error) {
        log_error("%s: gs_can_set_extended_filter_mask() failed, error: %s", __FUNCTION__, gs_error_string(error));
        return error;
    }

    error = gs_can_start(device);
    if (error) {
        log_error("%s: gs_can_start() failed, error: %s", __FUNCTION__, gs_error_string(error));
        return error;
    }

    if (set_default_route) {
        // Route all to CAN
        csp_rtable_set(0, 0, &interface->interface, CSP_NODE_MAC);
    }

    return GS_OK;
}

gs_error_t gs_csp_can_init(uint8_t device, uint8_t csp_addr, uint32_t mtu, const char * name, csp_iface_t ** csp_if)
{
    return gs_csp_can_init2(device, csp_addr, mtu, name, true, csp_if);
}
