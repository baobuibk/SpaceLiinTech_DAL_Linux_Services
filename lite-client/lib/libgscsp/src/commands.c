/* Copyright (c) 2013-2017 GomSpace A/S. All rights reserved. */

#include <math.h>
#include <stdlib.h>
#include <limits.h>

#include <gs/csp/error.h>

#include <csp/csp.h>
#include <csp/csp_cmp.h>
#include <csp/csp_endian.h>
#include <gs/util/string.h>
#include <gs/util/gosh/command.h>
#include <gs/util/hexdump.h>
#include <gs/util/base16.h>
#include <gs/util/log.h>
#include <gs/util/clock.h>

static gs_error_t parse_node_timeout(gs_command_context_t *ctx, int node_index, uint8_t * node, int timeout_index, uint32_t * timeout)
{
    gs_error_t error = GS_OK;
    if (node) {
        *node = csp_get_address();

        if (ctx->argc > node_index) {
            error = gs_string_to_uint8(ctx->argv[node_index], node);
        }
    }

    if (timeout && (error == GS_OK)) {
        *timeout = 1000;

        if (ctx->argc > timeout_index) {
            error = gs_string_to_uint32(ctx->argv[timeout_index], timeout);
        }
    }

    return error;
}

static int cmd_ping(gs_command_context_t *ctx)
{
    uint8_t node;
    uint32_t timeout;
    int res = parse_node_timeout(ctx, 1, &node, 2, &timeout);
    if (res) {
        return res;
    }

    uint32_t size = 1;
    if ((ctx->argc > 3) && (gs_string_to_uint32(ctx->argv[3], &size) != GS_OK)) {
        return GS_ERROR_ARG;
    }

    uint32_t options = CSP_O_NONE;
    if (ctx->argc > 4) {
        const char * features = ctx->argv[4];
        if (strchr(features, 'r'))
            options |= CSP_O_RDP;
        if (strchr(features, 'x'))
            options |= CSP_O_XTEA;
        if (strchr(features, 'h'))
            options |= CSP_O_HMAC;
        if (strchr(features, 'c'))
            options |= CSP_O_CRC32;
    }

    printf("Ping node %u, timeout %" PRIu32 ", size %" PRIu32 ": options: 0x%" PRIx32 " ... ", node, timeout, size, options);

    const uint64_t start = gs_clock_get_nsec();
    const int time = csp_ping(node, timeout, size, options);
    const uint64_t stop = gs_clock_get_nsec();
    const float elapsed = (((float)(stop - start)) / 1E6);
    if (time < 0) {
        printf("timeout after %.03f ms\r\n", elapsed);
        return GS_ERROR_TIMEOUT;
    }

    printf("reply in %.03f ms\r\n", elapsed);

    return GS_OK;
}

static int cmd_ps(gs_command_context_t *ctx)
{
    uint8_t node;
    uint32_t timeout;
    int res = parse_node_timeout(ctx, 1, &node, 2, &timeout);
    if (res) {
        return res;
    }

    csp_ps(node, timeout);

    return GS_OK;
}

static int cmd_memfree(gs_command_context_t *ctx)
{
    uint8_t node;
    uint32_t timeout;
    int res = parse_node_timeout(ctx, 1, &node, 2, &timeout);
    if (res) {
        return res;
    }

    csp_memfree(node, timeout);

    return GS_OK;
}

static int cmd_reboot(gs_command_context_t *ctx)
{
    if (ctx->argc < 2) {
        return GS_ERROR_ARG;
    }
    uint8_t node;
    int res = parse_node_timeout(ctx, 1, &node, 0, NULL);
    if (res) {
        return res;
    }

    csp_reboot(node);

    return GS_OK;
}

static int cmd_shutdown(gs_command_context_t *ctx)
{
    if (ctx->argc < 2) {
        return GS_ERROR_ARG;
    }
    uint8_t node;
    int res = parse_node_timeout(ctx, 1, &node, 0, NULL);
    if (res) {
        return res;
    }

    csp_shutdown(node);

    return GS_OK;
}

static int cmd_buf_free(gs_command_context_t *ctx)
{
    uint8_t node;
    uint32_t timeout;
    int res = parse_node_timeout(ctx, 1, &node, 2, &timeout);
    if (res) {
        return res;
    }

    csp_buf_free(node, timeout);

    return GS_OK;
}

static int cmd_uptime(gs_command_context_t *ctx)
{
    uint8_t node;
    uint32_t timeout;
    int res = parse_node_timeout(ctx, 1, &node, 2, &timeout);
    if (res) {
        return res;
    }

    csp_uptime(node, timeout);

    return GS_OK;
}

#ifdef CSP_DEBUG

static int cmd_csp_route_print_table(gs_command_context_t *ctx)
{
    csp_route_print_table();
    return GS_OK;
}

static int cmd_csp_route_print_interfaces(gs_command_context_t *ctx)
{
    csp_route_print_interfaces();
    return GS_OK;
}

static int cmd_csp_conn_print_table(gs_command_context_t *ctx)
{
    csp_conn_print_table();
    return GS_OK;
}

#endif

#if CSP_USE_RDP
static int cmd_csp_rdp_set_opt(gs_command_context_t *ctx)
{
    if (ctx->argc < 7) {
        return GS_ERROR_ARG;
    }
    int res;
    uint32_t window_size;
    if ((res = gs_string_to_uint32(ctx->argv[1], &window_size))) {
        return res;
    }
    uint32_t conn_timeout;
    if ((res = gs_string_to_uint32(ctx->argv[2], &conn_timeout))) {
        return res;
    }
    uint32_t packet_timeout;
    if ((res = gs_string_to_uint32(ctx->argv[3], &packet_timeout))) {
        return res;
    }
    uint32_t delayed_acks;
    if ((res = gs_string_to_uint32(ctx->argv[4], &delayed_acks))) {
        return res;
    }
    uint32_t ack_timeout;
    if ((res = gs_string_to_uint32(ctx->argv[5], &ack_timeout))) {
        return res;
    }
    uint32_t ack_delay_count;
    if ((res = gs_string_to_uint32(ctx->argv[6], &ack_delay_count))) {
        return res;
    }

    printf("Setting arguments to: window size %" PRIu32 ", conn timeout %" PRIu32 ", packet timeout %" PRIu32 ", delayed acks %" PRIu32 ", ack timeout %" PRIu32 ", ack delay count %" PRIu32 "\r\n",
           window_size, conn_timeout, packet_timeout, delayed_acks, ack_timeout, ack_delay_count);

    csp_rdp_set_opt(window_size, conn_timeout, packet_timeout, delayed_acks, ack_timeout, ack_delay_count);

    return GS_OK;
}
#endif

static int cmd_cmp_ident(gs_command_context_t *ctx)
{
    uint8_t node;
    uint32_t timeout;
    int ret = parse_node_timeout(ctx, 1, &node, 2, &timeout);
    if (ret) {
        return ret;
    }

    struct csp_cmp_message msg;

    ret = csp_cmp_ident(node, timeout, &msg);
    if (ret != CSP_ERR_NONE) {
        printf("CSP error: %d\r\n", ret);
        return gs_csp_error(ret);;
    }

    printf("Hostname: %s\r\n", msg.ident.hostname);
    printf("Model:    %s\r\n", msg.ident.model);
    printf("Revision: %s\r\n", msg.ident.revision);
    printf("Date:     %s\r\n", msg.ident.date);
    printf("Time:     %s\r\n", msg.ident.time);

    return GS_OK;
}

static int cmd_cmp_route_set(gs_command_context_t *ctx)
{
    if (ctx->argc != 6)
        return GS_ERROR_ARG;

    uint8_t node = atoi(ctx->argv[1]);
    uint32_t timeout = atoi(ctx->argv[2]);
    printf("Sending route_set to node %"PRIu8" timeout %"PRIu32"\r\n", node, timeout);

    struct csp_cmp_message msg;
    msg.route_set.dest_node = atoi(ctx->argv[3]);
    msg.route_set.next_hop_mac = atoi(ctx->argv[4]);
    strncpy(msg.route_set.interface, ctx->argv[5], 10);
    printf("Dest_node: %u, next_hop_mac: %u, interface %s\r\n", msg.route_set.dest_node, msg.route_set.next_hop_mac, msg.route_set.interface);

    int ret = csp_cmp_route_set(node, timeout, &msg);
    if (ret != CSP_ERR_NONE) {
        printf("CSP error: %d\r\n", ret);
        return gs_csp_error(ret);
    }

    return GS_OK;
}

static int cmd_cmp_ifc(gs_command_context_t *ctx) {

    uint8_t node;
    uint32_t timeout;
    char * interface;

    if (ctx->argc > 4 || ctx->argc < 3)
        return GS_ERROR_ARG;

    node = atoi(ctx->argv[1]);
    interface = ctx->argv[2];

    if (ctx->argc < 4)
        timeout = 1000;
    else
        timeout = atoi(ctx->argv[3]);

    struct csp_cmp_message msg;
    strncpy(msg.if_stats.interface, interface, CSP_CMP_ROUTE_IFACE_LEN);

    printf("Requesting interface stats for interface %s\r\n", interface);

    int ret = csp_cmp_if_stats(node, timeout, &msg);
    if (ret != CSP_ERR_NONE) {
        printf("CSP error: %d\r\n", ret);
        return gs_csp_error(ret);
    }

    msg.if_stats.tx = csp_ntoh32(msg.if_stats.tx);
    msg.if_stats.rx = csp_ntoh32(msg.if_stats.rx);
    msg.if_stats.tx_error = csp_ntoh32(msg.if_stats.tx_error);
    msg.if_stats.rx_error = csp_ntoh32(msg.if_stats.rx_error);
    msg.if_stats.drop = csp_ntoh32(msg.if_stats.drop);
    msg.if_stats.autherr = csp_ntoh32(msg.if_stats.autherr);
    msg.if_stats.frame = csp_ntoh32(msg.if_stats.frame);
    msg.if_stats.txbytes = csp_ntoh32(msg.if_stats.txbytes);
    msg.if_stats.rxbytes = csp_ntoh32(msg.if_stats.rxbytes);
    msg.if_stats.irq = csp_ntoh32(msg.if_stats.irq);

    printf("%-5s   tx: %05"PRIu32" rx: %05"PRIu32" txe: %05"PRIu32" rxe: %05"PRIu32"\r\n"
           "        drop: %05"PRIu32" autherr: %05"PRIu32 " frame: %05"PRIu32"\r\n"
           "        txb: %"PRIu32" rxb: %"PRIu32"\r\n\r\n",
           msg.if_stats.interface, msg.if_stats.tx, msg.if_stats.rx, msg.if_stats.tx_error, msg.if_stats.rx_error, msg.if_stats.drop,
           msg.if_stats.autherr, msg.if_stats.frame, msg.if_stats.txbytes, msg.if_stats.rxbytes);

    return GS_OK;
}

static int cmd_cmp_peek(gs_command_context_t *ctx)
{
    if ((ctx->argc != 4) && (ctx->argc != 5))
        return GS_ERROR_ARG;

    uint8_t node;
    uint32_t timeout;
    int res = parse_node_timeout(ctx, 1, &node, 4, &timeout);
    if (res) {
        return res;
    }

    uint32_t addr;
    if (gs_string_to_uint32(ctx->argv[2], &addr)) {
        return GS_ERROR_ARG;
    }

    uint32_t len;
    if (gs_string_to_uint32(ctx->argv[3], &len)) {
        return GS_ERROR_ARG;
    }
    if (len > CSP_CMP_PEEK_MAX_LEN) {
        return GS_ERROR_RANGE;
    }

    printf("Dumping mem from node %u addr 0x%"PRIx32" len %"PRIx32" timeout %"PRIu32"\r\n", node, addr, len, timeout);

    struct csp_cmp_message msg;
    msg.peek.addr = csp_hton32(addr);
    msg.peek.len = len;

    int ret = csp_cmp_peek(node, timeout, &msg);
    if (ret != CSP_ERR_NONE) {
        printf("CSP error: %d\r\n", ret);
        return gs_csp_error(ret);
    }

    gs_hexdump_addr(msg.peek.data, len, GS_TYPES_UINT2PTR(addr));

    return GS_OK;
}

static int cmd_cmp_poke(gs_command_context_t *ctx)
{
    if ((ctx->argc != 4) && (ctx->argc != 5))
        return GS_ERROR_ARG;

    uint8_t node;
    uint32_t timeout;
    int res = parse_node_timeout(ctx, 1, &node, 4, &timeout);
    if (res) {
        return res;
    }

    uint32_t addr;
    if (gs_string_to_uint32(ctx->argv[2], &addr)) {
        return GS_ERROR_ARG;
    }

    unsigned char data[CSP_CMP_POKE_MAX_LEN];
    uint32_t len = base16_decode(ctx->argv[3], data);
    if (len > CSP_CMP_PEEK_MAX_LEN) {
        printf("Max length is: %u\r\n", CSP_CMP_PEEK_MAX_LEN);
        return GS_ERROR_RANGE;
    }

    printf("Writing to mem at node %u addr 0x%"PRIx32" len %"PRIx32" timeout %"PRIu32"\r\n", node, addr, len, timeout);
    gs_hexdump_addr(data, len, GS_TYPES_UINT2PTR(addr));

    struct csp_cmp_message msg;
    msg.poke.addr = csp_hton32(addr);
    msg.poke.len = len;
    memcpy(msg.poke.data, data, CSP_CMP_POKE_MAX_LEN);

    int ret = csp_cmp_poke(node, timeout, &msg);
    if (ret != CSP_ERR_NONE) {
        printf("CSP error: %d\r\n", ret);
        return gs_csp_error(ret);
    }

    return GS_OK;
}

static int cmd_cmp_clock(gs_command_context_t *ctx, uint32_t node, uint32_t timeout, const gs_timestamp_t * set)
{
    char tbuf[GS_CLOCK_ISO8601_BUFFER_LENGTH];
    struct csp_cmp_message msg;
    memset(&msg, 0, sizeof(msg));

    if (set) {
        gs_clock_to_iso8601_string(set, tbuf, sizeof(tbuf));
        printf("Set time: %s (%" PRIu32 ".%09" PRIu32 " sec)\r\n", tbuf, set->tv_sec, set->tv_nsec);
        msg.clock.tv_sec = csp_hton32(set->tv_sec);
        msg.clock.tv_nsec = csp_hton32(set->tv_nsec);
    }

    gs_timestamp_t t1, t2;
    gs_clock_get_time(&t1);
    int ret = csp_cmp_clock(node, timeout, &msg);
    if (ret != CSP_ERR_NONE) {
        return gs_csp_error(ret);
    }
    gs_clock_get_time(&t2);

    /* Calculate round-trip time */
    const int64_t rtt = ((uint64_t)t2.tv_sec * 1000000000 + t2.tv_nsec) - ((uint64_t)t1.tv_sec * 1000000000 + t1.tv_nsec);

    gs_timestamp_t timestamp;
    timestamp.tv_sec = csp_ntoh32(msg.clock.tv_sec);
    timestamp.tv_nsec = csp_ntoh32(msg.clock.tv_nsec);

    gs_clock_to_iso8601_string(&timestamp, tbuf, sizeof(tbuf));
    printf("Get time: %s (%" PRIu32 ".%09" PRIu32 " sec)\r\n", tbuf, timestamp.tv_sec, timestamp.tv_nsec);

    /* Calculate time difference to local clock. This takes the round-trip
     * into account, but assumes a symmetrical link */
    const int64_t remote = (uint64_t)timestamp.tv_sec * 1000000000 + timestamp.tv_nsec;
    const int64_t local = (uint64_t)t1.tv_sec * 1000000000 + t1.tv_nsec + rtt / 2;

    const double diff = (remote - local) / 1000000.0;
    printf("Remote is %f ms %s local time\r\n", fabs(diff), diff > 0 ? "ahead of" : "behind");

    return GS_OK;
}

static int cmd_cmp_clock_get(gs_command_context_t *ctx)
{
    if (ctx->argc < 2) {
        return GS_ERROR_ARG;
    }

    uint32_t node;
    if (gs_string_to_uint32(ctx->argv[1], &node) != GS_OK) {
        return GS_ERROR_ARG;
    }

    uint32_t timeout = 1000;
    if (ctx->argc > 2) {
        if (gs_string_to_uint32(ctx->argv[2], &timeout) != GS_OK) {
            return GS_ERROR_ARG;
        }
    }

    return cmd_cmp_clock(ctx, node, timeout, NULL);
}

static int cmd_cmp_clock_set(gs_command_context_t *ctx)
{
    if (ctx->argc < 3) {
        return GS_ERROR_ARG;
    }

    uint32_t node;
    if (gs_string_to_uint32(ctx->argv[1], &node) != GS_OK) {
        return GS_ERROR_ARG;
    }

    gs_timestamp_t ts;
    if (gs_clock_from_string(ctx->argv[2], &ts) != GS_OK) {
        return GS_ERROR_ARG;
    }

    uint32_t timeout = 1000;
    if (ctx->argc > 3) {
        if (gs_string_to_uint32(ctx->argv[3], &timeout) != GS_OK) {
            return GS_ERROR_ARG;
        }
    }

    return cmd_cmp_clock(ctx, node, timeout, &ts);
}

static int cmd_cmp_clock_sync(gs_command_context_t *ctx)
{
    if (ctx->argc < 2) {
        return GS_ERROR_ARG;
    }

    uint32_t node;
    if (gs_string_to_uint32(ctx->argv[1], &node) != GS_OK) {
        return GS_ERROR_ARG;
    }

    uint32_t timeout = 1000;
    if (ctx->argc > 2) {
        if (gs_string_to_uint32(ctx->argv[2], &timeout) != GS_OK) {
            return GS_ERROR_ARG;
        }
    }

    gs_timestamp_t ts;
    gs_clock_get_time(&ts);

    return cmd_cmp_clock(ctx, node, timeout, &ts);
}

static const gs_command_t GS_COMMAND_SUB cmp_clock_commands[] = {
    {
        .name = "get",
        .help = "Get clock on <node>",
        .usage = "<node> [timeout]",
        .handler = cmd_cmp_clock_get,
    },
    {
        .name = "set",
        .help = "Set time of <node>",
        .usage = "<node> <sec.nsec|YYYY-MM-DDTHH:MM:SSZ> [timeout]",
        .handler = cmd_cmp_clock_set,
    },
    {
        .name = "sync",
        .help = "Sync/set time of <node> to time of this node",
        .usage = "<node> [timeout]",
        .handler = cmd_cmp_clock_sync,
    }
};

static const gs_command_t GS_COMMAND_SUB cmp_commands[] = {
    {
        .name = "ident",
        .help = "Node id",
        .usage = "[node] [timeout]",
        .handler = cmd_cmp_ident,
    },{
        .name = "route_set",
        .help = "Update table",
        .usage = "<node> <timeout> <addr> <mac> <ifstr>",
        .handler = cmd_cmp_route_set,
    },{
        .name = "ifc",
        .help = "Remote IFC",
        .usage = "<node> <interface> [timeout]",
        .handler = cmd_cmp_ifc,
    },{
        .name = "peek",
        .help = "Show remote memory",
        .usage = "<node> <addr> <len> [timeout]",
        .handler = cmd_cmp_peek,
    },{
        .name = "poke",
        .help = "Modify remote memory",
        .usage = "<node> <addr> <base16_data> [timeout]",
        .handler = cmd_cmp_poke,
    },{
        .name = "clock",
        .help = "Get/set clock",
        .chain = GS_COMMAND_INIT_CHAIN(cmp_clock_commands),
    }
};

static const gs_command_t GS_COMMAND_ROOT csp_commands[] = {
    {
        .name = "ping",
        .help = "csp: Ping",
        .usage = "[node] [timeout] [size] [opt: r|x|h|c]",
        .handler = cmd_ping,
    },{
        .name = "rps",
        .help = "csp: Remote ps",
        .usage = "[node] [timeout]",
        .handler = cmd_ps,
    },{
        .name = "memfree",
        .help = "csp: Memory free",
        .usage = "[node] [timeout]",
        .handler = cmd_memfree,
    },{
        .name = "buffree",
        .help = "csp: Buffer free",
        .usage = "[node] [timeout]",
        .handler = cmd_buf_free,
    },{
        .name = "reboot",
        .help = "csp: Reboot",
        .usage = "<node>",
        .handler = cmd_reboot,
    },{
        .name = "shutdown",
        .help = "csp: Shutdown",
        .usage = "<node>",
        .handler = cmd_shutdown,
    },{
        .name = "uptime",
        .help = "csp: Uptime",
        .usage = "[node] [timeout]",
        .handler = cmd_uptime,
    },{
        .name = "cmp",
        .help = "csp: Management",
        .chain = GS_COMMAND_INIT_CHAIN(cmp_commands),
    },
#ifdef CSP_DEBUG
    {
        .name = "route",
        .help = "csp: Show routing table",
        .handler = cmd_csp_route_print_table,
    },{
        .name = "ifc",
        .help = "csp: Show interfaces",
        .handler = cmd_csp_route_print_interfaces,
    },{
        .name = "conn",
        .help = "csp: Show connection table",
        .handler = cmd_csp_conn_print_table,
    },
#endif
#if CSP_USE_RDP
    {
        .name = "rdpopt",
        .help = "csp: Set RDP options",
        .handler = cmd_csp_rdp_set_opt,
        .usage = "<window size> <conn timeout> <packet timeout> <delayed ACKs> <ACK timeout> <ACK delay count>"
    },
#endif
};

gs_error_t gs_csp_register_commands(void)
{
    return GS_COMMAND_REGISTER(csp_commands);
}
