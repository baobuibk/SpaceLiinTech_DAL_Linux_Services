/* Copyright (c) 2013-2018 GomSpace A/S. All rights reserved. */
/**
 * NanoCam Client library
 */

#include <gs/cam/nanocam.h>

#include <inttypes.h>
#include <string.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <util/console.h>
#include <util/log.h>
#include <util/color_printf.h>
#include <gosh/getopt.h>

#include <csp/csp.h>
#include <csp/csp_endian.h>
#include <csp/csp_cmp.h>

/** Width of histogram */
#define HIST_WIDTH	50

/** Default timeout in ms */
static uint32_t cam_timeout = 5000;

/* Default store format is JPEG */
static uint8_t cam_format = NANOCAM_STORE_JPG;

static const char *format_to_string(uint8_t format)
{
    switch (format) {
	case NANOCAM_STORE_RAW:
            return "RAW";
	case NANOCAM_STORE_BMP:
            return "BMP";
	case NANOCAM_STORE_JPG:
            return "JPG";
	case NANOCAM_STORE_DNG:
            return "DNG";
	case NANOCAM_STORE_RAW10:
            return "RAW10";
	default:
            return "unknown";
    }
}

static uint8_t string_to_format(const char *format)
{
    if (!strcasecmp(format, "raw")) {
        return NANOCAM_STORE_RAW;
    } else if (!strcasecmp(format, "bmp")) {
        return NANOCAM_STORE_BMP;
    } else if (!strcasecmp(format, "jpg")) {
        return NANOCAM_STORE_JPG;
    } else if (!strcasecmp(format, "dng")) {
        return NANOCAM_STORE_DNG;
    } else if (!strcasecmp(format, "raw10")) {
        return NANOCAM_STORE_RAW10;
    } else {
        return atoi(format);
    }
}

int cmd_cam_node(struct command_context *ctx)
{
    if (ctx->argc > 2) {
        return CMD_ERROR_SYNTAX;
    } else if(ctx->argc < 2) {
        printf("CAM node = %d\n", nanocam_get_node());
    } else {
        nanocam_set_node(atoi(ctx->argv[1]));
    }

    return CMD_ERROR_NONE;
}

int cmd_cam_set_timeout(struct command_context *ctx)
{
    if (ctx->argc < 2) {
        printf("Current timeout is %"PRIu32"\n", cam_timeout);
        return CMD_ERROR_NONE;
    }

    if (sscanf(command_args(ctx), "%"SCNu32, &cam_timeout) != 1)
        return CMD_ERROR_SYNTAX;

    printf("Timeout set to %"PRIu32"\n", cam_timeout);

    return CMD_ERROR_NONE;
}

int cmd_cam_format(struct command_context *ctx)
{
    if (ctx->argc < 2) {
        printf("Current format is %s (%hhu)\n",
               format_to_string(cam_format), cam_format);
        return CMD_ERROR_NONE;
    }

    cam_format = string_to_format(ctx->argv[1]);

    printf("Format set to %s (%hhu)\n",
           format_to_string(cam_format), cam_format);

    return CMD_ERROR_NONE;
}

static color_printf_t hist_bin_color(bool red, bool green, bool blue)
{
    if (red && green && blue)
        return COLOR_WHITE;

    if (red && green)
        return COLOR_YELLOW;
    if (red && blue)
        return COLOR_MAGENTA;
    if (green && blue)
        return COLOR_CYAN;

    if (red)
        return COLOR_RED;
    if (green)
        return COLOR_GREEN;
    if (blue)
        return COLOR_BLUE;

    return COLOR_NONE;
}

int cmd_cam_snap(struct command_context *ctx)
{
    /* Argument parsing */
    const char *colors = "rgb";
    int c, remain, result;
    unsigned int i, j;
    bool histogram = true;

    cam_snap_t snap;
    cam_snap_reply_t reply;

    snap.count  = 1;
    snap.width  = 0;
    snap.height = 0;
    snap.delay  = 0;
    snap.flags  = 0;
    snap.format = cam_format;
    snap.left   = 0; // not used, initialize to zero
    snap.top    = 0; // not used, initialize to zero

    /* Histogram print */
    unsigned int bin_max = 0;
    bool r, g, b, show_r, show_g, show_b;

    while ((c = gosh_getopt(ctx, "ead:h:in:rstx")) != -1) {
        switch (c) {
            case 'e':
                snap.flags |= NANOCAM_SNAP_FLAG_NOEXPOSURE;
                break;
            case 'a':
                snap.flags |= NANOCAM_SNAP_FLAG_AUTO_GAIN;
                break;
            case 'd':
                snap.delay = atoi(ctx->optarg);
                break;
            case 'h':
                colors = ctx->optarg;
                break;
            case 'i':
                snap.flags |= NANOCAM_SNAP_FLAG_STORE_TAGS;
                break;
            case 'n':
                snap.count = atoi(ctx->optarg);
                break;
            case 'r':
                snap.flags |= NANOCAM_SNAP_FLAG_STORE_RAM;
                break;
            case 's':
                snap.flags |= NANOCAM_SNAP_FLAG_STORE_FLASH;
                break;
            case 't':
                snap.flags |= NANOCAM_SNAP_FLAG_STORE_THUMB;
                break;
            case 'x':
                histogram = false;
                snap.flags |= NANOCAM_SNAP_FLAG_NOHIST;
                break;
            case '?':
                return CMD_ERROR_SYNTAX;
        }
    }

    remain = ctx->argc - ctx->optind;
    int idx = ctx->optind;

    if (remain > 0)
        snap.width = atoi(ctx->argv[idx]);
    if (remain > 1)
        snap.height = atoi(ctx->argv[idx + 1]);

    if (snap.width || snap.height) {
        printf("Snapping %"PRIu16"x%"PRIu16" image, timeout %"PRIu32"\n",
               snap.width, snap.height, cam_timeout);
    } else {
        printf("Snapping full-size image, timeout %"PRIu32"\n", cam_timeout);
    }

    result = nanocam_snap(&snap, &reply, cam_timeout);
    if (result != NANOCAM_ERROR_NONE) {
        printf("Snap error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    printf("Snap result: %d\n", result);

    if (!histogram)
        return CMD_ERROR_NONE;

    /* Brightness */
    color_printf(COLOR_WHITE, "Brightness\n");
    color_printf(COLOR_NONE,  "  All   : %u%% (min/max/peak/avg %03hhu/%03hhu/%03hhu/%03hhu)\n",
                 reply.light_avg[0] * 100 / UINT8_MAX,
                 reply.light_min[0], reply.light_max[0],
                 reply.light_peak[0], reply.light_avg[0]);
    color_printf(COLOR_RED,   "  Red   : %u%% (min/max/peak/avg %03hhu/%03hhu/%03hhu/%03hhu)\n",
                 reply.light_avg[1] * 100 / UINT8_MAX,
                 reply.light_min[1], reply.light_max[1],
                 reply.light_peak[1], reply.light_avg[1]);
    color_printf(COLOR_GREEN, "  Green : %u%% (min/max/peak/avg %03hhu/%03hhu/%03hhu/%03hhu)\n",
                 reply.light_avg[2] * 100 / UINT8_MAX,
                 reply.light_min[2], reply.light_max[2],
                 reply.light_peak[2], reply.light_avg[2]);
    color_printf(COLOR_BLUE,  "  Blue  : %u%% (min/max/peak/avg %03hhu/%03hhu/%03hhu/%03hhu)\n",
                 reply.light_avg[3] * 100/ UINT8_MAX,
                 reply.light_min[3], reply.light_max[3],
                 reply.light_peak[3], reply.light_avg[3]);

    /* Histogram */
    color_printf(COLOR_WHITE, "Histogram\n");

    show_r = strchr(colors, 'r');
    show_g = strchr(colors, 'g');
    show_b = strchr(colors, 'b');

    /* Find max bin count */
    for (i = 0; i < NANOCAM_SNAP_HIST_BINS; i++) {
        for (j = 0; j < NANOCAM_SNAP_COLORS; j++) {
            if (reply.hist[j][i] > bin_max)
                bin_max = reply.hist[j][i];
        }
    }

    color_printf(COLOR_WHITE, "    Min +");
    for (j = 0; j < HIST_WIDTH + 1; j++)
        color_printf(COLOR_WHITE, "-");
    color_printf(COLOR_WHITE, "+\n");

    for (i = 0; i < NANOCAM_SNAP_HIST_BINS; i++) {
        /* Overall brightness */
        float pct = (float)reply.hist[0][i] * 100 / UINT8_MAX;
        printf(" %5.1f%% |", pct);

        /* Per-color brightness (round-up and scale to max bin count) */
        unsigned int r_blocks = (reply.hist[1][i] * HIST_WIDTH + bin_max - 1) / bin_max;
        unsigned int g_blocks = (reply.hist[2][i] * HIST_WIDTH + bin_max - 1) / bin_max;
        unsigned int b_blocks = (reply.hist[3][i] * HIST_WIDTH + bin_max - 1) / bin_max;

        /* Show bar */
        for (j = 0; j < HIST_WIDTH; j++) {
            r = (j < r_blocks) && show_r;
            g = (j < g_blocks) && show_g;
            b = (j < b_blocks) && show_b;
            if (r || g || b)
                color_printf(hist_bin_color(r, g, b), "#");
            else
                printf(" ");
        }

        printf(" | %03u-%03u\n",
               i * 256 / NANOCAM_SNAP_HIST_BINS,
               i * 256 / NANOCAM_SNAP_HIST_BINS + 15);
    }

    color_printf(COLOR_WHITE, "    Max +");
    for (j = 0; j < HIST_WIDTH + 1; j++)
        color_printf(COLOR_WHITE, "-");
    color_printf(COLOR_WHITE, "+\n");

    return CMD_ERROR_NONE;
}

int cmd_cam_store(struct command_context *ctx)
{
    int result;
    cam_store_t store;
    cam_store_reply_t reply;
    char filename[40];
    uint8_t scale = 0;

    memset(filename, 0, sizeof(filename));

    if (ctx->argc > 1)
        strncpy(filename, ctx->argv[1], sizeof(filename) - 1);
    if (ctx->argc > 2)
        scale = atoi(ctx->argv[2]);

    memcpy(store.filename, filename, sizeof(store.filename));
    store.filename[sizeof(store.filename) - 1] = '\0';
    store.format = cam_format;
    store.flags = 0;
    store.scale = scale;

    result = nanocam_store(&store, &reply, cam_timeout);
    if (result != NANOCAM_ERROR_NONE) {
        printf("Store error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    printf("Result %"PRIu8"\n", reply.result);
    printf("Image 0x%08"PRIx32" %"PRIu32"\n",
           reply.image_ptr, reply.image_size);

    return CMD_ERROR_NONE;
}

int cmd_cam_read(struct command_context *ctx)
{
    int result;
    uint8_t reg;
    uint16_t value;
    const char *args = command_args(ctx);

    if (sscanf(args, "%hhx", &reg) != 1)
        return CMD_ERROR_SYNTAX;

    result = nanocam_reg_read(reg, &value, cam_timeout);
    if (result != NANOCAM_ERROR_NONE) {
        printf("Register read error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    printf("Register 0x%02hhx value 0x%04hx\r\n", reg, value);

    return CMD_ERROR_NONE;
}

int cmd_cam_write(struct command_context *ctx)
{
    int result;
    uint8_t reg;
    uint16_t value;
    const char *args = command_args(ctx);

    if (sscanf(args, "%hhx %hx", &reg, &value) != 2)
        return CMD_ERROR_SYNTAX;

    result = nanocam_reg_write(reg, value, cam_timeout);
    if (result != NANOCAM_ERROR_NONE) {
        printf("Register write error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    printf("Register 0x%02hhx value 0x%04hx\r\n", reg, value);

    return CMD_ERROR_NONE;
}

static void cam_list_cb(int seq, cam_list_element_t *elm)
{
    if (!elm) {
        printf("No images to list\n");
    } else {
        uint32_t time_sec = elm->time / 1000000000;
        printf("[%02u] F:%"PRIu8", T:%"PRIu32" 0x%08"PRIx32" %"PRIu32"\n",
               seq, elm->format, time_sec, elm->ptr, elm->size);
    }
}

int cmd_cam_list(struct command_context *ctx)
{
    int result;

    result = nanocam_img_list(cam_list_cb, cam_timeout);
    if (result != NANOCAM_ERROR_NONE) {
        printf("Image list error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    return CMD_ERROR_NONE;
}

int cmd_cam_list_flush(struct command_context *ctx)
{
    int result;

    result = nanocam_img_list_flush(cam_timeout);
    if (result != NANOCAM_ERROR_NONE) {
        printf("Image list flush error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    return CMD_ERROR_NONE;
}

int cmd_cam_focus(struct command_context *ctx)
{
    int result;
    uint32_t af;

    result = nanocam_focus(0, &af, cam_timeout);
    if (result != NANOCAM_ERROR_NONE) {
        printf("Focus error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    printf("Result %"PRIu32"\n", af);

    return CMD_ERROR_NONE;
}

int cmd_cam_recoverfs(struct command_context *ctx)
{
    int timeout = cam_timeout;

    /* It takes a while to recreate the file system, so
     * use a minimum timeout of 120s */
    if (timeout < 120000) {
        printf("warning: adjusting timeout to 120s\n");
        timeout = 120000;
    }

    int result = nanocam_recoverfs(timeout);
    if (result < 0) {
        printf("Recoverfs error: %d\n", result);
        return CMD_ERROR_FAIL;
    }

    return CMD_ERROR_NONE;
}

int cmd_cam_peek(struct command_context *ctx)
{
    int result;
    uint32_t addr, len;
    struct csp_cmp_message msg;
    FILE *fp_new = NULL;

    if (ctx->argc < 3) {
        return CMD_ERROR_SYNTAX;
    }

    if (sscanf(ctx->argv[1], "%"SCNx32, &addr) != 1) {
        return CMD_ERROR_SYNTAX;
    }
    if (sscanf(ctx->argv[2], "%"SCNu32, &len) != 1) {
        return CMD_ERROR_SYNTAX;
    }

    printf("Dumping mem from cam addr 0x%"PRIx32" len %"PRIu32" timeout %"PRIu32"\r\n", addr, len, cam_timeout);

    fp_new = fopen("cam_hexdump.bin", "wb");
    if(fp_new==NULL) {
        printf("Failed to open ./cam_hexdump.bin!\r\n");
        return CMD_ERROR_FAIL;
    }
    printf("Writing to file cam_hexdump.bin (this takes a while!): \r\n");

    while(len>0) {
        msg.peek.addr = csp_hton32(addr);
        if(len > CSP_CMP_PEEK_MAX_LEN)
            msg.peek.len = CSP_CMP_PEEK_MAX_LEN;
        else
            msg.peek.len = len;

        result = csp_cmp_peek(nanocam_get_node(), cam_timeout, &msg);

        if (result != CSP_ERR_NONE) {
            printf("Peek error: %d ", result);
            printf("(addr=%"PRIX32" len=%"PRIu8")\r\n", msg.peek.addr, msg.peek.len);
            break;
        }

        printf(".");
        fwrite(msg.peek.data, msg.peek.len, 1, fp_new);

        len = len-msg.peek.len;
        addr = addr+msg.peek.len;
    }
    fclose(fp_new);

    if (result != CSP_ERR_NONE) {
        return CMD_ERROR_FAIL;
    } else {
        printf("Done\r\n");
        return CMD_ERROR_NONE;
    }
}

command_t __sub_command cam_subcommands[] = {
    {
        .name = "node",
        .help = "Set CAM address in host table",
        .usage = "<node>",
        .handler = cmd_cam_node,
    },{
        .name = "timeout",
        .help = "Set timeout",
        .usage = "<time>",
        .handler = cmd_cam_set_timeout,
    },{
        .name = "format",
        .help = "Set store format (0=raw, 1=bmp, 2=jpg, 3=dng, 4=raw10)",
        .usage = "<format>",
        .handler = cmd_cam_format,
    },{
        .name = "snap",
        .help = "Snap picture",
        .usage = "[-a][-d <delay>][-h <color>][-i][-n <count>][-r][-s][-t][-x][-e]",
        .handler = cmd_cam_snap,
    },{
        .name = "store",
        .help = "Store picture",
        .usage = "[path] [scale-down]",
        .handler = cmd_cam_store,
    },{
        .name = "read",
        .help = "Read sensor register",
        .usage = "<reg-hex>",
        .handler = cmd_cam_read,
    },{
        .name = "write",
        .help = "Write sensor register",
        .usage = "<reg-hex> <value-hex>",
        .handler = cmd_cam_write,
    },{
        .name = "list",
        .help = "List RAM pictures",
        .handler = cmd_cam_list,
    },{
        .name = "flush",
        .help = "Flush RAM pictures",
        .handler = cmd_cam_list_flush,
    },{
        .name = "focus",
        .help = "Run focus assist algorithm",
        .handler = cmd_cam_focus,
    },{
        .name = "recoverfs",
        .help = "Recreate data file system",
        .handler = cmd_cam_recoverfs,
    },{
        .name = "peek",
        .help = "Dump picture from RAM to ./cam_hexdump.bin via 'cmp peek'",
        .usage = "<address-hex> <size-dec>",
        .handler = cmd_cam_peek,
    }
};

command_t __root_command cam_commands[] = {
    {
        .name = "cam",
        .help = "client: NanoCam C1U",
        .chain = INIT_CHAIN(cam_subcommands),
    }
};

void nanocam_register_commands(void) {
    command_register(cam_commands);
}
