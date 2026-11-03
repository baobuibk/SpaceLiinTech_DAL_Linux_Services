/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <gs/fp/fp_client.h>

#define GS_FP_INTERNAL_USE 1
#include <gs/fp/internal/types.h>

#include <gs/util/error.h>
#include <gs/util/stdio.h>
#include <gs/util/string.h>
#include <csp/csp.h>
#include <csp/csp_endian.h>
#include <inttypes.h>

#define FP_CLIENT_TIMEOUT 10000

static uint8_t fp_node = 1;
static uint8_t fp_port = 18;

static gs_error_t fp_client_send(fp_server_packet_t *p, int outlen)
{
    fp_server_packet_t reply;

    int ret = csp_transaction(CSP_PRIO_NORM, fp_node, fp_port, FP_CLIENT_TIMEOUT, p, outlen, &reply, FP_SERVER_PACKET_EMPTY);

    /* Validate reply */
    if (ret != FP_SERVER_PACKET_EMPTY) {
        return GS_ERROR_UNKNOWN;
    }

    if (reply.reply != FP_REPLY_OK) {
        return GS_ERROR_UNKNOWN;
    }

    return GS_OK;
}

gs_error_t fp_client_host(uint8_t node, uint8_t port)
{
    fp_node = node;
    fp_port = port;
    return GS_OK;
}

gs_error_t fp_client_fp_flush(void)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_FLUSH_FP;

    return fp_client_send(&p, FP_SERVER_PACKET_EMPTY);
}

gs_error_t fp_client_fp_store(const char * filename)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_STORE_FP;
    strcpy(p.fp.store.path, filename);

    return fp_client_send(&p, FP_SERVER_PACKET_SIZE(fp_server_store_fp_t));
}

gs_error_t fp_client_fp_load(const char * filename)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_LOAD_FP;
    strcpy(p.fp.load.path, filename);

    return fp_client_send(&p, FP_SERVER_PACKET_SIZE(fp_server_load_fp_t));
}

gs_error_t fp_client_timer_create(const char * key, const char * cmd, fp_timer_basis_t basis, fp_timer_state_t state, gs_timestamp_t *when, uint16_t repeat)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_CREATE;
    strcpy(p.timer.create.timer, key);
    strcpy(p.timer.create.cmd, cmd);
    p.timer.create.repeat = csp_hton16(repeat);
    p.timer.create.basis = basis;
    p.timer.create.state = state;
    p.timer.create.when.tv_sec = csp_hton32(when->tv_sec);
    p.timer.create.when.tv_nsec = csp_hton32(when->tv_nsec);

    return fp_client_send(&p, FP_SERVER_PACKET_SIZE(fp_server_timer_create_t));
}

gs_error_t fp_client_timer_delete(const char * key)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_DELETE;
    strcpy(p.timer.delete.timer, key);

    return fp_client_send(&p,FP_SERVER_PACKET_SIZE(fp_server_timer_delete_t));
}

gs_error_t fp_client_timer_set_active(const char * key)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_SET_ACTIVE;
    strcpy(p.timer.active.timer, key);

    return fp_client_send(&p, FP_SERVER_PACKET_SIZE(fp_server_timer_active_t));
}

gs_error_t fp_client_timer_set_all_active(void)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_SET_ALL_ACTIVE;

    return fp_client_send(&p, FP_SERVER_PACKET_EMPTY);
}

gs_error_t fp_client_timer_set_dormant(const char * key)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_SET_DORMANT;
    strcpy(p.timer.dormant.timer, key);

    return fp_client_send(&p, FP_SERVER_PACKET_SIZE(fp_server_timer_dormant_t));
}

gs_error_t fp_client_timer_set_repeat(const char * key, uint16_t count)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_SET_REPEAT;
    strcpy(p.timer.repeat.timer, key);
    p.timer.repeat.repeat = csp_hton16(count);

    return fp_client_send(&p, FP_SERVER_PACKET_SIZE(fp_server_timer_set_repeat_t));
}

gs_error_t fp_client_timer_set_time(const char * key, fp_timer_basis_t basis, const gs_timestamp_t *when)
{
    fp_server_packet_t p;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_SET_TIME;
    strcpy(p.timer.time.timer, key);
    p.timer.time.basis = basis;
    p.timer.time.when.tv_sec = csp_hton32(when->tv_sec);
    p.timer.time.when.tv_nsec = csp_hton32(when->tv_nsec);

    return fp_client_send(&p, FP_SERVER_PACKET_SIZE(fp_server_timer_set_time_t));
}

gs_error_t fp_client_timer_list(void)
{
    fp_server_packet_t p, *pp;
    p.type = FP_TYPE_REQUEST;
    p.cmd = FP_CMD_TIMER_LIST;

    csp_conn_t * conn = csp_connect(CSP_PRIO_NORM, fp_node, fp_port, 10000, 0);
    if (conn == NULL) {
        return GS_ERROR_UNKNOWN;
    }

    csp_transaction_persistent(conn, 10000, &p, FP_SERVER_PACKET_EMPTY, NULL, 0);

    /* Wait for replies */
    csp_packet_t * pkg = NULL;
    int replied = 0;
    while ((pkg = csp_read(conn, 10000)) != NULL) {
        pp = (fp_server_packet_t *)pkg->data;

        uint16_t total = csp_ntoh16(pp->timer.list.total);
        uint16_t seq = csp_ntoh16(pp->timer.list.seq);

        if (total != 0) {
            gs_timestamp_t w;

            /* Print header */
            if (replied == 0) {
                replied = 1;
                printf("Timer     Act Basis When       Repeat Remain Event\r\n");
            }

            fp_timer_state_t state = pp->timer.list.state;
            fp_timer_basis_t basis = pp->timer.list.basis;
            uint16_t repeat = csp_ntoh16(pp->timer.list.repeat);
            uint16_t remain = csp_ntoh16(pp->timer.list.remain);
            w.tv_sec = csp_ntoh32(pp->timer.list.when.tv_sec);
            w.tv_nsec = csp_ntoh32(pp->timer.list.when.tv_nsec);

            uint32_t when;
            when = w.tv_sec;

            printf("%-9s %s   %s   %-10"PRIu32" %-6"PRIu16" %-6"PRIu16 " %s\r\n",
                   pp->timer.list.timer, state == FP_TIME_ACTIVE ? "Y" : "N",
                   basis == FP_TIME_ABSOLUTE ? "Abs" : "Rel", when, repeat,
                   remain, pp->timer.list.cmd);
        } else {
            printf("No timers in list\r\n");
        }

        csp_buffer_free(pkg);

        if (total == 0 || seq == total) {
            break;
        }
    }

    csp_close(conn);

    if (pkg == NULL) {
        return GS_ERROR_UNKNOWN;
    }

    return GS_OK;
}
