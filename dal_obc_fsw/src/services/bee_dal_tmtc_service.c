/* src/services/bee_dal_tmtc_service.c */
#include "services/bee_dal_tmtc_service.h"
#include "services/hk_service.h"
#include "services/obc_hk_wire.h"
#include "services/exp_uart_service.h"
#include "services/gui_state.h"
#include "services/power_en.h"
#include "services/power_stat.h"
#include "services/sensor_stat.h"
#include "services/uvc_capture.h"
#include "services/sys_info_wire.h"
#include "services/exp_csp_service.h"
#include "dal_obc_config.h"
#include "bee_dal_proto.h"
#include "bee_dal_files.h"

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <stdatomic.h>

extern bool g_verbose;
extern bool g_quiet_housekeeping;

static int              g_fd = -1;
static pthread_t        g_rx_thread;
static pthread_t        g_socket_thread;
static atomic_bool     g_running = false;
static bool             g_rx_started = false;
static bool             g_socket_started = false;
static int              g_socket_fd = -1;
static char             g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static pthread_mutex_t  g_dispatch_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bee_dal_local_reply_t *g_local_reply;
static _Thread_local int g_reply_fd = -1;
static _Thread_local bool g_suppress_hk_trace;
static atomic_int_fast64_t g_rtc_sim_offset = 0;

static bool rtc_simulated(void)
{
    const char *value = getenv("DAL_OBC_RTC_SIMULATE");
    return value && strcmp(value, "1") == 0;
}

/* Routine read-only telemetry that GUI and host pollers request frequently. */
static bool is_housekeeping_read(const bee_dal_tc_frame_t *frame)
{
    if (frame->type == BEE_DAL_TYPE_TELEMETRY)
        return frame->addr == BEE_DAL_TM_GET || frame->addr == BEE_DAL_TM_OBC_GET;
    if (frame->type == BEE_DAL_TYPE_SYS_CFG)
        return frame->addr == 0x04 || frame->addr == 0x07; /* pwr_stat, sen_stat */
    if (frame->type == BEE_DAL_TYPE_EXP_CTRL)
        return frame->addr == BEE_DAL_EXP_TLM;
    return false;
}

static const char *serial_port(void)
{
    const char *override = getenv("DAL_OBC_RS422_PORT");
    return override && *override ? override : DAL_OBC_RS422_PORT;
}

/* =========================================================================
 * Verbose dump helpers
 * ========================================================================= */
static void hex_dump(const uint8_t *data, size_t len)
{
    for (size_t off = 0; off < len; off += 16) {
        size_t line_len = (len - off < 16) ? (len - off) : 16;
        char   hex[16 * 3 + 1];
        char   ascii[17];
        size_t hpos = 0;

        for (size_t i = 0; i < line_len; i++) {
            hpos += (size_t)snprintf(hex + hpos, sizeof(hex) - hpos, "%02X ", data[off + i]);
            ascii[i] = (data[off + i] >= 32 && data[off + i] < 127) ? (char)data[off + i] : '.';
        }
        ascii[line_len] = '\0';

        printf("    %04zX  %-48s  %s\n", off, hex, ascii);
    }
    if (len == 0)
        printf("    (empty)\n");
}

/* =========================================================================
 * Serial port setup
 * ========================================================================= */
static int open_serial(const char *path, speed_t baud)
{
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return -1;

    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, baud);
    cfsetospeed(&tio, baud);

    tio.c_cflag |= (tcflag_t)(CLOCAL | CREAD);
    tio.c_cflag &= (tcflag_t)~PARENB;   /* no parity */
    tio.c_cflag &= (tcflag_t)~CSTOPB;   /* 1 stop bit */
    tio.c_cflag &= (tcflag_t)~CSIZE;
    tio.c_cflag |= CS8;                 /* 8 data bits */
#ifdef CRTSCTS
    tio.c_cflag &= (tcflag_t)~CRTSCTS;
#endif

    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = (cc_t)(DAL_OBC_RS422_READ_TIMEOUT_MS / 100);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);

    return fd;
}

static ssize_t write_full(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    int    stall_count = 0;

    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            if (++stall_count > 100) {
                errno = EIO;
                return -1;
            }
            continue;
        }
        stall_count = 0;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

static ssize_t send_socket_full(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

/* =========================================================================
 * TM reply helpers
 * ========================================================================= */
static void send_tm(uint8_t ack_err, uint8_t addr_errcode,
                    const uint8_t *payload, uint16_t len)
{
    if (g_local_reply) {
        g_local_reply->ack_err = ack_err;
        g_local_reply->addr_errcode = addr_errcode;
        g_local_reply->length = len;
        if (len && payload) memcpy(g_local_reply->payload, payload, len);
        g_local_reply->ready = true;
        return;
    }
    uint8_t out[BEE_DAL_TM_HDR_LEN + BEE_DAL_MAX_PAYLOAD + 2];
    int n = bee_dal_encode_tm(ack_err, addr_errcode, payload, len, out, sizeof(out));
    if (n < 0) {
        fprintf(stderr, "[BEE-DAL] encode overflow (len=%u)\n", len);
        return;
    }

    if (g_verbose && (!g_suppress_hk_trace || ack_err == BEE_DAL_ACK_ERR_MARK)) {
        if (ack_err == BEE_DAL_ACK_ERR_MARK)
            printf("[TX] ACK/ERR=0xFF (ERR)  ERRCODE=0x%02X  payload_len=%u\n",
                   addr_errcode, len);
        else
            printf("[TX] ACK/ERR=0x%02X (echo TYPE)  ADDR=0x%02X  payload_len=%u\n",
                   ack_err, addr_errcode, len);
        if (len > 0) {
            printf("[TX] payload:\n");
            hex_dump(payload, len);
        }
        printf("[TX] frame (%d bytes):\n", n);
        hex_dump(out, (size_t)n);
    }

    int target = g_reply_fd >= 0 ? g_reply_fd : g_fd;
    if ((g_reply_fd >= 0 ? send_socket_full(target, out, (size_t)n)
                         : write_full(target, out, (size_t)n)) < 0)
        fprintf(stderr, "[BEE-DAL] write failed: %s\n", strerror(errno));
}

static void send_ack(const bee_dal_tc_frame_t *f, const uint8_t *payload, uint16_t len)
{
    send_tm(f->type, f->addr, payload, len);
}

static void send_err(uint8_t errcode)
{
    send_tm(BEE_DAL_ACK_ERR_MARK, errcode, NULL, 0);
}

/* Debug frames (TYPE=0xFF) must NOT echo f->type as ACK/ERR like send_ack()  */
#define BEE_DAL_DEBUG_ACK 0xFEU

static void send_debug_ack(const bee_dal_tc_frame_t *f, const uint8_t *payload, uint16_t len)
{
    send_tm(BEE_DAL_DEBUG_ACK, f->addr, payload, len);
}

static uint32_t read_uptime_sec(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 0;
    double up = 0.0;
    int ok = (fscanf(f, "%lf", &up) == 1);
    fclose(f);
    return ok ? (uint32_t)up : 0;
}

/* Generic "TM ACK package": ACK/ERR=TC TYPE, ADDR/ERRCODE=TC ADDR,
 * LENGTH=4, System uptime (sec) U32. Used by ping and dat_des. */
static void send_ack_uptime(const bee_dal_tc_frame_t *f)
{
    uint8_t payload[4];
    size_t  off = 0;
    bee_dal_put_u32(payload, &off, read_uptime_sec());
    send_ack(f, payload, (uint16_t)off);
}

/* EXP replies: status U8, text length U16 LE, then UTF-8 UART output. */
#define EXP_REPLY_MAX 16384U
static void handle_exp_gateway(const bee_dal_tc_frame_t *f)
{
    char command[96], text[EXP_REPLY_MAX + 1];
    uint8_t result;
    if (f->addr == BEE_DAL_EXP_PING && f->length == 0) {
        result = exp_uart_telemetry("system", text, sizeof(text));
    } else if (f->addr == BEE_DAL_EXP_TLM && f->length > 0 && f->length < 20 &&
               !memchr(f->payload, 0, f->length)) {
        memcpy(command, f->payload, f->length);
        command[f->length] = 0;
        result = exp_uart_telemetry(command, text, sizeof(text));
    } else if (f->addr == BEE_DAL_EXP_RUN && f->length > 0 && f->length <= 95 &&
               !memchr(f->payload, 0, f->length)) {
        memcpy(command, f->payload, f->length);
        command[f->length] = 0;
        result = exp_uart_command(command, text, sizeof(text));
    } else {
        send_err(BEE_DAL_ERR_TC_INVALID);
        return;
    }
    size_t length = strnlen(text, sizeof(text));
    uint8_t *reply = malloc(3 + length);
    if (!reply) { send_err(BEE_DAL_ERR_MEMORY); return; }
    reply[0] = result;
    reply[1] = (uint8_t)length;
    reply[2] = (uint8_t)(length >> 8);
    memcpy(reply + 3, text, length);
    send_ack(f, reply, (uint16_t)(3 + length));
    free(reply);
}

/* =========================================================================
 * Command handlers
 * ========================================================================= */

/* PING (TYPE=0x00, ADDR=0x00) */
static void handle_ping(const bee_dal_tc_frame_t *f)
{
    send_ack_uptime(f);
}

static void handle_sys_info(const bee_dal_tc_frame_t *f)
{
    if (f->length != 0) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    uint8_t payload[SYS_INFO_PAYLOAD_SIZE] = {0};
    payload[0] = SYS_INFO_SCHEMA;
    payload[1] = 3;
    size_t stamp = 4;
    bee_dal_put_u32(payload, &stamp, (uint32_t)time(NULL));

    uint8_t *pdu = payload + SYS_INFO_HEADER_SIZE;
    pdu[0] = SYS_INFO_PDU;
    pdu[2] = DAL_PDU_HW_VERSION_MAJOR;
    pdu[3] = DAL_PDU_HW_VERSION_MINOR;
    pdu[4] = DAL_PDU_HW_VERSION_PATCH;
    memset(pdu + 5, 0xff, 3);
    uint8_t version[3];
    uint32_t build;
    if (gui_pdu_read_version(version, &build) == 0) {
        pdu[1] = 1;
        memcpy(pdu + 5, version, 3);
        snprintf((char *)pdu + 8, SYS_INFO_BUILD_SIZE, "%08X", build);
    }

    uint8_t *exp = pdu + SYS_INFO_RECORD_SIZE;
    exp[0] = SYS_INFO_EXP;
    memset(exp + 2, 0xff, 6);
    uint8_t firmware[3], hardware[3];
    char build_id[21];
    if (exp_csp_identity(firmware, hardware, build_id, sizeof(build_id)) == 0) {
        exp[1] = 1;
        memcpy(exp + 2, hardware, 3);
        memcpy(exp + 5, firmware, 3);
        memcpy(exp + 8, build_id, strnlen(build_id, SYS_INFO_BUILD_SIZE));
    }

    uint8_t *obc = exp + SYS_INFO_RECORD_SIZE;
    obc[0] = SYS_INFO_OBC;
    obc[1] = 1;
    obc[2] = DAL_OBC_HW_VERSION_MAJOR;
    obc[3] = DAL_OBC_HW_VERSION_MINOR;
    obc[4] = DAL_OBC_HW_VERSION_PATCH;
    obc[5] = DAL_OBC_VERSION_MAJOR;
    obc[6] = DAL_OBC_VERSION_MINOR;
    obc[7] = DAL_OBC_VERSION_PATCH;
    payload[2] = (uint8_t)((pdu[1] ? 1U : 0U) | (exp[1] ? 2U : 0U) | 4U);
    send_ack(f, payload, sizeof(payload));
}

/* pwr_adj_cfg: VOUT setpoint in mV. The PDU handles TRANS power sequencing. */
static void handle_pwr_adj_cfg(const bee_dal_tc_frame_t *f)
{
    if (f->length != 0 && f->length != 2) {
        send_err(BEE_DAL_ERR_TC_INVALID);
        return;
    }
    bool set = f->length == 2;
    uint16_t target_mv = 0;
    if (set) {
        size_t off = 0;
        target_mv = bee_dal_get_u16(f->payload, &off);
        if (target_mv < 800 || target_mv > 20000) {
            send_err(BEE_DAL_ERR_DATA);
            return;
        }
    }
    gui_pdu_vout_t state;
    if (gui_pdu_vout(set, target_mv, &state) != 0) {
        send_err(BEE_DAL_ERR_UNKNOWN);
        return;
    }
    uint8_t payload[8];
    size_t off = 0;
    bee_dal_put_u16(payload, &off, state.setpoint_mv);
    bee_dal_put_u16(payload, &off, state.voltage_mv);
    bee_dal_put_u16(payload, &off, state.current_ma);
    bee_dal_put_u16(payload, &off, state.flags);
    send_ack(f, payload, sizeof(payload));
}

static void handle_sensor_stat(const bee_dal_tc_frame_t *f)
{
    if (f->length!=4) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    uint8_t payload[SENSOR_STAT_MAX_PAYLOAD];size_t length=0;
    int result=sensor_stat_read(f->payload[0],f->payload[1],f->payload[2],f->payload[3],
                                payload,sizeof(payload),&length);
    if (result!=0) {send_err(result==-2 ? BEE_DAL_ERR_DATA : BEE_DAL_ERR_UNKNOWN);return;}
    send_ack(f,payload,(uint16_t)length);
}

static int thermal_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Pump detection is queued on EXP UART0. Return its ticket and poll it
 * separately, so one RS-422 transaction never waits for hardware init. */
static void handle_fld_pump_detect(const bee_dal_tc_frame_t *f)
{
    const uint8_t *p=f->payload;
    uint8_t out[32]={0};
    char command[48],reply[512];
    unsigned long id=0,driver_error=0;
    char state[32]={0};
    for (unsigned i=8;i<32;i++) if (p[i]) {send_err(BEE_DAL_ERR_DATA);return;}
    if (p[2]>1 || (p[2]==0 && (p[3]>1 || p[4] || p[5] || p[6] || p[7])) ||
        (p[2]==1 && (p[3]!=0xff || !(p[4]|p[5]|p[6]|p[7])))) {
        send_err(BEE_DAL_ERR_DATA);return;
    }
    out[0]=1;out[1]=9;out[2]=p[2];out[3]=p[3];
    if (p[2]==0) snprintf(command,sizeof(command),"%s",p[3]?"pump4_detect":"pump_detect");
    else {
        id=(unsigned long)p[4]|((unsigned long)p[5]<<8)|
            ((unsigned long)p[6]<<16)|((unsigned long)p[7]<<24);
        snprintf(command,sizeof(command),"pump_result %lu",id);
        memcpy(out+4,p+4,4);
    }
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    char *marker=strstr(reply,"Pump request QUEUED id=");
    if (marker && p[2]==0 && sscanf(marker,"Pump request QUEUED id=%lu",&id)==1 && id<=UINT32_MAX) {
        out[4]=(uint8_t)id;out[5]=(uint8_t)(id>>8);
        out[6]=(uint8_t)(id>>16);out[7]=(uint8_t)(id>>24);
        out[8]=0; /* queued */
    } else if (p[2]==0 && (strstr(reply,"Pump detected") || strstr(reply,"Pump4 detected"))) {
        out[8]=2; /* done; UART2 CLI may wait internally */
    } else if (p[2]==1 && (marker=strstr(reply,"Pump request ")) &&
        sscanf(marker,"Pump request %lu %31s (r=%lu)",&id,state,&driver_error)==3 &&
        id==((unsigned long)p[4]|((unsigned long)p[5]<<8)|
             ((unsigned long)p[6]<<16)|((unsigned long)p[7]<<24)) && driver_error<=UINT32_MAX) {
        static const char *const names[]={"QUEUED","ACCEPTED","DONE","REFUSED",
            "IO_ERROR","POWER_LOST","CANCELLED"};
        unsigned code=11;
        for (unsigned i=0;i<sizeof(names)/sizeof(*names);i++)
            if (!strcmp(state,names[i])) {code=i;break;}
        out[8]=(uint8_t)code;
        out[9]=(uint8_t)driver_error;out[10]=(uint8_t)(driver_error>>8);
        out[11]=(uint8_t)(driver_error>>16);out[12]=(uint8_t)(driver_error>>24);
    } else if (strstr(reply,"Pump type unconfigured") || strstr(reply,"Pump request NOT_READY")) out[8]=9;
    else if (strstr(reply,"Pump request BUSY")) out[8]=7;
    else if (strstr(reply,"Pump request INTERLOCKED")) out[8]=8;
    else if (strstr(reply,"Pump request INVALID")) out[8]=10;
    else if (strstr(reply,"Pump result expired or unknown")) out[8]=11;
    else {send_err(BEE_DAL_ERR_DATA);return;}
    send_ack(f,out,sizeof(out));
}

/* 01/16: one atomic PC configuration change per 32-byte TC. */
static void handle_fld_cfg(const bee_dal_tc_frame_t *f)
{
    uint8_t out[32] = {0};
    char command[96], reply[4096];
    const uint8_t *p = f->payload;
    if (f->length != 0 && f->length != 32) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (f->length == 0 || p[0] == 0) {
        unsigned page = f->length ? p[1] : 0;
        if (exp_uart_command("cfg_json_pc", reply, sizeof(reply)) != 0) {
            send_err(BEE_DAL_ERR_UNKNOWN); return;
        }
        char *json = strchr(reply, '{');
        if (!json) { send_err(BEE_DAL_ERR_DATA); return; }
        unsigned total = 0, id = 0;
        char *marker = strstr(json, "\"CHUNK_REQUIRED\"");
        if (marker) {
            char *idp = strstr(json, "\"report_id\":");
            char *sizep = strstr(json, "\"total\":");
            if (!idp || !sizep || sscanf(idp, "\"report_id\":%u", &id) != 1 ||
                sscanf(sizep, "\"total\":%u", &total) != 1 || total > 6144) {
                send_err(BEE_DAL_ERR_DATA); return;
            }
        } else {
            char *end = strrchr(json, '}');
            if (!end) { send_err(BEE_DAL_ERR_DATA); return; }
            total = (unsigned)(end - json + 1);
        }
        unsigned offset = page * 28u;
        if (offset >= total) { send_err(BEE_DAL_ERR_DATA); return; }
        out[0] = (uint8_t)total; out[1] = (uint8_t)(total >> 8);
        out[2] = (uint8_t)offset; out[3] = (uint8_t)(offset >> 8);
        if (marker) {
            snprintf(command, sizeof(command), "json_chunk %u %u", id, offset);
            if (exp_uart_command(command, reply, sizeof(reply)) != 0) {
                send_err(BEE_DAL_ERR_UNKNOWN); return;
            }
            char *data = strstr(reply, "\"data\":\"");
            if (!data) { send_err(BEE_DAL_ERR_DATA); return; }
            data += 8;
            for (unsigned i = 0; i < 28 && offset + i < total; ++i) {
                if (!*data || *data == '"') { send_err(BEE_DAL_ERR_DATA); return; }
                if (*data == '\\') {
                    ++data;
                    if (*data != '"' && *data != '\\' && *data != '/') {
                        send_err(BEE_DAL_ERR_DATA); return;
                    }
                }
                out[4+i] = (uint8_t)*data++;
            }
        } else memcpy(out + 4, json + offset, total - offset < 28 ? total - offset : 28);
        send_ack(f, out, 32); return;
    }
    if (p[0] != 1 || f->length != 32) { send_err(BEE_DAL_ERR_DATA); return; }
    static const char *const operation[] = {"", "test", "replace", "circulation"};
    static const char *const ground[] = {"protein_a", "protein_b", "precip_respol", "precip_rsvb"};
    static const char *const fields[] = {"", "duration", "minflow", "flow_action",
        "pump_freq", "pump_volt", "volume", "pump_on", "pump_off", "cycles",
        "pump_off_settle", "run_mode", "max_pump_on", "flow_check", "flow_delay",
        "fail_samples", "pump_enable", "name"};
    uint32_t value = (uint32_t)p[4] | ((uint32_t)p[5]<<8) |
        ((uint32_t)p[6]<<16) | ((uint32_t)p[7]<<24);
    const char *expected = NULL;
    switch (p[1]) {
    case 1:
        if (p[2]>3 || p[3]>3 || p[4]>3 || p[5]>3 ||
            p[2]==p[3] || p[2]==p[4] || p[2]==p[5] ||
            p[3]==p[4] || p[3]==p[5] || p[4]==p[5]) break;
        snprintf(command,sizeof(command),"pc_valve_conf_channel %u %u %u %u",p[2],p[3],p[4],p[5]);
        expected="OK pc_valve_conf_channel"; goto execute;
    case 2:
        if (value<1 || value>100) break;
        snprintf(command,sizeof(command),"pc_valve_conf_pulse %u",value);
        expected="OK pc_valve_conf_pulse"; goto execute;
    case 3:
        if (value>1) break;
        snprintf(command,sizeof(command),"pc_flow_conf_channel %u",value);
        expected="OK pc_flow_conf_channel"; goto execute;
    case 4:
        if (!value) break;
        snprintf(command,sizeof(command),"pc_flow_conf_interval %u",value);
        expected="OK pc_flow_conf_interval"; goto execute;
    case 5: {
        if (p[4]>15) break;
        const char *name = NULL;
        if (p[2]==0) name="pc_standby_conf_dir";
        else if (p[2]>=1 && p[2]<=3 && p[3]<2)
            name=p[2]==1?"pc_test_conf_dir":p[2]==2?"pc_replace_conf_dir":"pc_circulation_conf_dir";
        else if (p[2]==4 && p[3]<4) name="pc_onground_conf";
        if (!name) break;
        /* Standby has no route argument. */
        if (p[2]==0) snprintf(command,sizeof(command),"%s %u %u %u %u",name,
            p[4]&1,(p[4]>>1)&1,(p[4]>>2)&1,(p[4]>>3)&1);
        else snprintf(command,sizeof(command),"%s %s %u %u %u %u",name,
            p[2]==4?ground[p[3]]:p[3]?"b":"a",
            p[4]&1,(p[4]>>1)&1,(p[4]>>2)&1,(p[4]>>3)&1);
        expected="OK pc_"; goto execute;
    }
    case 6:
        if (p[2]>3 || p[3]<1 || p[3]>(f->type==2?17:12) ||
            (p[2]==0 && p[3]<10) || (p[2]!=0 && p[3]>=10 && p[3]<=12) ||
            (p[2]!=3 && (p[3]==7 || p[3]==8 || p[3]==9)) ||
            (p[2]==3 && p[3]==1) ||
            (p[3]==3 && value>2) || (p[3]==11 && value>1)) break;
        if (p[3]>=13) {
            if (p[3]==17) {
                if (p[2]!=0 || !p[4] || p[27] || p[28] || p[29] || p[30] || p[31]) break;
                bool valid=true;
                for (unsigned i=4;i<27 && p[i];i++)
                    if (p[i]<0x21 || p[i]>0x7e) valid=false;
                if (!valid) break;
                char name[24];memcpy(name,p+4,24);name[23]=0;
                snprintf(command,sizeof(command),"pc_profile_set global name %s",name);
            } else {
                if (p[2]==0 || (p[3]==15 && p[2]==3) ||
                    ((p[3]==13 || p[3]==16) && value>1) ||
                    (p[3]==15 && (value<1 || value>100))) break;
                snprintf(command,sizeof(command),"pc_profile_set %s %s %u",
                    operation[p[2]],fields[p[3]],value);
            }
            expected="OK pc_profile_set";goto execute;
        }
        if (p[3]==2 || p[3]==6)
            snprintf(command,sizeof(command),"pc_profile_set %s %s %u.%03u",
                p[2]?operation[p[2]]:"global",fields[p[3]],value/1000,value%1000);
        else if (p[3]==3 || p[3]==11)
            snprintf(command,sizeof(command),"pc_profile_set %s %s %s",
                p[2]?operation[p[2]]:"global",fields[p[3]],
                p[3]==11?(value?"volume":"time"):(value==0?"none":value==1?"warn":"stop"));
        else snprintf(command,sizeof(command),"pc_profile_set %s %s %u",
            p[2]?operation[p[2]]:"global",fields[p[3]],value);
        expected="OK pc_profile_set"; goto execute;
    case 7:
        if (value>1) break;
        snprintf(command,sizeof(command),"pc_resume_cfg %u",value);
        expected="OK pc_resume_cfg"; goto execute;
    case 8:
        if (value>1) break;
        snprintf(command,sizeof(command),"pump_type_set %s",value?"highdriver":"lowdriver");
        expected="Pump type"; goto execute;
    case 9:
        handle_fld_pump_detect(f); return;
    case 10:
        if (f->type!=2 || value>1) break;
        snprintf(command,sizeof(command),"pc_datalog_%s",value?"enable":"disable");
        expected="OK pc_datalog:";goto execute;
    case 11:
        if (f->type!=2 || value) break;
        snprintf(command,sizeof(command),"pc_profile_load_default");
        expected="OK pc_profile_load";goto execute;
    }
    send_err(BEE_DAL_ERR_DATA); return;
execute:
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    if (!strstr(reply,expected) ||
        (p[1]==8 && !strstr(reply,value?"Pump type: highdriver":"Pump type: lowdriver"))) {
        send_err(BEE_DAL_ERR_DATA);return;
    }
    memcpy(out,p,32);
    send_ack(f,out,32);
}

static void handle_pc_cfg(const bee_dal_tc_frame_t *f)
{
    if (f->length==32 && f->payload[0]==1 &&
        (f->payload[1]==8 || f->payload[1]==9)) {
        send_err(BEE_DAL_ERR_DATA);return;
    }
    handle_fld_cfg(f);
}

static void handle_thp_cfg(const bee_dal_tc_frame_t *f)
{
    if (f->length!=32) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    const uint8_t *p=f->payload;
    if (p[0]>1 || p[1]>1 || p[2]>=(p[1]?2:4)) {send_err(BEE_DAL_ERR_DATA);return;}
    if (!p[0]) {
        for (unsigned i=3;i<32;i++) if (p[i]) {send_err(BEE_DAL_ERR_DATA);return;}
    } else if (!p[1]) {
        for (unsigned i=28;i<32;i++) if (p[i]) {send_err(BEE_DAL_ERR_DATA);return;}
    }
    char command[78],reply[512];
    static const char digits[]="0123456789abcdef";
    memcpy(command,"thp_wire_cfg ",13);
    for (unsigned i=0;i<32;i++) {
        command[13+2*i]=digits[p[i]>>4];command[14+2*i]=digits[p[i]&15];
    }
    command[77]='\0';
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    const char *hex=strstr(reply,"THP_CFG ");
    if (!hex) {send_err(BEE_DAL_ERR_DATA);return;}
    hex+=8;
    uint8_t out[32];
    for (unsigned i=0;i<32;i++) {
        unsigned byte;int consumed=0;
        if (sscanf(hex+2*i,"%2x%n",&byte,&consumed)!=1 || consumed!=2) {
            send_err(BEE_DAL_ERR_DATA);return;
        }
        out[i]=(uint8_t)byte;
    }
    if (out[0]!=p[0] || out[1]!=p[1] || out[2]!=p[2]) {send_err(BEE_DAL_ERR_DATA);return;}
    send_ack(f,out,32);
}

static void handle_thp_ctl(const bee_dal_tc_frame_t *f)
{
    if (f->length!=1) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    uint8_t selector=f->payload[0];
    unsigned channel=selector&3u,kind=(selector>>2)&1u,action=(selector>>3)&3u;
    if ((selector&0xe0u) || (kind && (channel>1u || action==3u))) {
        send_err(BEE_DAL_ERR_DATA);return;
    }
    char command[24],reply[256];
    snprintf(command,sizeof(command),"thp_wire_ctl %02x",selector);
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    const char *hex=strstr(reply,"THP_CTL ");
    unsigned value;int consumed=0;
    if (!hex || sscanf(hex+8,"%2x%n",&value,&consumed)!=1 || consumed!=2 ||
        (value&7u)>(kind?4u:2u)) {send_err(BEE_DAL_ERR_DATA);return;}
    uint8_t status=(uint8_t)value;
    if ((!!(status&0x80u))!=
        (kind?((status&7u)>=1u && (status&7u)<=3u):((status&7u)==1u))) {
        send_err(BEE_DAL_ERR_DATA);return;
    }
    send_ack(f,&status,1);
}

static void handle_pc_ctl(const bee_dal_tc_frame_t *f)
{
    if (f->length!=8) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    const uint8_t *p=f->payload;
    for (unsigned i=2;i<8;i++) if (p[i]) {send_err(BEE_DAL_ERR_DATA);return;}
    char command[48],reply[512];
    const char *expected=NULL;
    static const char *const cases[]={"both","a","b"};
    static const char *const routes[]={"protein_a","protein_b","precip_respol","precip_rsvb"};
    if ((p[0]>=3 && p[0]<=5 && p[1]>2) || (p[0]==6 && p[1]>3) ||
        ((p[0]<3 || p[0]>6) && p[1])) {send_err(BEE_DAL_ERR_DATA);return;}
    switch (p[0]) {
    case 1: snprintf(command,sizeof(command),"pc_activate");expected="OK pc_activate";break;
    case 2: snprintf(command,sizeof(command),"pc_deactivate");expected="OK pc_deactivate";break;
    case 3: snprintf(command,sizeof(command),"pc_test %s",cases[p[1]]);expected="pc test sent";break;
    case 4: snprintf(command,sizeof(command),"pc_replace %s",cases[p[1]]);expected="pc replace sent";break;
    case 5: snprintf(command,sizeof(command),"pc_circulation %s",cases[p[1]]);expected="pc circulation sent";break;
    case 6: snprintf(command,sizeof(command),"pc_onground %s",routes[p[1]]);expected="pc onground preset sent";break;
    case 7: snprintf(command,sizeof(command),"pc_standby");expected="pc standby sent";break;
    case 8: snprintf(command,sizeof(command),"pc_stop");expected="pc stop sent";break;
    case 9: snprintf(command,sizeof(command),"pc_reset_error");expected="pc reset_error sent";break;
    default: send_err(BEE_DAL_ERR_DATA);return;
    }
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    if (strstr(reply,"ERROR") ||
        (!strstr(reply,expected) &&
         !(p[0]==1 && strstr(reply,"pc already activated")) &&
         !(p[0]==2 && strstr(reply,"pc already deactivated")))) {
        const char *reason = strstr(reply, "ERROR");
        if (!reason) reason = reply;
        fprintf(stderr, "[PC] %s rejected: %.*s\n", command,
                (int)strcspn(reason, "\r\n"), reason);
        send_err(BEE_DAL_ERR_DATA);return;
    }
    send_ack(f,NULL,0);
}

static bool icd_can_required(void)
{
    const char *enabled = getenv("DAL_EXP_CSP_ENABLE");
    return !enabled || strcmp(enabled, "0") != 0;
}

static void handle_pc_stat(const bee_dal_tc_frame_t *f)
{
    if (f->length) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    uint8_t cached;
    if (exp_csp_icd_status(3, &cached, 1) == 0) {
        if ((cached&7u)==7u || ((cached&0x20u) && ((cached&7u)<1u || (cached&7u)>3u)) ||
            ((cached&0x40u) && (cached&7u)!=3u)) {send_err(BEE_DAL_ERR_DATA);return;}
        send_ack(f,&cached,1);return;
    }
    if (icd_can_required()) {
        send_err(BEE_DAL_ERR_UNKNOWN);return;
    }
    char reply[256];
    if (exp_uart_cached_command("pc_wire_stat",reply,sizeof(reply))!=0) {
        send_err(BEE_DAL_ERR_UNKNOWN);return;
    }
    const char *hex=strstr(reply,"PC_STAT ");
    if (!hex) {send_err(BEE_DAL_ERR_DATA);return;}
    hex+=8;
    unsigned value;
    if (!isxdigit((unsigned char)hex[0]) ||
        !isxdigit((unsigned char)hex[1]) ||
        (hex[2] && !isspace((unsigned char)hex[2])) ||
        sscanf(hex,"%2x",&value)!=1 || (value&7u)==7u ||
        ((value&0x20u) && ((value&7u)<1u || (value&7u)>3u)) ||
        ((value&0x40u) && (value&7u)!=3u)) {
        send_err(BEE_DAL_ERR_DATA);return;
    }
    uint8_t out=(uint8_t)value;
    send_ack(f,&out,1);
}

static void handle_fld_ctl(const bee_dal_tc_frame_t *f)
{
    const uint8_t *p=f->payload;
    char command[96], reply[512];
    uint32_t value;
    if (f->length!=8) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    value=(uint32_t)p[4]|((uint32_t)p[5]<<8)|((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
    if (p[0]==0 && p[1]==0 && p[2]==0 && p[3]<=15 && value==0) {
        snprintf(command,sizeof(command),"pc_onground_control %u %u %u %u",
            p[3]&1u,(p[3]>>1)&1u,(p[3]>>2)&1u,(p[3]>>3)&1u);
    } else if ((p[0]==1 || p[0]==2) && p[3]==0) {
        const char *prefix=p[0]==1?"pump":"pump4";
        if (p[1]<=1 && p[2]==0 && value==0)
            snprintf(command,sizeof(command),"%s_%s",prefix,p[1]?"enable":"disable");
        else if (p[1]==2 && p[2]==0 &&
                 value>=(p[0]==1?8u:50u) && value<=(p[0]==1?1992u:800u))
            snprintf(command,sizeof(command),"%s_set_freq %u",prefix,value);
        else if (p[1]==3 && p[2]<=(p[0]==1?0u:3u) && value<=(p[0]==1?150u:250u)) {
            if (p[0]==1) snprintf(command,sizeof(command),"pump_set_volt %u",value);
            else snprintf(command,sizeof(command),"pump4_set_volt %u %u",p[2],value);
        } else {send_err(BEE_DAL_ERR_DATA);return;}
    } else {send_err(BEE_DAL_ERR_DATA);return;}
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    if (p[0]==0) {
        if (!strstr(reply,"pc onground_control sent")) {send_err(BEE_DAL_ERR_DATA);return;}
    } else if (!strstr(reply,"Pump request QUEUED id=") &&
               !strstr(reply,"Pump enabled") && !strstr(reply,"Pump disabled") &&
               !strstr(reply,"Pump4 enabled") && !strstr(reply,"Pump4 disabled") &&
               !strstr(reply,"Pump value set")) {send_err(BEE_DAL_ERR_DATA);return;}
    if (p[0]!=0) {
        const char *queued=strstr(reply,"Pump request QUEUED id=");
        unsigned long id=0;
        if (queued) {
            if (sscanf(queued,"Pump request QUEUED id=%lu",&id)!=1 ||
                id==0 || id>UINT32_MAX) {send_err(BEE_DAL_ERR_DATA);return;}
            uint8_t result[4]={(uint8_t)id,(uint8_t)(id>>8),(uint8_t)(id>>16),(uint8_t)(id>>24)};
            send_ack(f,result,sizeof(result));return;
        }
    }
    send_ack(f,NULL,0);
}

/* 01/1A: one fixed-width manual motor request, translated to the EXP CLI. */
static void handle_rba_ctl(const bee_dal_tc_frame_t *f)
{
    if (f->length!=8) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    const uint8_t *p=f->payload;
    uint32_t value=(uint32_t)p[4]|((uint32_t)p[5]<<8)|
        ((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
    int32_t signed_value=(int32_t)value;
    char command[96],reply[512];
    if (p[3] || p[2]>200 ||
        ((p[0]==1 || p[0]==7 || p[0]==8) && (p[1]!=2 || p[2])) ||
        ((p[0]>=2 && p[0]<=5) && p[1]>1) ||
        (p[0]==6 && (p[1]>2 || p[2])) ||
        (p[0]==9 && (p[1]>1 || p[2])) ||
        (p[0]!=8 && p[0]!=2 && p[0]!=3 && p[0]!=4 && value)) {
        send_err(BEE_DAL_ERR_DATA);return;
    }
    switch (p[0]) {
    case 1: snprintf(command,sizeof(command),"motor5_init");break;
    case 2:
        if (!signed_value || signed_value==INT32_MIN) {send_err(BEE_DAL_ERR_DATA);return;}
        if (p[2]) snprintf(command,sizeof(command),"motor5_move %u %d %u",p[1],signed_value,p[2]);
        else snprintf(command,sizeof(command),"motor5_move %u %d",p[1],signed_value);
        break;
    case 3:
        if (p[2]) snprintf(command,sizeof(command),"motor5_goto %u %d %u",p[1],signed_value,p[2]);
        else snprintf(command,sizeof(command),"motor5_goto %u %d",p[1],signed_value);
        break;
    case 4:
        if (value!=1 && value!=UINT32_MAX) {send_err(BEE_DAL_ERR_DATA);return;}
        if (p[2]) snprintf(command,sizeof(command),"motor5_run %u %s %u",p[1],value==1?"cw":"ccw",p[2]);
        else snprintf(command,sizeof(command),"motor5_run %u %s 0",p[1],value==1?"cw":"ccw");
        break;
    case 5:
        if (p[2]) {
            /* The legacy home CLI needs a direction; use the owner command with configured speed. */
            send_err(BEE_DAL_ERR_DATA);return;
        }
        snprintf(command,sizeof(command),"motor5_home %u",p[1]);break;
    case 6: snprintf(command,sizeof(command),"motor5_stop %s",p[1]==2?"all":p[1]==0?"0":"1");break;
    case 7: snprintf(command,sizeof(command),"motor5_disable");break;
    case 8:
        if (!value) {send_err(BEE_DAL_ERR_DATA);return;}
        snprintf(command,sizeof(command),"motor5_result %u",value);break;
    case 9: snprintf(command,sizeof(command),"motor5_stop %u hold",p[1]);break;
    default: send_err(BEE_DAL_ERR_DATA);return;
    }
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    if (p[0]==6 || p[0]==7 || p[0]==9) {
        const char *accepted=p[0]==6?"STOP_REQUESTED":
                             p[0]==7?"DISABLE_REQUESTED":"STOP_HOLD_REQUESTED";
        if (!strstr(reply,accepted)) {
            send_err(BEE_DAL_ERR_DATA);return;
        }
        send_ack(f,NULL,0);return;
    }
    static const char *const states[]={"QUEUED","ACCEPTED","DONE","INVALID","BUSY",
        "NOT_READY","REFUSED","NOT_HOMED","LIMIT_REACHED","FAULT","POWER_LOST",
        "CANCELLED","STORAGE_ERROR","NOT_FOUND","LIMIT_INVALID","POWER_OFF",
        "SPI_ERROR","WRONG_DEVICE"};
    unsigned code=0;
    const char *line=NULL;
    for (;code<sizeof(states)/sizeof(*states);code++) {
        const char *candidate=strstr(reply,states[code]);
        if (candidate && candidate[strlen(states[code])]==' ' &&
            !strncmp(candidate+strlen(states[code])+1,"request=",8)) {
            line=candidate;break;
        }
    }
    unsigned long id;long position;
    if (!line || sscanf(line,"%*s request=%lu position=%ld",&id,&position)!=2 ||
        id==0 || id>UINT32_MAX || position<INT32_MIN || position>INT32_MAX ||
        (p[0]==8 && id!=value) ||
        (p[0]!=8 && code>=3)) {send_err(BEE_DAL_ERR_DATA);return;}
    if (p[0]==8) {
        uint8_t out[12]={0};
        for (unsigned i=0;i<4;i++) {out[i]=(uint8_t)(id>>(8*i));out[8+i]=(uint8_t)((uint32_t)position>>(8*i));}
        out[4]=(uint8_t)code;
        send_ack(f,out,sizeof(out));return;
    }
    uint8_t out[4];for (unsigned i=0;i<4;i++) out[i]=(uint8_t)(id>>(8*i));
    send_ack(f,out,sizeof(out));
}

static void handle_fld_stat(const bee_dal_tc_frame_t *f)
{
    char reply[256];
    uint8_t out[16];
    if (f->length) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    if (exp_csp_icd_status(1, out, sizeof(out)) == 0) {
        if ((out[0]&0xf0u) || (out[1]&0xf0u) || out[3]>2 ||
            out[13] || out[14] || out[15]) {send_err(BEE_DAL_ERR_DATA);return;}
        send_ack(f,out,sizeof(out));return;
    }
    if (icd_can_required()) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    if (exp_uart_cached_command("fld_wire_stat",reply,sizeof(reply))!=0) {
        send_err(BEE_DAL_ERR_UNKNOWN);return;
    }
    const char *data=strstr(reply,"FLD_STAT ");
    if (!data) {send_err(BEE_DAL_ERR_DATA);return;}
    data+=9;
    for (unsigned i=0;i<16;i++) {
        unsigned byte;
        if (!isxdigit((unsigned char)data[2*i]) ||
            !isxdigit((unsigned char)data[2*i+1])) {send_err(BEE_DAL_ERR_DATA);return;}
        if (sscanf(data+2*i,"%2x",&byte)!=1) {send_err(BEE_DAL_ERR_DATA);return;}
        out[i]=(uint8_t)byte;
    }
    if ((data[32]>='0' && data[32]<='9') ||
        (data[32]>='a' && data[32]<='f') ||
        (data[32]>='A' && data[32]<='F') ||
        (out[0]&0xf0u) || (out[1]&0xf0u) || out[3]>2 ||
        out[13] || out[14] || out[15]) {send_err(BEE_DAL_ERR_DATA);return;}
    send_ack(f,out,sizeof(out));
}

static void handle_rba_stat(const bee_dal_tc_frame_t *f)
{
    if (f->length) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    uint8_t cached[16];
    if (exp_csp_icd_status(2, cached, sizeof(cached)) == 0) {
        if ((cached[0]&0xc0u) || (cached[1]&0xf0u) || (cached[2]&0xf0u) ||
            (cached[3]&0xf0u) || (cached[1]&~cached[2]) || cached[4]>5 ||
            cached[5]>5 || cached[14]>200 || cached[15]>200) {
            send_err(BEE_DAL_ERR_DATA);return;
        }
        send_ack(f,cached,sizeof(cached));return;
    }
    char reply[256];
    /* A manually requested status must work when periodic CSP polling is off
     * or the cache is stale. Query the MCU once on this request only. */
    if (exp_uart_command("rba_wire_stat",reply,sizeof(reply))!=0) {
        send_err(BEE_DAL_ERR_UNKNOWN);return;
    }
    const char *hex=strstr(reply,"RBA_STAT ");
    if (!hex) {send_err(BEE_DAL_ERR_DATA);return;}
    hex+=9;
    uint8_t out[16];
    for (unsigned i=0;i<sizeof(out);i++) {
        unsigned value;
        if (!isxdigit((unsigned char)hex[2*i]) ||
            !isxdigit((unsigned char)hex[2*i+1]) ||
            sscanf(hex+2*i,"%2x",&value)!=1) {send_err(BEE_DAL_ERR_DATA);return;}
        out[i]=(uint8_t)value;
    }
    if (isxdigit((unsigned char)hex[32]) || (out[0]&0xc0u) ||
        (out[1]&0xf0u) || (out[2]&0xf0u) || (out[3]&0xf0u) ||
        (out[1]&~out[2]) || out[4]>5 || out[5]>5 ||
        out[14]>200 || out[15]>200) {send_err(BEE_DAL_ERR_DATA);return;}
    send_ack(f,out,sizeof(out));
}

static void handle_thm_cfg(const bee_dal_tc_frame_t *f)
{
    if (f->length != 0 && f->length != 32) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (f->length == 32) {
        const uint8_t *p=f->payload;
        if (p[0]!=1 || !p[1] || (p[1]&0xc0)) { send_err(BEE_DAL_ERR_DATA); return; }
        for (unsigned i=0;i<4;i++) if (p[1]&(1u<<i)) {
            unsigned o=2+4*i; unsigned on=p[o+2]|((unsigned)p[o+3]<<8);
            if (p[o]>100 || p[o+1]>100 || !on || on>60000) { send_err(BEE_DAL_ERR_DATA); return; }
        }
        for (unsigned i=0;i<2;i++) if (p[1]&(1u<<(i+4))) {
            unsigned o=18+7*i;
            uint32_t hz=(uint32_t)p[o+2]|((uint32_t)p[o+3]<<8)|
                ((uint32_t)p[o+4]<<16)|((uint32_t)p[o+5]<<24);
            if (p[o]>25 || !p[o+1] || p[o+1]>100 || hz<100 || hz>100000) {
                send_err(BEE_DAL_ERR_DATA); return;
            }
        }
    }
    char command[80]="thm_cfg";
    if (f->length) {
        static const char digits[]="0123456789abcdef";
        command[7]=' ';
        for (unsigned i=0;i<32;i++) {
            command[8+2*i]=digits[f->payload[i]>>4];
            command[9+2*i]=digits[f->payload[i]&15];
        }
        command[72]=0;
    }
    char reply[512];
    if (exp_uart_command(command,reply,sizeof(reply))!=0) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
    char *marker=strstr(reply,"THM_CFG ");
    if (!marker || strlen(marker+8)<64) { send_err(BEE_DAL_ERR_DATA); return; }
    marker+=8;
    uint8_t payload[32];
    for (unsigned i=0;i<32;i++) {
        int hi=thermal_hex(marker[2*i]),lo=thermal_hex(marker[2*i+1]);
        if (hi<0 || lo<0) { send_err(BEE_DAL_ERR_DATA); return; }
        payload[i]=(uint8_t)((hi<<4)|lo);
    }
    if (payload[0]!=1 || payload[1]!=0x3f) { send_err(BEE_DAL_ERR_DATA); return; }
    send_ack(f,payload,32);
}

static void handle_thm_ctl(const bee_dal_tc_frame_t *f)
{
    if (f->length!=8) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    const uint8_t *p=f->payload;
    uint32_t duration=(uint32_t)p[4]|((uint32_t)p[5]<<8)|
        ((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
    if (p[0]>1 || p[1]>(p[0]?1:3) || p[2]>2 || p[3]>100 ||
        (p[2]==0 && (p[3] || duration)) ||
        (p[2]==1 && (!p[3] || (p[0] && duration) || (!p[0] && (!duration || duration>600000)))) ||
        (p[2]==2 && (!p[0] || p[3] || duration)) ||
        (p[0] && p[3]>25)) {send_err(BEE_DAL_ERR_DATA);return;}
    char command[80],reply[256];
    snprintf(command,sizeof(command),"thm_wire_ctl %u %u %u %u %lu",
             p[0],p[1],p[2],p[3],(unsigned long)duration);
    if (exp_uart_command(command,reply,sizeof(reply))!=0) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    unsigned state,duty;unsigned long id;
    char *marker=strstr(reply,"THM_CTL ");
    if (!marker || sscanf(marker,"THM_CTL %u %u %lu",&state,&duty,&id)!=3 ||
        state>2 || duty>100 || id>UINT32_MAX) {send_err(BEE_DAL_ERR_DATA);return;}
    (void)id;
    send_ack_uptime(f);
}

static void handle_thm_stat(const bee_dal_tc_frame_t *f)
{
    if (f->length!=0) {send_err(BEE_DAL_ERR_TC_INVALID);return;}
    uint8_t cached[6];
    if (exp_csp_icd_status(0, cached, sizeof(cached)) == 0) {
        for (unsigned i=0;i<6;i++)
            if ((cached[i]&0x7fu)>100u || ((cached[i]&0x80u) && !(cached[i]&0x7fu))) {
                send_err(BEE_DAL_ERR_DATA);return;
            }
        send_ack(f,cached,sizeof(cached));return;
    }
    if (icd_can_required()) {send_err(BEE_DAL_ERR_UNKNOWN);return;}
    char reply[256];
    if (exp_uart_cached_command("thm_wire_stat all",reply,sizeof(reply))!=0) {
        send_err(BEE_DAL_ERR_UNKNOWN);return;
    }
    char *marker=strstr(reply,"THM_STAT6 ");
    if (!marker || strlen(marker+10)<12u) {send_err(BEE_DAL_ERR_DATA);return;}
    marker+=10;
    uint8_t out[6];
    for (unsigned i=0;i<6u;i++) {
        int hi=thermal_hex(marker[2*i]),lo=thermal_hex(marker[2*i+1]);
        if (hi<0 || lo<0) {send_err(BEE_DAL_ERR_DATA);return;}
        out[i]=(uint8_t)((hi<<4)|lo);
        if ((out[i]&0x7fu)>100u || ((out[i]&0x80u)!=0 && (out[i]&0x7fu)==0)) {
            send_err(BEE_DAL_ERR_DATA);return;
        }
    }
    send_ack(f,out,sizeof(out));
}

static void handle_uvc_cfg(const bee_dal_tc_frame_t *f)
{
    if (f->length != 1 && f->length != 32) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    uint8_t camera = f->payload[0];
    uint16_t mask = f->length == 32 ? (uint16_t)f->payload[2] | ((uint16_t)f->payload[3] << 8) : 0;
    if (camera < 1 || camera > 4 || (f->length == 32 &&
        (f->payload[1] != 1 || f->payload[31] > 100 || (mask & 0xe000) ||
         (f->payload[31] && !(mask & (1u << 12)))))) {
        send_err(BEE_DAL_ERR_DATA); return;
    }
    const char *tool = getenv("DAL_USB_CAM_TOOL");
    if (!tool || !*tool) tool = "/home/root/tools/bin/usb_cam";
    char camera_arg[2] = {(char)('0' + camera), 0};
    char hex[65] = {0};
    if (f->length == 32)
        for (size_t i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", f->payload[i]);
    int pipes[2];
    if (pipe(pipes)) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
    pid_t pid = fork();
    if (pid < 0) { close(pipes[0]); close(pipes[1]); send_err(BEE_DAL_ERR_UNKNOWN); return; }
    if (pid == 0) {
        dup2(pipes[1], STDOUT_FILENO);
        close(pipes[0]); close(pipes[1]);
        if (f->length == 1) execl(tool, tool, camera_arg, "--uvc-get", (char *)NULL);
        else execl(tool, tool, camera_arg, "--uvc-set", hex, (char *)NULL);
        _exit(127);
    }
    close(pipes[1]);
    char output[128] = {0}; size_t used = 0;
    while (used < sizeof(output) - 1) {
        ssize_t n = read(pipes[0], output + used, sizeof(output) - 1 - used);
        if (n <= 0) break;
        used += (size_t)n;
    }
    close(pipes[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || used < 64) {
        send_err(BEE_DAL_ERR_DATA); return;
    }
    uint8_t reply[32];
    for (size_t i = 0; i < 32; ++i) {
        unsigned value;
        if (sscanf(output + 2 * i, "%2x", &value) != 1) { send_err(BEE_DAL_ERR_DATA); return; }
        reply[i] = (uint8_t)value;
    }
    if (reply[0] != camera || reply[1] != 1) { send_err(BEE_DAL_ERR_DATA); return; }
    send_ack(f, reply, sizeof(reply));
}

static void handle_uvc_ctl(const bee_dal_tc_frame_t *f)
{
    if (f->length != 8) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (f->payload[1] != 1 || f->payload[2] || f->payload[3]) {
        send_err(BEE_DAL_ERR_DATA); return;
    }
    size_t off = 4;
    uint32_t id = bee_dal_get_u32(f->payload, &off);
    uint8_t reply[8];
    int rc = uvc_capture_queue(f->payload[0], id, reply);
    if (rc) { send_err(rc == -2 ? BEE_DAL_ERR_DATA : BEE_DAL_ERR_UNKNOWN); return; }
    send_ack(f, reply, sizeof(reply));
}

static void handle_uvc_stat(const bee_dal_tc_frame_t *f)
{
    if (f->length != 5) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    size_t off = 1;
    uint32_t id = bee_dal_get_u32(f->payload, &off);
    uint8_t reply[8];
    int rc = uvc_capture_status(f->payload[0], id, reply);
    if (rc) { send_err(BEE_DAL_ERR_DATA); return; }
    send_ack(f, reply, sizeof(reply));
}


/* pwr_en: board U8; optional select U16, on U16, flags U8. */
static void handle_power_en(const bee_dal_tc_frame_t *f)
{
    if (f->length!=1 && f->length!=6) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    uint8_t board=f->payload[0];
    if (board<3 || board>5) { send_err(BEE_DAL_ERR_DATA); return; }
    uint16_t select=0, on=0;
    uint8_t flags=0;
    if (f->length==6) {
        size_t off=1;
        select=bee_dal_get_u16(f->payload,&off);
        on=bee_dal_get_u16(f->payload,&off);
        flags=bee_dal_get_u8(f->payload,&off);
        if (!select) { send_err(BEE_DAL_ERR_DATA); return; }
    }
    power_en_result_t result;
    int rc=power_en_execute(board,select,on,flags,&result);
    if (rc==-2) { send_err(BEE_DAL_ERR_DATA); return; }
    if (rc!=0) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
    uint8_t payload[7];
    size_t off=0;
    bee_dal_put_u8(payload,&off,board);
    bee_dal_put_u16(payload,&off,result.on);
    bee_dal_put_u16(payload,&off,result.known);
    bee_dal_put_u16(payload,&off,result.failed);
    send_ack(f,payload,sizeof(payload));
}

/* Local extension: clear the PDU eFuse fault latch (Modbus coil 00003). */
static void handle_pdu_fault_clear(const bee_dal_tc_frame_t *f)
{
    if (f->length != 0) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (gui_pdu_fault_clear() != 0) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
    const uint8_t cleared = 0;
    send_ack(f, &cleared, 1);
}

/* 01/1D: action 0 get, 1 enable, 2 on period, 3 off period. */
static void handle_pdu_heartbeat(const bee_dal_tc_frame_t *f)
{
    if (f->length!=0 && f->length!=5) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    uint8_t action=0;
    uint32_t value=0;
    if (f->length==5) {
        size_t off=0;
        action=bee_dal_get_u8(f->payload,&off);
        value=bee_dal_get_u32(f->payload,&off);
        if (action<1 || action>3 ||
            (action==1 && value>1) ||
            (action>=2 && (value<1 || value>60000))) {
            send_err(BEE_DAL_ERR_DATA); return;
        }
    }
    uint8_t enabled;
    uint32_t on_ms, off_ms;
    if (gui_pdu_heartbeat(action,value,&enabled,&on_ms,&off_ms)!=0) {
        send_err(BEE_DAL_ERR_UNKNOWN); return;
    }
    uint8_t reply[9];
    size_t off=0;
    bee_dal_put_u8(reply,&off,enabled);
    bee_dal_put_u32(reply,&off,on_ms);
    bee_dal_put_u32(reply,&off,off_ms);
    send_ack(f,reply,sizeof(reply));
}

/* 01/1E: PDU acknowledges Modbus register write before entering bootloader. */
static void handle_pdu_bootloader(const bee_dal_tc_frame_t *f)
{
    if (f->length!=0) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (gui_pdu_enter_bootloader()!=0) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
    const uint8_t accepted=1;
    send_ack(f,&accepted,1);
}

static void handle_power_stat(const bee_dal_tc_frame_t *f)
{
    if (f->length!=3) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    uint16_t mask=(uint16_t)(f->payload[1]|((uint16_t)f->payload[2]<<8));
    uint8_t payload[POWER_STAT_MAX_PAYLOAD];
    size_t length=0;
    int rc=power_stat_read(f->payload[0],mask,payload,sizeof(payload),&length);
    if (rc==-2) { send_err(BEE_DAL_ERR_DATA); return; }
    if (rc!=0) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
    send_ack(f,payload,(uint16_t)length);
}

static bool set_hw_rtc(time_t epoch)
{
    int fd = open(DAL_OBC_RTC_DEVICE, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[BEE-DAL] sys_rtc: open %s failed: %s\n",
                DAL_OBC_RTC_DEVICE, strerror(errno));
        return false;
    }

    struct tm tm_utc;
    gmtime_r(&epoch, &tm_utc);

    struct rtc_time rtc_tm;
    memset(&rtc_tm, 0, sizeof(rtc_tm));
    rtc_tm.tm_sec  = tm_utc.tm_sec;
    rtc_tm.tm_min  = tm_utc.tm_min;
    rtc_tm.tm_hour = tm_utc.tm_hour;
    rtc_tm.tm_mday = tm_utc.tm_mday;
    rtc_tm.tm_mon  = tm_utc.tm_mon;
    rtc_tm.tm_year = tm_utc.tm_year;

    bool ok = (ioctl(fd, RTC_SET_TIME, &rtc_tm) == 0);
    if (!ok)
        fprintf(stderr, "[BEE-DAL] sys_rtc: RTC_SET_TIME on %s failed: %s\n",
                DAL_OBC_RTC_DEVICE, strerror(errno));

    close(fd);
    return ok;
}

static void handle_sys_rtc(const bee_dal_tc_frame_t *f)
{
    if (f->length != 0 && f->length != 4) {
        send_err(BEE_DAL_ERR_TC_INVALID);
        return;
    }
    if (f->length == 4) {
        size_t off = 0;
        uint32_t epoch = bee_dal_get_u32(f->payload, &off);
        if (rtc_simulated()) {
            time_t host_now = time(NULL);
            if (host_now == (time_t)-1) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
            atomic_store(&g_rtc_sim_offset, (int64_t)epoch - (int64_t)host_now);
        } else {
            struct timespec ts = { .tv_sec = (time_t)epoch, .tv_nsec = 0 };
            if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
                fprintf(stderr, "[BEE-DAL] sys_rtc: clock_settime failed: %s\n", strerror(errno));
                send_err(BEE_DAL_ERR_UNKNOWN);
                return;
            }
            (void)set_hw_rtc((time_t)epoch);
        }
    }

    time_t now = time(NULL);
    if (now == (time_t)-1) { send_err(BEE_DAL_ERR_UNKNOWN); return; }
    if (rtc_simulated()) now = (time_t)((int64_t)now + atomic_load(&g_rtc_sim_offset));
    uint8_t payload[4];
    size_t  poff = 0;
    bee_dal_put_u32(payload, &poff, (uint32_t)now);
    send_ack(f, payload, (uint16_t)poff);
}

/* =========================================================================
 * Data transfer state -- snapshot generation id from the last dat_lst
 * ========================================================================= */
static uint32_t g_dat_snap_id      = 0;
static bool     g_dat_selected     = false;
static uint8_t  g_dat_data_type    = 0;
static uint32_t g_dat_index        = 0;
static uint16_t g_dat_chunk_size   = 0;

static void handle_dat_lst(const bee_dal_tc_frame_t *f)
{
    g_dat_selected = false;

    uint8_t mark_filt = BEE_DAL_FILES_MARK_ALL;
    if (f->length >= 1) {
        size_t off = 0;
        mark_filt = bee_dal_get_u8(f->payload, &off);
        if (f->length != 1) {
            fprintf(stderr, "[BEE-DAL] dat_lst: TC length=%u, expected 0 or 1 -- "
                            "using first byte as filter, ignoring %u extra\n",
                    (unsigned)f->length, (unsigned)(f->length - 1));
        }
    }

    uint32_t hk_count = 0, sci1_count = 0, sci2_count = 0;
    if (bee_dal_files_scan(mark_filt, &g_dat_snap_id, &hk_count, &sci1_count, &sci2_count) != XERR_OK) {
        send_err(BEE_DAL_ERR_DATA); return;
    }

    uint8_t payload[12];
    size_t  off = 0;
    bee_dal_put_u32(payload, &off, hk_count);
    bee_dal_put_u32(payload, &off, sci1_count);
    bee_dal_put_u32(payload, &off, sci2_count);
    send_ack(f, payload, (uint16_t)off);
}

static void handle_dat_sel(const bee_dal_tc_frame_t *f)
{
    if (f->length < 5) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (f->length != 5) {
        /* Diagnostic only -- does not change accept/reject behavior. */
        fprintf(stderr, "[BEE-DAL] dat_sel: TC length=%u, expected exactly 5 -- "
                        "using first 5 bytes, ignoring %u extra\n",
                (unsigned)f->length, (unsigned)(f->length - 5));
    }

    g_dat_selected = false;

    size_t   off        = 0;
    uint8_t  data_type   = bee_dal_get_u8(f->payload, &off);
    uint32_t data_index  = bee_dal_get_u16(f->payload, &off);
    uint16_t chunk_size  = bee_dal_get_u16(f->payload, &off);

    if (data_type > BEE_DAL_DATA_TYPE_SCI2) { send_err(BEE_DAL_ERR_TC_INVALID); return; }

    if (chunk_size == 0 || chunk_size > BEE_DAL_MAX_PAYLOAD) {
        fprintf(stderr, "[BEE-DAL] dat_sel: chunk_size=%u exceeds current max %u\n",
                chunk_size, (unsigned)BEE_DAL_MAX_PAYLOAD);
        send_err(BEE_DAL_ERR_DATA);
        return;
    }

    bee_dal_file_entry_t entry;
    xerr_t err = bee_dal_files_get_entry(g_dat_snap_id, data_type, data_index, &entry);
    if (err != XERR_OK) {
        send_err(err == XERR_STALE ? BEE_DAL_ERR_UNKNOWN : BEE_DAL_ERR_DATA);
        return;
    }

    uint32_t size = 0, crc32 = 0;
    if (bee_dal_files_info(g_dat_snap_id, data_type, data_index, &size, &crc32) != XERR_OK) {
        send_err(BEE_DAL_ERR_DATA);
        return;
    }

    g_dat_selected   = true;
    g_dat_data_type  = data_type;
    g_dat_index      = data_index;
    g_dat_chunk_size = chunk_size;

    uint32_t chunk_count = (size + chunk_size - 1) / chunk_size;
    if (chunk_count == 0) chunk_count = 1;

    uint8_t payload[41];
    size_t  poff = 0;
    bee_dal_put_u8 (payload, &poff, data_type);
    bee_dal_put_u32(payload, &poff, data_index);
    bee_dal_put_u32(payload, &poff, chunk_size);
    bee_dal_put_str(payload, &poff, entry.name, 20);
    bee_dal_put_u32(payload, &poff, size);
    bee_dal_put_u32(payload, &poff, crc32);
    bee_dal_put_u32(payload, &poff, chunk_count);

    send_ack(f, payload, (uint16_t)poff);
}

/* dat_get (TYPE=0x03, ADDR=0x02): TC payload = Chunk Index(U32). Streams
 * one chunk of the file currently selected by dat_sel. */
static void handle_dat_get(const bee_dal_tc_frame_t *f)
{
    if (f->length < 4) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (f->length != 4) {
        fprintf(stderr, "[BEE-DAL] dat_get: TC length=%u, expected exactly 4 -- "
                        "using first 4 bytes, ignoring %u extra\n",
                (unsigned)f->length, (unsigned)(f->length - 4));
    }

    if (!g_dat_selected) { send_err(BEE_DAL_ERR_DATA); return; }

    size_t   off         = 0;
    uint32_t chunk_index = bee_dal_get_u32(f->payload, &off);

    uint8_t  chunk_buf[BEE_DAL_MAX_PAYLOAD];
    uint16_t chunk_len = 0;
    xerr_t err = bee_dal_files_read_chunk(g_dat_snap_id, g_dat_data_type, g_dat_index,
                                          chunk_index, g_dat_chunk_size,
                                          chunk_buf, &chunk_len);
    if (err != XERR_OK) {
        send_err(err == XERR_STALE ? BEE_DAL_ERR_UNKNOWN : BEE_DAL_ERR_DATA);
        return;
    }

    send_ack(f, chunk_buf, chunk_len);
}

static void handle_dat_des(const bee_dal_tc_frame_t *f)
{
    g_dat_selected = false;
    send_ack_uptime(f);
}

static void handle_dat_del(const bee_dal_tc_frame_t *f)
{
    if (f->length != 5) { send_err(BEE_DAL_ERR_TC_INVALID); return; }

    size_t   off        = 0;
    uint8_t  data_type   = bee_dal_get_u8(f->payload, &off);
    uint32_t data_index  = bee_dal_get_u32(f->payload, &off);

    if (data_type > BEE_DAL_DATA_TYPE_SCI2) { send_err(BEE_DAL_ERR_TC_INVALID); return; }

    uint32_t hk_count = 0, sci1_count = 0, sci2_count = 0;
    xerr_t err = bee_dal_files_delete(g_dat_snap_id, data_type, data_index,
                                      &hk_count, &sci1_count, &sci2_count);
    if (err != XERR_OK) {
        send_err(err == XERR_STALE ? BEE_DAL_ERR_UNKNOWN : BEE_DAL_ERR_DATA);
        return;
    }

    g_dat_snap_id++;
    g_dat_selected = false;

    uint8_t payload[12];
    size_t  poff = 0;
    bee_dal_put_u32(payload, &poff, hk_count);
    bee_dal_put_u32(payload, &poff, sci1_count);
    bee_dal_put_u32(payload, &poff, sci2_count);
    send_ack(f, payload, (uint16_t)poff);
}

static void handle_dat_mark(const bee_dal_tc_frame_t *f)
{
    if (f->length < 1) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (f->length != 1) {
        fprintf(stderr, "[BEE-DAL] dat_mark: TC length=%u, expected exactly 1 -- "
                        "using first byte, ignoring %u extra\n",
                (unsigned)f->length, (unsigned)(f->length - 1));
    }

    size_t  off  = 0;
    uint8_t mark = bee_dal_get_u8(f->payload, &off);

    if (mark == BEE_DAL_MARK_RESERVED) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    if (!g_dat_selected) { send_err(BEE_DAL_ERR_DATA); return; }

    xerr_t err = bee_dal_files_mark(g_dat_snap_id, g_dat_data_type, g_dat_index, mark);
    if (err != XERR_OK) {
        send_err(err == XERR_STALE ? BEE_DAL_ERR_UNKNOWN : BEE_DAL_ERR_DATA);
        return;
    }

    send_ack_uptime(f);
}

/* tm_get (TYPE=0x04, ADDR=0x00): 29-byte payload */
static void handle_tm_get(const bee_dal_tc_frame_t *f)
{
    bee_dal_telemetry_t tm;
    hk_service_get_telemetry(&tm);
    if (!tm.obc_snapshot_valid) { send_err(BEE_DAL_ERR_DATA); return; }

    uint8_t payload[29];
    size_t  off = 0;
    bee_dal_put_u32(payload, &off, tm.sys_time_epoch);
    bee_dal_put_i8 (payload, &off, tm.sys_status);
    bee_dal_put_u32(payload, &off, tm.bootcount);
    bee_dal_put_i16(payload, &off, tm.onboard_temp_c);
    bee_dal_put_i16(payload, &off, tm.baro_temp_c);
    bee_dal_put_u16(payload, &off, tm.baro_press_pa);
    bee_dal_put_u16(payload, &off, tm.baro_hum_rh);
    bee_dal_put_i16(payload, &off, tm.mlx_temp_c);
    bee_dal_put_u16(payload, &off, tm.mlx_status);
    bee_dal_put_i16(payload, &off, tm.slf_temp_c);
    bee_dal_put_i16(payload, &off, tm.slf_flow_rate);
    bee_dal_put_u16(payload, &off, tm.rba_status);
    bee_dal_put_u16(payload, &off, tm.ta_status);

    send_ack(f, payload, (uint16_t)off);
}

static void handle_tm_obc_get(const bee_dal_tc_frame_t *f)
{
    if (f->length != 0) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    bee_dal_telemetry_t snapshot;
    uint8_t payload[OBC_HK_WIRE_LEN];
    hk_service_get_telemetry(&snapshot);
    if (!snapshot.obc_snapshot_valid) { send_err(BEE_DAL_ERR_DATA); return; }
    obc_hk_encode(&snapshot, payload);
    send_ack(f, payload, OBC_HK_WIRE_LEN);
}

/* =========================================================================
 * Debug handoff
 * ========================================================================= */
#define BEE_DAL_DEBUG_HANDOFF_TYPE 0xFFU /* also used by debug shell, below */

#ifndef BEE_DAL_DEBUG_HANDOFF_DISABLE

static const uint8_t BEE_DAL_DEBUG_HANDOFF_MAGIC[] = "DEBUG_HANDOFF_OK";
#define BEE_DAL_DEBUG_HANDOFF_MAGIC_LEN (sizeof(BEE_DAL_DEBUG_HANDOFF_MAGIC) - 1U)

static volatile bool g_debug_handoff_requested = false;

static bool is_debug_handoff_frame(const bee_dal_tc_frame_t *f)
{
    return f->type   == BEE_DAL_DEBUG_HANDOFF_TYPE &&
           f->length == BEE_DAL_DEBUG_HANDOFF_MAGIC_LEN &&
           memcmp(f->payload, BEE_DAL_DEBUG_HANDOFF_MAGIC, BEE_DAL_DEBUG_HANDOFF_MAGIC_LEN) == 0;
}

static void handle_debug_handoff(const bee_dal_tc_frame_t *f)
{
    printf("[BEE-DAL] Debug handoff magic frame received -- acking, then requesting "
           "main.c hand %s to %s\n", DAL_OBC_RS422_PORT, DAL_OBC_DEBUG_GETTY_UNIT);

    uint8_t payload[4];
    size_t  off = 0;
    bee_dal_put_u32(payload, &off, read_uptime_sec());
    send_debug_ack(f, payload, (uint16_t)off);

    g_debug_handoff_requested = true;
}

#endif /* BEE_DAL_DEBUG_HANDOFF_DISABLE */

bool bee_dal_tmtc_service_debug_handoff_requested(void)
{
#ifndef BEE_DAL_DEBUG_HANDOFF_DISABLE
    return g_debug_handoff_requested;
#else
    return false;
#endif
}

/* =========================================================================
 * Debug shell
 * ========================================================================= */
#ifndef BEE_DAL_DEBUG_SHELL_DISABLE

#include <sys/wait.h>

#define BEE_DAL_DEBUG_SHELL_ADDR    0x01U
#define BEE_DAL_DEBUG_SHELL_MAX_CMD 2048U
#define BEE_DAL_DEBUG_SHELL_MAX_OUT 20480U
#define BEE_DAL_DEBUG_SHELL_HDR_LEN 7U

static uint8_t g_shell_tm[BEE_DAL_DEBUG_SHELL_HDR_LEN + BEE_DAL_DEBUG_SHELL_MAX_OUT];

static void handle_debug_shell(const bee_dal_tc_frame_t *f)
{
    if (f->length == 0 || f->length > BEE_DAL_DEBUG_SHELL_MAX_CMD) {
        send_err(BEE_DAL_ERR_TC_INVALID);
        return;
    }

    char cmd[BEE_DAL_DEBUG_SHELL_MAX_CMD + 1];
    memcpy(cmd, f->payload, f->length);
    cmd[f->length] = '\0';

    int pipefd[2];
    if (pipe(pipefd) != 0) { send_err(BEE_DAL_ERR_UNKNOWN); return; }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        send_err(BEE_DAL_ERR_UNKNOWN);
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);

    uint8_t *out       = g_shell_tm + BEE_DAL_DEBUG_SHELL_HDR_LEN;
    uint32_t out_len    = 0;
    bool     truncated  = false;
    uint8_t  rdbuf[512];
    ssize_t  n;
    while ((n = read(pipefd[0], rdbuf, sizeof(rdbuf))) > 0) {
        uint32_t space = BEE_DAL_DEBUG_SHELL_MAX_OUT - out_len;
        uint32_t take  = ((uint32_t)n < space) ? (uint32_t)n : space;
        if (take > 0) {
            memcpy(out + out_len, rdbuf, take);
            out_len += take;
        }
        if (take < (uint32_t)n) truncated = true;
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    uint8_t exit_status = 3; /* exec/fork/pipe error, default until classified */
    uint8_t exit_code   = 0;
    if (WIFEXITED(status)) {
        exit_status = 0;
        exit_code   = (uint8_t)WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_status = 1;
        exit_code   = (uint8_t)WTERMSIG(status);
    }

    size_t off = 0;
    bee_dal_put_u8 (g_shell_tm, &off, exit_status);
    bee_dal_put_u8 (g_shell_tm, &off, exit_code);
    bee_dal_put_u8 (g_shell_tm, &off, truncated ? 1 : 0);
    bee_dal_put_u32(g_shell_tm, &off, out_len);

    send_debug_ack(f, g_shell_tm, (uint16_t)(BEE_DAL_DEBUG_SHELL_HDR_LEN + out_len));
}

#endif /* BEE_DAL_DEBUG_SHELL_DISABLE */

/* CSP commands are queued; neither the RS-422 nor local dispatcher waits for CAN. */
static void handle_exp_csp_submit(const bee_dal_tc_frame_t *f)
{
    if (f->length < 5 || f->length > 230) {
        send_err(BEE_DAL_ERR_TC_INVALID); return;
    }
    char command[231];
    for (unsigned i = 0; i < f->length; i++) {
        if (f->payload[i] < 0x20 || f->payload[i] > 0x7e) {
            send_err(BEE_DAL_ERR_TC_INVALID); return;
        }
    }
    memcpy(command, f->payload, f->length);
    command[f->length] = 0;
    unsigned table = 0;
    char field[96] = {0}, extra[2] = {0};
    char value[128] = {0};
    exp_csp_op_t op;
    const char *argument = NULL;
    if (!strncmp(command, "get ", 4)) {
        if (sscanf(command + 4, "%u %95s %1s", &table, field, extra) != 2) {
            send_err(BEE_DAL_ERR_TC_INVALID); return;
        }
        op = EXP_CSP_GET;
        argument = field;
    } else if (!strncmp(command, "set ", 4)) {
        int consumed = 0;
        if (sscanf(command + 4, "%u %95s %n", &table, field, &consumed) != 2 ||
            consumed <= 0 || !command[4 + consumed] ||
            strlen(command + 4 + consumed) >= sizeof(value)) {
            send_err(BEE_DAL_ERR_TC_INVALID); return;
        }
        strcpy(value, command + 4 + consumed);
        op = EXP_CSP_SET;
        argument = field;
    } else if (!strncmp(command, "run ", 4)) {
        op = EXP_CSP_RUN;
        argument = command + 4;
    } else if (!strcmp(command, "refresh 0")) {
        op = EXP_CSP_REFRESH;
        table = 0;
        argument = NULL;
    } else {
        send_err(BEE_DAL_ERR_TC_INVALID); return;
    }
    uint32_t ticket;
    if (exp_csp_submit(op, table, argument,
                       op == EXP_CSP_SET ? value : NULL, &ticket) != 0) {
        send_err(BEE_DAL_ERR_DATA); return;
    }
    uint8_t payload[4];
    size_t off = 0;
    bee_dal_put_u32(payload, &off, ticket);
    send_ack(f, payload, sizeof(payload));
}

static void handle_exp_csp_result(const bee_dal_tc_frame_t *f)
{
    if (f->length != 4) { send_err(BEE_DAL_ERR_TC_INVALID); return; }
    size_t off = 0;
    uint32_t ticket = bee_dal_get_u32(f->payload, &off);
    exp_csp_state_t state;
    char output[1024];
    if (exp_csp_result(ticket, &state, output, sizeof(output)) != 0) {
        send_err(BEE_DAL_ERR_DATA); return;
    }
    size_t n = strlen(output);
    uint8_t payload[3 + 1023];
    payload[0] = (uint8_t)state;
    payload[1] = (uint8_t)n;
    payload[2] = (uint8_t)(n >> 8);
    memcpy(payload + 3, output, n);
    send_ack(f, payload, (uint16_t)(n + 3));
}

/* 02/0F returns cached EXP telemetry without waiting for a CAN transaction. */
static void handle_exp_csp_snapshot(const bee_dal_tc_frame_t *f)
{
    if (f->length != 1 || (f->payload[0] != 0 && f->payload[0] != 2 && f->payload[0] != 3 && f->payload[0] != 6 &&
                           f->payload[0] != 12)) {
        send_err(BEE_DAL_ERR_TC_INVALID); return;
    }
    char json[4608];
    if (exp_csp_snapshot_json(f->payload[0], json, sizeof(json)) != 0) {
        send_err(BEE_DAL_ERR_UNKNOWN); return;
    }
    send_ack(f, (const uint8_t *)json, (uint16_t)strlen(json));
}

/* =========================================================================
 * Dispatch table
 * ========================================================================= */
static void dispatch(const bee_dal_tc_frame_t *f)
{
#ifndef BEE_DAL_DEBUG_HANDOFF_DISABLE
    if (is_debug_handoff_frame(f)) {
        handle_debug_handoff(f);
        return;
    }
#endif
#ifndef BEE_DAL_DEBUG_SHELL_DISABLE
    if (f->type == BEE_DAL_DEBUG_HANDOFF_TYPE && f->addr == BEE_DAL_DEBUG_SHELL_ADDR) {
        handle_debug_shell(f);
        return;
    }
#endif

    switch (f->type) {

    case BEE_DAL_TYPE_PING:
        if (f->addr == 0x00) { handle_ping(f); return; }
        break;

    case BEE_DAL_TYPE_SYS_CFG:
        switch (f->addr) {
        case 0x00: handle_sys_info(f); return;
        case 0x01: handle_sys_rtc(f);  return;
        case 0x02: handle_pwr_adj_cfg(f); return;
        case 0x03: handle_power_en(f); return;
        case 0x04: handle_power_stat(f); return;
        case 0x07: handle_sensor_stat(f); return;
        case 0x10: handle_uvc_cfg(f); return;
        case 0x11: handle_uvc_ctl(f); return;
        case 0x12: handle_uvc_stat(f); return;
        case 0x13: handle_thm_cfg(f); return;
        case 0x14: handle_thm_ctl(f); return;
        case 0x15: handle_thm_stat(f); return;
        case 0x16: handle_fld_cfg(f); return;
        case 0x17: handle_fld_ctl(f); return;
        case 0x18: handle_fld_stat(f); return;
        case 0x1a: handle_rba_ctl(f); return;
        case 0x1b: handle_rba_stat(f); return;
        case 0x1c: handle_pdu_fault_clear(f); return;
        case 0x1d: handle_pdu_heartbeat(f); return;
        case 0x1e: handle_pdu_bootloader(f); return;
        default:
            send_err(BEE_DAL_ERR_UNKNOWN); 
            return;
        }

    case BEE_DAL_TYPE_DATA_XFER:
        switch (f->addr) {
        case BEE_DAL_DAT_LST: handle_dat_lst(f); return;
        case BEE_DAL_DAT_SEL: handle_dat_sel(f); return;
        case BEE_DAL_DAT_GET: handle_dat_get(f); return;
        case BEE_DAL_DAT_DES: handle_dat_des(f); return;
        case BEE_DAL_DAT_DEL: handle_dat_del(f); return;
        case BEE_DAL_DAT_MARK: handle_dat_mark(f); return;
        default: break;
        }
        break;

    case BEE_DAL_TYPE_TELEMETRY:
        switch (f->addr) {
        case BEE_DAL_TM_GET: handle_tm_get(f); return;
        case BEE_DAL_TM_OBC_GET: handle_tm_obc_get(f); return;
        case BEE_DAL_TM_CFG: send_err(BEE_DAL_ERR_UNKNOWN); return; 
        default: break;
        }
        break;

    case BEE_DAL_TYPE_EXP_CTRL:
        if (f->addr==0x00) {handle_pc_stat(f);return;}
        if (f->addr==0x01) {handle_pc_cfg(f);return;}
        if (f->addr==0x02) {handle_pc_ctl(f);return;}
        if (f->addr==0x03) {handle_thp_cfg(f);return;}
        if (f->addr==0x04) {handle_thp_ctl(f);return;}
        if (f->addr==BEE_DAL_EXP_CSP_SUBMIT) {handle_exp_csp_submit(f);return;}
        if (f->addr==BEE_DAL_EXP_CSP_RESULT) {handle_exp_csp_result(f);return;}
        if (f->addr==BEE_DAL_EXP_CSP_SNAPSHOT) {handle_exp_csp_snapshot(f);return;}
        if (f->addr >= BEE_DAL_EXP_PING && f->addr <= BEE_DAL_EXP_RUN) {
            handle_exp_gateway(f);
            return;
        }
        send_err(BEE_DAL_ERR_UNKNOWN);
        return;
    case BEE_DAL_TYPE_SFU:  
        send_err(BEE_DAL_ERR_UNKNOWN);
        return;

    default:
        break;
    }

    send_err(BEE_DAL_ERR_TC_INVALID);
}

/* =========================================================================
 * RX thread
 * ========================================================================= */
static double elapsed_ms_since(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) * 1000.0
         + (double)(now.tv_nsec - start->tv_nsec) / 1.0e6;
}

typedef struct {
    bee_dal_decoder_t decoder;
    uint8_t raw[BEE_DAL_TC_HDR_LEN + BEE_DAL_MAX_PAYLOAD + 2];
    size_t raw_len;
} rx_state_t;

static void rx_state_init(rx_state_t *state)
{
    memset(state, 0, sizeof(*state));
    bee_dal_decoder_init(&state->decoder);
}

static void rx_idle(rx_state_t *state)
{
    if (bee_dal_decoder_idle(&state->decoder, DAL_OBC_RS422_READ_TIMEOUT_MS) ==
        BEE_DAL_DEC_TIMEOUT) {
        fprintf(stderr, "[BEE-DAL] inter-byte timeout -- decoder resynced\n");
        state->raw_len = 0;
    }
}

static void rx_byte(rx_state_t *state, uint8_t byte, int reply_fd)
{
    if (state->raw_len == 0 && byte != BEE_DAL_START_BYTE) {
        /* Garbage before the frame start is discarded by the shared decoder. */
    } else if (state->raw_len < sizeof(state->raw)) {
        state->raw[state->raw_len++] = byte;
    }

    bee_dal_tc_frame_t frame;
    bee_dal_dec_result_t result = bee_dal_decoder_feed(&state->decoder, byte, &frame);
    if (result == BEE_DAL_DEC_FRAME_OK) {
        bool trace = g_verbose && !(g_quiet_housekeeping && is_housekeeping_read(&frame));
        if (trace) {
            printf("[RX] frame (%zu bytes):\n", state->raw_len);
            hex_dump(state->raw, state->raw_len);
            printf("[RX] TYPE=0x%02X  ADDR=0x%02X  LENGTH=%u\n",
                   frame.type, frame.addr, frame.length);
            if (frame.length > 0) {
                printf("[RX] payload:\n");
                hex_dump(frame.payload, frame.length);
            }
        }
        state->raw_len = 0;
        struct timespec started;
        clock_gettime(CLOCK_MONOTONIC, &started);
        pthread_mutex_lock(&g_dispatch_lock);
        g_reply_fd = reply_fd;
        g_suppress_hk_trace = !trace && g_verbose;
        dispatch(&frame);
        g_suppress_hk_trace = false;
        g_reply_fd = -1;
        pthread_mutex_unlock(&g_dispatch_lock);
        if (trace)
            printf("[RX] processed in %.2f ms\n", elapsed_ms_since(&started));
    } else if (result == BEE_DAL_DEC_CRC_ERROR) {
        if (g_verbose) {
            printf("[RX] CRC error, frame discarded (%zu bytes):\n", state->raw_len);
            hex_dump(state->raw, state->raw_len);
        } else {
            fprintf(stderr, "[BEE-DAL] CRC error, frame discarded\n");
        }
        state->raw_len = 0;
    }
}

static void *rx_thread(void *arg)
{
    (void)arg;
    rx_state_t state;
    rx_state_init(&state);
    printf("[BEE-DAL] RX thread started on %s\n", serial_port());
    while (g_running) {
        uint8_t byte;
        ssize_t n = read(g_fd, &byte, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[BEE-DAL] read error: %s -- RX thread exiting\n", strerror(errno));
            g_running = false;
            break;
        }
        if (n == 0) { rx_idle(&state); continue; }
        rx_byte(&state, byte, -1);
    }
    printf("[BEE-DAL] RX thread stopped\n");
    return NULL;
}

static int open_frame_socket(const char *path)
{
    if (!path || path[0] != '/') { errno = EINVAL; return -1; }
    if (strlen(path) >= sizeof(g_socket_path)) { errno = ENAMETOOLONG; return -1; }
    const char *console_override = getenv("DAL_FLIGHT_CONSOLE_SOCKET");
    const char *console_path = console_override && *console_override ?
        console_override : DAL_OBC_CONSOLE_SOCKET;
    if (strcmp(path, console_path) == 0) { errno = EINVAL; return -1; }
    struct stat st;
    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) { errno = EEXIST; return -1; }
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (probe < 0) return -1;
        struct sockaddr_un existing = { .sun_family = AF_UNIX };
        strcpy(existing.sun_path, path);
        int connected = connect(probe, (struct sockaddr *)&existing, sizeof(existing));
        int reason = errno;
        close(probe);
        if (connected == 0) { errno = EADDRINUSE; return -1; }
        if (reason != ECONNREFUSED && reason != ENOENT) { errno = reason; return -1; }
        if (unlink(path) != 0) return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy(addr.sun_path, path);
    mode_t old_umask = umask(0177);
    int bound = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    umask(old_umask);
    if (bound != 0 || chmod(path, 0600) != 0 || listen(fd, 4) != 0) {
        close(fd);
        if (bound == 0) unlink(path);
        return -1;
    }
    strcpy(g_socket_path, path);
    return fd;
}

static void *socket_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        struct pollfd listener = { .fd = g_socket_fd, .events = POLLIN };
        int ready = poll(&listener, 1, 200);
        if (ready < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[BEE-DAL] socket poll failed: %s\n", strerror(errno));
            g_running = false;
            break;
        }
        if (!ready || !(listener.revents & POLLIN)) continue;
        int client = accept4(g_socket_fd, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            fprintf(stderr, "[BEE-DAL] socket accept failed: %s\n", strerror(errno));
            g_running = false;
            break;
        }
        struct timeval send_timeout = { .tv_sec = 5 };
        if (setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                       &send_timeout, sizeof(send_timeout)) != 0) {
            close(client);
            continue;
        }
        rx_state_t state;
        rx_state_init(&state);
        while (g_running) {
            struct pollfd peer = { .fd = client, .events = POLLIN | POLLHUP };
            ready = poll(&peer, 1, 200);
            if (ready < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (!ready) { rx_idle(&state); continue; }
            uint8_t bytes[512];
            ssize_t count = read(client, bytes, sizeof(bytes));
            if (count <= 0) break;
            for (ssize_t i = 0; i < count; i++) rx_byte(&state, bytes[i], client);
        }
        close(client);
    }
    return NULL;
}

/* =========================================================================
 * Public API
 * ========================================================================= */
xerr_t bee_dal_tmtc_service_init(void)
{
    /* DAL_DATA_DIR redirects the ICD file backend (used by the host test framework). */
    const char *data_dir = getenv("DAL_DATA_DIR");
    bee_dal_files_init(data_dir && *data_dir ? data_dir : BEEIF_DATA_DIR);

    g_fd = open_serial(serial_port(), DAL_OBC_RS422_BAUD);
    if (g_fd < 0) {
        fprintf(stderr, "[BEE-DAL] Failed to open %s: %s\n",
                serial_port(), strerror(errno));
        return XERR_IO;
    }
    printf("[BEE-DAL] Opened %s @ %d bps 8N1\n", serial_port(), DAL_OBC_RS422_BAUD_NUM);
    return XERR_OK;
}

xerr_t bee_dal_tmtc_service_start(void)
{
    if (g_running) {
        fprintf(stderr, "[BEE-DAL] Already running\n");
        return XERR_INVAL;
    }
    if (g_fd < 0) {
        fprintf(stderr, "[BEE-DAL] Not initialized\n");
        return XERR_STATE;
    }

    const char *path = getenv("DAL_OBC_RS422_SOCKET");
    if (path && *path) {
        g_socket_fd = open_frame_socket(path);
        if (g_socket_fd < 0) {
            fprintf(stderr, "[BEE-DAL] Failed to open frame socket %s: %s\n",
                    path, strerror(errno));
            return XERR_IO;
        }
    }

    g_running = true;
    if (pthread_create(&g_rx_thread, NULL, rx_thread, NULL) != 0) {
        fprintf(stderr, "[BEE-DAL] Failed to create RX thread\n");
        g_running = false;
        if (g_socket_fd >= 0) {
            close(g_socket_fd); g_socket_fd = -1;
            unlink(g_socket_path); g_socket_path[0] = 0;
        }
        return XERR_IO;
    }
    g_rx_started = true;
    if (g_socket_fd >= 0) {
        if (pthread_create(&g_socket_thread, NULL, socket_thread, NULL) != 0) {
            fprintf(stderr, "[BEE-DAL] Failed to create frame socket thread\n");
            g_running = false;
            pthread_join(g_rx_thread, NULL);
            g_rx_started = false;
            close(g_socket_fd); g_socket_fd = -1;
            unlink(g_socket_path); g_socket_path[0] = 0;
            return XERR_IO;
        }
        g_socket_started = true;
        printf("[BEE-DAL] Frame socket listening on %s\n", g_socket_path);
    }

    printf("[BEE-DAL] Started\n");
    return XERR_OK;
}

void bee_dal_tmtc_service_stop(void)
{
    if (!g_rx_started && !g_socket_started && g_fd < 0) return;
    printf("[BEE-DAL] Stopping...\n");
    g_running = false;
    if (g_socket_started) {
        pthread_join(g_socket_thread, NULL);
        g_socket_started = false;
    }
    if (g_rx_started) {
        pthread_join(g_rx_thread, NULL);
        g_rx_started = false;
    }
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
        g_socket_fd = -1;
        unlink(g_socket_path);
        g_socket_path[0] = 0;
    }
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    printf("[BEE-DAL] Stopped\n");
}

bool bee_dal_tmtc_service_is_running(void) { return g_running; }

int bee_dal_tmtc_execute_local(uint8_t type, uint8_t addr,
                               const uint8_t *payload, uint16_t length,
                               bee_dal_local_reply_t *reply)
{
    if (!reply || length > BEE_DAL_MAX_PAYLOAD || (length && !payload))
        return XERR_INVAL;
    bee_dal_tc_frame_t frame = { .type = type, .addr = addr, .length = length };
    if (length) memcpy(frame.payload, payload, length);
    memset(reply, 0, sizeof(*reply));
    pthread_mutex_lock(&g_dispatch_lock);
    g_local_reply = reply;
    dispatch(&frame);
    g_local_reply = NULL;
    pthread_mutex_unlock(&g_dispatch_lock);
    return reply->ready ? XERR_OK : XERR_IO;
}
