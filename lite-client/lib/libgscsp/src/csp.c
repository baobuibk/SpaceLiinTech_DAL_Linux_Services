/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */

#include <gs/csp/csp.h>
#include <gs/csp/log.h>
#include <gs/util/check.h>
#include "local.h"

void gs_csp_conf_get_defaults_embedded(gs_csp_conf_t * conf)
{
    static const gs_csp_conf_t defaults = {
        .use_gs_log = true,
        .use_command_line_options = true,
        .csp_buffer_size = 256,  // typical MTU size is 256
        .csp_buffers = 10, // in case of RDP connections, must be > RDP Windows size
        .address = 1,
        .hostname = "hostname",
        .model = "model",
        .revision = "revision",
    };

    *conf = defaults;

#if GS_CSP_COMMAND_LINE_SUPPORT
    conf->address = gs_csp_command_line_get_address();
#endif
}

void gs_csp_conf_get_defaults_server(gs_csp_conf_t * conf)
{
    gs_csp_conf_get_defaults_embedded(conf);
    conf->csp_buffer_size = 512;
    conf->csp_buffers = 400;
}

gs_error_t gs_csp_init(const gs_csp_conf_t * conf)
{
    GS_CHECK_ARG(conf != NULL);

    if (conf->use_gs_log) {
        gs_csp_log_init();
    }

    int res = csp_buffer_init(conf->csp_buffers, conf->csp_buffer_size);
    if (res != CSP_ERR_NONE) {
        log_error("%s: csp_buffer_init(buffers: %u, size: %u) failed, CSP error: %d, error: %d",
                  __FUNCTION__, (unsigned int) conf->csp_buffers, (unsigned int) conf->csp_buffer_size, res, gs_csp_error(res));
        return gs_csp_error(res);
    }

    csp_set_hostname(conf->hostname);
    csp_set_model(conf->model);
    csp_set_revision(conf->revision);

    uint8_t csp_address = conf->address;
#if GS_CSP_COMMAND_LINE_SUPPORT
    if (gs_csp_command_line_is_address_set()) {
        csp_address = gs_csp_command_line_get_address();
    }
#endif

    res = csp_init(csp_address);
    if (res != CSP_ERR_NONE) {
        log_error("%s: csp_init(address: %u) failed, CSP error: %d, error: %d",
                  __FUNCTION__, conf->address, res, gs_csp_error(res));
        return gs_csp_error(res);
    }

#if GS_CSP_COMMAND_LINE_SUPPORT
    if (conf->use_command_line_options) {
        gs_error_t error = gs_csp_command_line_configure_interfaces();
        if (error) {
            log_error("%s: gs_csp_command_line_configure_interfaces() failed, error: %d",
                      __FUNCTION__, error);
            return error;
        }
    }
#endif

    return GS_OK;
}

bool gs_csp_is_address_valid(uint8_t address)
{
    if (address < 1) {
        return false;
    }
    if (address >= 33) {
        return false;
    }
    return true;
}
