#include "services/flight_console_service.h"
#include "services/bee_dal_tmtc_service.h"
#include "services/gui_state.h"
#include "services/sys_info_wire.h"
#include "services/exp_csp_service.h"
#include "services/port_config.h"
#include "dal_obc_config.h"
#include "bee_dal_proto.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#define REQUEST_MAX 4096
#define RESPONSE_MAX 140000
#define CONSOLE_LOG_PREVIEW 4096

extern bool g_verbose;
extern bool g_quiet_housekeeping;

static int g_listen_fd = -1;
static pthread_t g_thread;
static volatile bool g_running;
static char g_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_escaped(const char *data, size_t length)
{
    size_t shown = length < CONSOLE_LOG_PREVIEW ? length : CONSOLE_LOG_PREVIEW;
    for (size_t i = 0; i < shown; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\n') fputs("\\n", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c < 32 || c >= 127) printf("\\x%02X", c);
        else putchar(c);
    }
    if (shown < length) fputs("... [truncated]", stdout);
}

static void log_exchange(const char *request, const char *response,
                         size_t response_length, bool quiet_read, bool success)
{
    if (!g_verbose || (g_quiet_housekeeping && quiet_read && success)) return;
    pthread_mutex_lock(&g_log_lock);
    fputs("[CONSOLE] RX: ", stdout);
    log_escaped(request, strlen(request));
    printf("\n[CONSOLE] TX (%zu bytes): ", response_length);
    log_escaped(response, response_length);
    putchar('\n');
    fflush(stdout);
    pthread_mutex_unlock(&g_log_lock);
}

static void send_logged(int fd, const char *request, const char *response,
                        size_t length, bool quiet_read, bool success)
{
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, response + sent, length - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += (size_t)n;
    }
    log_exchange(request, response, sent, quiet_read, success && sent == length);
}

static bool routine_console_read(uint8_t type, uint8_t addr)
{
    if (type == BEE_DAL_TYPE_TELEMETRY)
        return addr == BEE_DAL_TM_GET || addr == BEE_DAL_TM_OBC_GET;
    if (type == BEE_DAL_TYPE_SYS_CFG) return addr == 0x04 || addr == 0x07;
    return type == BEE_DAL_TYPE_EXP_CTRL &&
           (addr == BEE_DAL_EXP_TLM || addr == BEE_DAL_EXP_CSP_SNAPSHOT);
}

static void append(char *out, size_t cap, size_t *used, const char *fmt, ...)
{
    if (*used >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *used, cap - *used, fmt, ap);
    va_end(ap);
    if (n > 0) *used += (size_t)n < cap - *used ? (size_t)n : cap - *used - 1;
}

static void put_json_string(char *out, size_t cap, size_t *used, const char *s)
{
    append(out, cap, used, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') append(out, cap, used, "\\%c", *p);
        else if (*p == '\n') append(out, cap, used, "\\n");
        else if (*p == '\r') append(out, cap, used, "\\r");
        else if (*p == '\t') append(out, cap, used, "\\t");
        else if (*p < 32 || *p >= 127) append(out, cap, used, "\\u%04x", *p);
        else append(out, cap, used, "%c", *p);
    }
    append(out, cap, used, "\"");
}
static void handle_gui_shell(int fd, const char *request, const char *logged_request)
{
    char *end;
    errno=0;
    unsigned long seconds=strtoul(request,&end,10);
    if (errno || end==request || *end!=' ' || seconds<1 || seconds>120 || !end[1] || strlen(end+1)>2048) {
        const char *error="{\"ok\":false,\"error\":\"use gui_shell <timeout 1..120> <command>\"}\n";
        send_logged(fd,logged_request,error,strlen(error),false,false);return;
    }
    const char *command=end+1;
    int pipes[2];if (pipe(pipes)!=0) {
        const char *error="{\"ok\":false,\"error\":\"pipe failed\"}\n";
        send_logged(fd,logged_request,error,strlen(error),false,false);return;
    }
    pid_t pid=fork();
    if (pid<0) {
        close(pipes[0]);close(pipes[1]);
        const char *error="{\"ok\":false,\"error\":\"fork failed\"}\n";
        send_logged(fd,logged_request,error,strlen(error),false,false);return;
    }
    if (!pid) {
        setpgid(0,0);
        close(pipes[0]);dup2(pipes[1],STDOUT_FILENO);dup2(pipes[1],STDERR_FILENO);close(pipes[1]);
        execl("/bin/sh","sh","-c",command,(char *)NULL);_exit(127);
    }
    setpgid(pid,pid);
    close(pipes[1]);
    int flags=fcntl(pipes[0],F_GETFL);fcntl(pipes[0],F_SETFL,flags|O_NONBLOCK);
    char capture[20001];size_t used=0;
    time_t deadline=time(NULL)+(time_t)seconds;
    int status=0;bool done=false,timed_out=false,eof=false;
    while (!done || !eof) {
        struct pollfd p={.fd=pipes[0],.events=POLLIN|POLLHUP};
        poll(&p,1,100);
        char buf[512];ssize_t n;
        while ((n=read(pipes[0],buf,sizeof(buf)))>0) {
            size_t take=(size_t)n<sizeof(capture)-1-used?(size_t)n:sizeof(capture)-1-used;
            if (take) memcpy(capture+used,buf,take),used+=take;
        }
        if (n==0) eof=true;
        if (!done && waitpid(pid,&status,WNOHANG)==pid) done=true;
        if (!done && time(NULL)>=deadline) {
            timed_out=true;kill(-pid,SIGKILL);kill(pid,SIGKILL);
            waitpid(pid,&status,0);done=true;
        }
        if (done && time(NULL)>deadline+1) {kill(-pid,SIGKILL);break;}
    }
    close(pipes[0]);capture[used]=0;
    char response[130000];size_t length=0;response[0]=0;
    int code=WIFEXITED(status)?WEXITSTATUS(status):128;
    append(response,sizeof(response),&length,"{\"ok\":%s,\"exit\":%d,\"timeout\":%s,\"text\":",
           !timed_out && code==0?"true":"false",code,timed_out?"true":"false");
    put_json_string(response,sizeof(response),&length,capture);
    append(response,sizeof(response),&length,"}\n");
    send_logged(fd,logged_request,response,length,false,!timed_out && code==0);
}

struct gui_shell_job {
    int fd;
    char request[REQUEST_MAX + 1];
    char logged_request[REQUEST_MAX + 1];
};
static void *gui_shell_worker(void *argument)
{
    struct gui_shell_job *job=argument;
    handle_gui_shell(job->fd,job->request,job->logged_request);
    close(job->fd);
    free(job);
    return NULL;
}

static int number(const char *s, uint32_t max, uint32_t *value)
{
    if (!s || !*s || *s == '-') return -1;
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 0);
    if (errno || *end || v > max) return -1;
    *value = (uint32_t)v;
    return 0;
}

static int signed_number(const char *s, int32_t *value)
{
    if (!s || !*s) return -1;
    char *end;
    errno=0;
    long long v=strtoll(s,&end,10);
    if (errno || *end || end==s || v<INT32_MIN || v>INT32_MAX) return -1;
    *value=(int32_t)v;
    return 0;
}

static char *token(char **cursor) { return strtok_r(NULL, " \t", cursor); }

static int data_type_arg(const char *arg, uint32_t *type)
{
    if (!arg) return -1;
    if (!strcmp(arg, "hk") || !strcmp(arg, "housekeeping")) { *type = 0; return 0; }
    if (!strcmp(arg, "images") || !strcmp(arg, "sci1")) { *type = 1; return 0; }
    if (!strcmp(arg, "logs") || !strcmp(arg, "sci2") || !strcmp(arg, "data")) { *type = 2; return 0; }
    return number(arg, 2, type);
}

static int thp_u16_arg(char **save,uint8_t *payload,unsigned off,unsigned min,unsigned max)
{
    uint32_t value;
    if (number(token(save),max,&value) || value<min) return -1;
    payload[off]=(uint8_t)value;payload[off+1]=(uint8_t)(value>>8);return 0;
}
static int thp_i16_arg(char **save,uint8_t *payload,unsigned off,int min,int max)
{
    int32_t value;
    if (signed_number(token(save),&value) || value<min || value>max) return -1;
    payload[off]=(uint8_t)value;payload[off+1]=(uint8_t)((uint16_t)value>>8);return 0;
}
static int thp_u8_arg(char **save,uint8_t *payload,unsigned off,unsigned max)
{
    uint32_t value;
    if (number(token(save),max,&value)) return -1;
    payload[off]=(uint8_t)value;return 0;
}
static int thp_gain_arg(char **save,uint8_t *payload,unsigned off)
{
    char *s=token(save),*end;
    if (!s || !*s) return -1;
    errno=0;float value=strtof(s,&end);
    if (errno || *end || !isfinite(value) || value<0) return -1;
    uint32_t bits;memcpy(&bits,&value,4);
    for (unsigned i=0;i<4;i++) payload[off+i]=(uint8_t)(bits>>(8*i));
    return 0;
}

static int bme_os_code(const char *name)
{
    static const char *const choices[] = {"skip", "1x", "2x", "4x", "8x", "16x"};
    if (!name) return -1;
    for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i)
        if (!strcasecmp(name, choices[i])) return (int)i;
    return -1;
}

static int bme_filter_code(const char *name)
{
    static const char *const choices[] = {"off", "1", "3", "7", "15", "31", "63", "127"};
    if (!name) return -1;
    for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i)
        if (!strcasecmp(name, choices[i])) return (int)i;
    return -1;
}

static int imu_odr_code(const char *name)
{
    static const char *const choices[] = {"off", "12.5hz", "26hz", "52hz",
        "104hz", "208hz", "416hz", "833hz"};
    if (!name) return -1;
    for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i)
        if (!strcasecmp(name, choices[i])) return (int)i;
    return -1;
}

static int imu_scale_code(const char *name, bool gyro)
{
    static const char *const accel[] = {"2g", "16g", "4g", "8g"};
    static const char *const gyro_choices[] = {"125dps", "250dps", "500dps", "1000dps", "2000dps"};
    const char *const *choices = gyro ? gyro_choices : accel;
    size_t count = gyro ? 5U : 4U;
    if (!name) return -1;
    for (size_t i = 0; i < count; ++i)
        if (!strcasecmp(name, choices[i])) return (int)i;
    return -1;
}

static const char *sensor_source_name(uint8_t board,uint8_t id)
{
    static const char *const obc[]={"all","board_temp","ntc","bme","pac_u6",
        "pac_u2","mipi1","mipi2"};
    static const char *const exp[]={"all","board_temp","ntc","bme","imu",
        "flow1","flow2","pac","camera1","camera2","limits"};
    static const char *const pdu[]={"all","board_temp","pac"};
    const char *const *names=board==3 ? obc : board==4 ? exp : pdu;
    size_t count=board==3 ? sizeof(obc)/sizeof(*obc) :
        board==4 ? sizeof(exp)/sizeof(*exp) : sizeof(pdu)/sizeof(*pdu);
    return id<count ? names[id] : "unknown";
}

static int sensor_source_id(uint8_t board,const char *name)
{
    if (!name) return -1;
    unsigned limit=board==3 ? 7U : board==4 ? 10U : 2U;
    for (unsigned i=0;i<=limit;i++)
        if (!strcasecmp(name,sensor_source_name(board,(uint8_t)i))) return (int)i;
    return -1;
}

static int sensor_group_id(const char *name)
{
    static const char *const groups[]={"all","temperature","bme","imu","flow",
        "power","camera","limits"};
    if (!name) return -1;
    for (size_t i=0;i<sizeof(groups)/sizeof(*groups);i++)
        if (!strcasecmp(name,groups[i])) return (int)i;
    return -1;
}

static int power_line_bit(uint8_t addr, const char *name)
{
    static const char *const obc[]={"AI_power","FPGA_power","USB1_power",
        "USB2_power","USB3_power","USB4_power","MIPI1_power","MIPI2_power"};
    static const char *const exp[]={"EXP12V","SLN_HD","MOTOR"};
    static const char *const pdu[]={"VOUT","TRANS"};
    const char *const *names=addr==3 ? obc : addr==4 ? exp : pdu;
    size_t count=addr==3 ? 8U : addr==4 ? 3U : 2U;
    if (!name) return -1;
    for (size_t i=0;i<count;i++) if (!strcasecmp(name,names[i])) return (int)i;
    return -1;
}

static int parse_command(char *line, uint8_t *type, uint8_t *addr,
                         uint8_t *payload, uint16_t *length)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    uint32_t a, b, c;
    if (!cmd) return -1;
    *length = 0;
    if (!strcmp(cmd, "ping")) { *type = 0; *addr = 0; }
    else if (!strcmp(cmd, "sys_info")) {
        *type = 1; *addr = 0;
        char *action = token(&save);
        if (action) {
            if (strcmp(action, "self_test") || token(&save)) return -1;
            payload[0] = 1; *length = 1;
        }
    }
    else if (!strcmp(cmd, "sys_rtc") || !strcmp(cmd, "sys_rtc_cfg")) {
        char *arg = token(&save);
        *type = 1; *addr = 1;
        if (arg) {
            if (!strcmp(arg, "now")) a = (uint32_t)time(NULL);
            else if (number(arg, UINT32_MAX, &a)) return -1;
            *length = 4;
            bee_dal_put_u32(payload, &(size_t){0}, a);
        }
    }
    else if (!strcmp(cmd, "pdu_vout") || !strcmp(cmd, "pwr_adj_cfg")) {
        char *action = token(&save);
        *type = 1; *addr = 2;
        if (!action) return -1;
        if (!strcmp(action, "get")) { /* zero-byte read */ }
        else if (!strcmp(action, "set")) {
            if (number(token(&save), 20000, &a) || a < 800) return -1;
            *length = 8;
            memset(payload, 0, 8);
            bee_dal_put_u16(payload, &(size_t){0}, (uint16_t)a);
        } else return -1;
    }
    else if (!strcmp(cmd, "pdu_fault_clear")) {
        *type = 1; *addr = 0x1c;
    }
    else if (!strcmp(cmd, "pdu_heartbeat")) {
        char *action=token(&save);
        *type=1; *addr=0x1d;
        if (!action) return -1;
        if (!strcmp(action,"get")) { /* zero-byte read */ }
        else {
            uint8_t op;
            if (!strcmp(action,"enable")) {
                char *state=token(&save);
                if (!state) return -1;
                if (!strcmp(state,"on")) a=1;
                else if (!strcmp(state,"off")) a=0;
                else return -1;
                op=1;
            } else if (!strcmp(action,"on") || !strcmp(action,"off")) {
                op=!strcmp(action,"on") ? 2 : 3;
                if (number(token(&save),60000,&a) || a<1) return -1;
            } else return -1;
            size_t off=0;
            bee_dal_put_u8(payload,&off,op);
            bee_dal_put_u32(payload,&off,a);
            *length=(uint16_t)off;
        }
    }
    else if (!strcmp(cmd, "pdu_bootloader")) {
        char *action=token(&save);
        if (!action || strcmp(action,"enter")) return -1;
        *type=1; *addr=0x1e;
    }
    else if (!strcmp(cmd,"pwr_en")) {
        char *board_name=token(&save);
        if (board_name && !strcmp(board_name, "get")) {
            if (token(&save)) return -1;
            *type = 1; *addr = 3; return 0;
        }
        if (board_name && (!strcmp(board_name, "on") || !strcmp(board_name, "off"))) {
            bool on = !strcmp(board_name, "on");
            if (number(token(&save), 0x1fff, &a) || !a) return -1;
            char *option = token(&save);
            if (option && (strcmp(option, "force") || on || (a & ~0x700u))) return -1;
            if (token(&save)) return -1;
            uint16_t word = (uint16_t)a | (on ? 0x2000u : 0) | (option ? 0x4000u : 0);
            *type = 1; *addr = 3; *length = 2;
            bee_dal_put_u16(payload, &(size_t){0}, word); return 0;
        }
        uint8_t board=board_name && !strcasecmp(board_name,"OBC") ? 3 :
                      board_name && !strcasecmp(board_name,"EXP") ? 4 :
                      board_name && !strcasecmp(board_name,"PDU") ? 5 : 0;
        char *action=token(&save);
        *type=1; *addr=0x32;
        if (!board || !action) return -1;
        payload[0]=board; *length=1;
        if (!strcasecmp(action,"get")) { /* board-only read */ }
        else if (!strcmp(action,"set")) {
            if (number(token(&save),UINT16_MAX,&a) || !a ||
                number(token(&save),UINT16_MAX,&b)) return -1;
            char *option=token(&save);
            uint8_t flags=0;
            if (option) {
                if (board!=4 || strcmp(option,"force")) return -1;
                flags=1;
            }
            size_t off=1;
            bee_dal_put_u16(payload,&off,(uint16_t)a);
            bee_dal_put_u16(payload,&off,(uint16_t)b);
            bee_dal_put_u8(payload,&off,flags);
            *length=(uint16_t)off;
        } else {
            int bit=power_line_bit(board,action);
            char *state=token(&save);
            if (bit<0 || !state) return -1;
            bool enabled;
            if (!strcasecmp(state,"on")) enabled=true;
            else if (!strcasecmp(state,"off")) enabled=false;
            else return -1;
            char *option=token(&save);
            uint8_t flags=0;
            if (option) {
                if (board!=4 || enabled || strcasecmp(option,"force")) return -1;
                flags=1;
            }
            size_t off=1;
            bee_dal_put_u16(payload,&off,(uint16_t)(1U<<bit));
            bee_dal_put_u16(payload,&off,enabled ? (uint16_t)(1U<<bit) : 0U);
            bee_dal_put_u8(payload,&off,flags);
            *length=(uint16_t)off;
        }
    }
    else if (!strcmp(cmd,"pwr_stat")) {
        char *report=token(&save);
        static const char *const names[]={"all","monitor","enable","error"};
        if (!report || token(&save)) return -1;
        unsigned i;for (i=0;i<4;i++) if (!strcasecmp(report,names[i])) break;
        if (i==4) return -1;
        *type=1;*addr=4;*length=1;payload[0]=(uint8_t)i;
    }
    else if (!strcmp(cmd,"sen_cfg")) {
        char *action=token(&save);
        if (!action) return -1;
        *type=1;*addr=5;*length=0;
        if (!strcasecmp(action,"get")) {if (token(&save)) return -1;}
        else if (!strcasecmp(action,"set")) {
            char *sensor=token(&save);if (!sensor) return -1;
            memset(payload,0,8);*length=8;
            if (!strcasecmp(sensor,"bme")) {
                int t=bme_os_code(token(&save)),p=bme_os_code(token(&save));
                int h=bme_os_code(token(&save)),f=bme_filter_code(token(&save));
                if (t<=0 || p<=0 || h<=0 || f<0) return -1;
                payload[0]=1;payload[2]=t;payload[3]=p;payload[4]=h;payload[5]=f;
            } else if (!strcasecmp(sensor,"imu")) {
                char *op=token(&save);if (!op) return -1;payload[0]=2;
                if (!strcasecmp(op,"accel") || !strcasecmp(op,"gyro")) {
                    bool gyro=!strcasecmp(op,"gyro");
                    int odr=imu_odr_code(token(&save)),scale=imu_scale_code(token(&save),gyro);
                    if (odr<0 || scale<0) return -1;
                    payload[1]=gyro;payload[2]=odr;payload[3]=scale;
                } else if (!strcasecmp(op,"freefall") || !strcasecmp(op,"wakeup")) {
                    bool ff=!strcasecmp(op,"freefall");
                    if (number(token(&save),ff?7:63,&a) || number(token(&save),ff?63:3,&b)) return -1;
                    payload[1]=ff?2:3;payload[2]=a;payload[3]=b;
                } else return -1;
            } else return -1;
            if (token(&save)) return -1;
        } else return -1;
    }
    else if (!strcmp(cmd,"sen_stat")) {
        char *board_name=token(&save);
        static const char *const reports[] = {"all", "temperature", "bme", "imu", "flow"};
        for (unsigned report = 0; board_name && report < 5; ++report) {
            if (strcasecmp(board_name, reports[report])) continue;
            if (token(&save)) return -1;
            *type=1;*addr=6;*length=1;payload[0]=(uint8_t)report;
            return 0;
        }
        uint8_t board=board_name && !strcasecmp(board_name,"OBC") ? 3 :
                      board_name && !strcasecmp(board_name,"EXP") ? 4 :
                      board_name && !strcasecmp(board_name,"PDU") ? 5 : 0;
        char *action=token(&save);
        if (!board || !action || strcasecmp(action,"get")) return -1;
        char *group_name=token(&save);
        int group=sensor_group_id(group_name);
        if (group<0) return -1;
        char *next=token(&save);int source=0;uint32_t cursor=0;
        if (next && strcasecmp(next,"cursor")) {
            source=sensor_source_id(board,next);
            if (source<0) return -1;
            next=token(&save);
        }
        if (next) {
            if (strcasecmp(next,"cursor") || number(token(&save),254,&cursor)) return -1;
        }
        if (token(&save)) return -1;
        *type=1;*addr=7;*length=4;
        payload[0]=board;payload[1]=(uint8_t)group;
        payload[2]=(uint8_t)source;payload[3]=(uint8_t)cursor;
    }
    else if (!strcmp(cmd,"thm_ctl")) {
        char *kind=token(&save),*channel=token(&save),*action=token(&save);
        if (!kind || !channel || !action) return -1;
        unsigned board=!strcasecmp(kind,"heater") ? 0u : !strcasecmp(kind,"ir") ? 1u : 2u;
        if (board>1u || number(channel,board?1:3,&a)) return -1;
        *type=1;*addr=0x14;*length=8;
        memset(payload,0,8);payload[0]=(uint8_t)board;payload[1]=(uint8_t)a;
        if (!strcasecmp(action,"off")) {
            if (token(&save)) return -1;
        } else if (board==1u && !strcasecmp(action,"feedback_start")) {
            if (token(&save)) return -1;
            payload[2]=2;
        } else if (!strcasecmp(action,"set")) {
            if (number(token(&save),board?25:100,&b) || !b) return -1;
            payload[2]=1;payload[3]=(uint8_t)b;
            if (board==0u) {
                if (number(token(&save),600000,&c) || !c) return -1;
                for (unsigned i=0;i<4;i++) payload[4+i]=(uint8_t)(c>>(8*i));
            }
            if (token(&save)) return -1;
        } else return -1;
    }
    else if (!strcmp(cmd,"thm_stat")) {
        char *action=token(&save);
        *type=1;*addr=0x15;
        if (action && strcasecmp(action,"get")) return -1;
        if (token(&save)) return -1;
    }
    else if (!strcmp(cmd,"rba_ctl") || !strcmp(cmd,"rba_job")) {
        char *action=token(&save), *motor=NULL, *argument=NULL, *speed=NULL;
        int32_t signed_value=0;
        uint32_t value=0;
        *type=1;*addr=!strcmp(cmd,"rba_job") ? 0x33 : 0x1a;*length=8;
        memset(payload,0,8);
        if (!action) return -1;
        if (!strcasecmp(action,"init") || !strcasecmp(action,"disable")) {
            payload[0]=!strcasecmp(action,"init")?1:7;payload[1]=2;
        } else if (!strcasecmp(action,"result")) {
            if (number(token(&save),UINT32_MAX,&value) || !value) return -1;
            payload[0]=8;payload[1]=2;
        } else if (!strcasecmp(action,"stop") || !strcasecmp(action,"stop_hold")) {
            motor=token(&save);
            if (!motor) return -1;
            if (!strcasecmp(action,"stop") && !strcasecmp(motor,"all")) value=2;
            else if (number(motor,1,&value)) return -1;
            payload[0]=!strcasecmp(action,"stop")?6:9;
            payload[1]=(uint8_t)value;value=0;
        } else {
            motor=token(&save);
            if (number(motor,1,&value)) return -1;
            payload[1]=(uint8_t)value;value=0;
            if (!strcasecmp(action,"home")) payload[0]=5;
            else if (!strcasecmp(action,"move") || !strcasecmp(action,"goto")) {
                payload[0]=!strcasecmp(action,"move")?2:3;
                argument=token(&save);
                if (signed_number(argument,&signed_value) ||
                    (payload[0]==2 && (!signed_value || signed_value==INT32_MIN))) return -1;
                value=(uint32_t)signed_value;
            } else if (!strcasecmp(action,"jog")) {
                payload[0]=4;argument=token(&save);
                if (!argument || (strcasecmp(argument,"cw") && strcasecmp(argument,"ccw"))) return -1;
                value=!strcasecmp(argument,"cw")?1u:UINT32_MAX;
            } else return -1;
            speed=token(&save);
            if (payload[0]==5 && speed) return -1;
            if (speed) {
                uint32_t parsed;
                if (number(speed,200,&parsed) || !parsed) return -1;
                payload[2]=(uint8_t)parsed;
            }
        }
        if (token(&save)) return -1;
        for (unsigned i=0;i<4;i++) payload[4+i]=(uint8_t)(value>>(8*i));
    }
    else if (!strcmp(cmd,"rba_stat")) {
        char *action=token(&save);
        if ((action && strcasecmp(action,"get")) || token(&save)) return -1;
        *type=1;*addr=0x1b;
    }
    else if (!strcmp(cmd,"fld_stat")) {
        char *action=token(&save);
        *type=1;*addr=0x18;
        if (action && strcasecmp(action,"get")) return -1;
        if (token(&save)) return -1;
    }
    else if (!strcmp(cmd,"fld_ctl") || !strcmp(cmd,"fld_job")) {
        char *device=token(&save), *action=token(&save), *arg;
        *type=1;*addr=!strcmp(cmd,"fld_job") ? 0x34 : 0x17;*length=8;
        memset(payload,0,8);
        if (!device || !action) return -1;
        if (!strcasecmp(device,"valve")) {
            if (strcasecmp(action,"mask") || number(token(&save),15,&a) || token(&save)) return -1;
            payload[0]=0;payload[1]=0;payload[3]=(uint8_t)a;
        } else if (!strcasecmp(device,"pump")) {
            arg=token(&save);
            if (!arg) return -1;
            a=0;
            if (!strcasecmp(action,"u11")) payload[0]=1;
            else if (!strcasecmp(action,"u15")) payload[0]=2;
            else return -1;
            if (!strcasecmp(arg,"off")) payload[1]=0;
            else if (!strcasecmp(arg,"on")) payload[1]=1;
            else if (!strcasecmp(arg,"frequency")) {
                if (number(token(&save),payload[0]==1?1992:800,&a) ||
                    a<(payload[0]==1?8:50)) return -1;
                payload[1]=2;
            } else if (!strcasecmp(arg,"voltage")) {
                if (payload[0]==2) {
                    if (number(token(&save),3,&b)) return -1;
                    payload[2]=(uint8_t)b;
                }
                if (number(token(&save),payload[0]==1?150:250,&a)) return -1;
                payload[1]=3;
            } else return -1;
            if (token(&save)) return -1;
            for (unsigned i=0;i<4;i++) payload[4+i]=(uint8_t)(a>>(8*i));
        } else return -1;
    }
    else if (!strcmp(cmd,"pc_stat") || !strcmp(cmd,"exp_stat")) {
        char *action=token(&save);
        if ((action && strcasecmp(action,"get")) || token(&save)) return -1;
        *type=2;*addr=0x00;
    }
    else if (!strcmp(cmd,"thp_cfg")) {
        char *kind=token(&save),*action=token(&save);
        uint32_t profile;
        if (kind && !strcmp(kind,"get") && !action) {
            *type=2;*addr=0x03;return 0;
        }
        if (!kind || !action) return -1;
        memset(payload,0,32);*type=2;*addr=0x03;*length=32;
        if (!strcasecmp(kind,"heater")) payload[1]=0;
        else if (!strcasecmp(kind,"ir")) payload[1]=1;
        else return -1;
        if (!strcasecmp(action,"get")) {payload[0]=0;*addr=0x10;}
        else if (!strcasecmp(action,"set")) payload[0]=1;
        else return -1;
        if (number(token(&save),payload[1]?1:3,&profile)) return -1;
        payload[2]=(uint8_t)profile;
        if (!payload[0]) {if (token(&save)) return -1;}
        else if (!payload[1]) {
            if (thp_u8_arg(&save,payload,3,3) || thp_u8_arg(&save,payload,4,3) ||
                thp_i16_arg(&save,payload,5,-10000,20000) || thp_i16_arg(&save,payload,7,1,32767) ||
                thp_i16_arg(&save,payload,9,1,20000) || thp_u16_arg(&save,payload,11,1,60000) ||
                thp_u8_arg(&save,payload,13,1) || thp_u8_arg(&save,payload,14,100) ||
                thp_u8_arg(&save,payload,15,100) || thp_gain_arg(&save,payload,16) ||
                thp_gain_arg(&save,payload,20) || thp_gain_arg(&save,payload,24) || token(&save)) return -1;
        } else {
            if (thp_u8_arg(&save,payload,3,1) || thp_u8_arg(&save,payload,4,31) ||
                thp_u8_arg(&save,payload,5,23) || thp_u8_arg(&save,payload,6,32) ||
                thp_u8_arg(&save,payload,7,24) || thp_i16_arg(&save,payload,8,-32768,32766) ||
                thp_i16_arg(&save,payload,10,-32767,32767) || thp_i16_arg(&save,payload,12,1,32767) ||
                thp_u16_arg(&save,payload,14,250,10000) || thp_u16_arg(&save,payload,16,251,60000) ||
                thp_u8_arg(&save,payload,18,25) || thp_u8_arg(&save,payload,19,100) ||
                thp_gain_arg(&save,payload,20) || thp_gain_arg(&save,payload,24) ||
                thp_gain_arg(&save,payload,28) || token(&save)) return -1;
        }
    }
    else if (!strcmp(cmd,"thp_ctl") || !strcmp(cmd,"thp_en")) {
        char *kind=token(&save),*index=token(&save),*action=token(&save);
        if (kind && !strcmp(kind,"get") && !index && !action) {
            *type=2;*addr=4;return 0;
        }
        uint32_t channel;
        if (!kind || !index || !action || token(&save)) return -1;
        *type=2;*addr=0x04;*length=1;
        uint8_t kind_code;
        if (!strcasecmp(kind,"heater")) kind_code=0;
        else if (!strcasecmp(kind,"ir")) kind_code=1;
        else return -1;
        if (number(index,kind_code?1:3,&channel)) return -1;
        uint8_t action_code;
        if (!strcasecmp(action,"status")) action_code=0;
        else if (!strcasecmp(action,"start")) action_code=1;
        else if (!strcasecmp(action,"stop")) action_code=2;
        else if (!strcasecmp(action,"resume") && !kind_code) action_code=3;
        else return -1;
        payload[0]=(uint8_t)(channel|(kind_code<<2)|(action_code<<3));
    }
    else if (!strcmp(cmd,"pc_ctl") || !strcmp(cmd,"exp_ctl")) {
        char *action=token(&save), *selection=NULL;
        if (!action) return -1;
        *type=2;*addr=0x02;*length=8;
        memset(payload,0,8);
        if (!strcasecmp(action,"activate")) payload[0]=1;
        else if (!strcasecmp(action,"deactivate")) payload[0]=2;
        else if (!strcasecmp(action,"test")) payload[0]=3;
        else if (!strcasecmp(action,"replace")) payload[0]=4;
        else if (!strcasecmp(action,"circulation")) payload[0]=5;
        else if (!strcasecmp(action,"ground")) payload[0]=6;
        else if (!strcasecmp(action,"standby")) payload[0]=7;
        else if (!strcasecmp(action,"stop")) payload[0]=8;
        else if (!strcasecmp(action,"reset_error")) payload[0]=9;
        else return -1;
        if (payload[0]>=3 && payload[0]<=6) {
            selection=token(&save);
            if (payload[0]==6) {
                static const char *const routes[]={"protein_a","protein_b","precip_respol","precip_rsvb"};
                unsigned i;
                for (i=0;i<4;i++) if (selection && !strcasecmp(selection,routes[i])) break;
                if (i==4) return -1;
                payload[1]=(uint8_t)i;
            } else if (!selection || !strcasecmp(selection,"both")) payload[1]=0;
            else if (!strcasecmp(selection,"a")) payload[1]=1;
            else if (!strcasecmp(selection,"b")) payload[1]=2;
            else return -1;
        }
        if (token(&save)) return -1;
    }
    else if (!strcmp(cmd,"fld_cfg") || !strcmp(cmd,"pc_cfg") || !strcmp(cmd,"exp_cfg")) {
        char *group=token(&save),*name;
        bool pc=strcmp(cmd,"fld_cfg")!=0;
        *type=pc?2:1;*addr=pc?0x01:0x16;
        if (group && !strcasecmp(group,"get")) {
            char *page=token(&save);
            if (page) {
                if (number(page,219,&a) || token(&save)) return -1;
                memset(payload,0,32);payload[1]=(uint8_t)a;*length=32;
                *addr=pc?0x12:0x36;
            }
            return 0;
        }
        if (!group) return -1;
        memset(payload,0,32);payload[0]=1;*length=32;
        if (!strcasecmp(group,"valve")) {
            name=token(&save);
            if (!name) return -1;
            if (!strcasecmp(name,"map")) {
                payload[1]=1;
                for (unsigned i=0;i<4;i++) {
                    if (number(token(&save),3,&a)) return -1;
                    payload[2+i]=(uint8_t)a;
                }
            } else if (!strcasecmp(name,"pulse")) {
                if (number(token(&save),100,&a) || !a) return -1;
                payload[1]=2;
                for (unsigned i=0;i<4;i++) payload[4+i]=(uint8_t)(a>>(8*i));
            } else return -1;
        } else if (!strcasecmp(group,"flow")) {
            name=token(&save);
            if (!name) return -1;
            if (!strcasecmp(name,"channel")) {
                if (number(token(&save),1,&a)) return -1;
                payload[1]=3;
            } else if (!strcasecmp(name,"interval")) {
                if (number(token(&save),UINT32_MAX,&a) || !a) return -1;
                payload[1]=4;
            } else return -1;
            for (unsigned i=0;i<4;i++) payload[4+i]=(uint8_t)(a>>(8*i));
        } else if (!strcasecmp(group,"route")) {
            static const char *const routes[]={"standby","test","replace","circulation","ground"};
            name=token(&save);if (!name) return -1;
            unsigned route=5;
            for (unsigned i=0;i<5;i++) if (!strcasecmp(name,routes[i])) route=i;
            if (route==5) return -1;
            payload[1]=5;payload[2]=(uint8_t)route;
            if (route>=1) {
                name=token(&save);if (!name) return -1;
                if (route==4) {
                    static const char *const presets[]={"protein_a","protein_b","precip_respol","precip_rsvb"};
                    unsigned preset=4;
                    for (unsigned i=0;i<4;i++) if (!strcasecmp(name,presets[i])) preset=i;
                    if (preset==4) return -1;
                    payload[3]=(uint8_t)preset;
                } else if (!strcasecmp(name,"a")) payload[3]=0;
                else if (!strcasecmp(name,"b")) payload[3]=1;
                else return -1;
            }
            for (unsigned i=0;i<4;i++) {
                if (number(token(&save),1,&a)) return -1;
                payload[4]|=(uint8_t)(a<<i);
            }
        } else if (!strcasecmp(group,"profile")) {
            static const char *const targets[]={"global","test","replace","circulation"};
            static const char *const fields[]={"","duration","minflow","flow_action",
                "pump_freq","pump_volt","volume","pump_on","pump_off","cycles",
                "pump_off_settle","run_mode","max_pump_on","flow_check","flow_delay",
                "fail_samples","pump_enable","name"};
            name=token(&save);if (!name) return -1;
            unsigned target=4;
            for (unsigned i=0;i<4;i++) if (!strcasecmp(name,targets[i])) target=i;
            if (target==4) return -1;
            payload[1]=6;payload[2]=(uint8_t)target;
            name=token(&save);if (!name) return -1;
            unsigned field=18;
            for (unsigned i=1;i<18;i++) if (!strcasecmp(name,fields[i])) field=i;
            if (field==18 || (!pc && field>=13)) return -1;
            payload[3]=(uint8_t)field;
            char *value=token(&save);if (!value) return -1;
            if (field==17) {
                size_t len=strlen(value);
                if (target!=0 || !len || len>23) return -1;
                memcpy(payload+4,value,len);
            } else if (field==2 || field==6) {
                char *end;errno=0;double v=strtod(value,&end);
                if (errno || *end || v<0 || v>4294967.0) return -1;
                a=(uint32_t)(v*1000.0+0.5);
            } else if (field==3) {
                if (!strcasecmp(value,"none")) a=0;
                else if (!strcasecmp(value,"warn")) a=1;
                else if (!strcasecmp(value,"stop")) a=2;
                else return -1;
            } else if (field==11) {
                if (!strcasecmp(value,"time")) a=0;
                else if (!strcasecmp(value,"volume")) a=1;
                else return -1;
            } else if (number(value,UINT32_MAX,&a)) return -1;
            if (field!=17) for (unsigned i=0;i<4;i++) payload[4+i]=(uint8_t)(a>>(8*i));
        } else if (pc && !strcasecmp(group,"datalog")) {
            name=token(&save);
            if (!name) return -1;
            payload[1]=10;
            if (!strcasecmp(name,"on")) payload[4]=1;
            else if (strcasecmp(name,"off")) return -1;
        } else if (pc && !strcasecmp(group,"defaults")) {
            payload[1]=11;
        } else if (!pc && !strcasecmp(group,"pump")) {
            name=token(&save);
            if (!name) return -1;
            payload[1]=9;
            if (!strcasecmp(name,"detect")) {
                name=token(&save);
                if (!name) return -1;
                if (!strcasecmp(name,"u11")) payload[3]=0;
                else if (!strcasecmp(name,"u15")) payload[3]=1;
                else return -1;
            } else if (!strcasecmp(name,"result")) {
                if (number(token(&save),UINT32_MAX,&a) || !a) return -1;
                payload[2]=1;payload[3]=0xff;
                for (unsigned i=0;i<4;i++) payload[4+i]=(uint8_t)(a>>(8*i));
            } else return -1;
        } else if (!strcasecmp(group,"resume") || (!pc && !strcasecmp(group,"pump_type"))) {
            name=token(&save);if (!name) return -1;
            payload[1]=!strcasecmp(group,"resume")?7:8;
            if (payload[1]==7) {if (number(name,1,&a)) return -1;}
            else if (!strcasecmp(name,"lowdriver")) a=0;
            else if (!strcasecmp(name,"highdriver")) a=1;
            else return -1;
            payload[4]=(uint8_t)a;
        } else return -1;
        if (token(&save)) return -1;
    }
    else if (!strcmp(cmd,"thm_cfg")) {
        char *kind=token(&save);
        *type=1;*addr=0x13;
        if (kind && !strcasecmp(kind,"get") && !token(&save)) return 0;
        if (!kind) return -1;
        char *index=token(&save),*action=token(&save);
        if (!action || strcasecmp(action,"set")) return -1;
        memset(payload,0,32);payload[0]=1;*length=32;
        if (!strcasecmp(kind,"heater")) {
            if (number(index,3,&a)) return -1;
            unsigned o=2+4*a;payload[1]=(uint8_t)(1u<<a);
            char *name=token(&save);
            if (!name || strcasecmp(name,"max_duty") || number(token(&save),100,&b)) return -1;
            payload[o]=(uint8_t)b;
            name=token(&save);
            if (!name || strcasecmp(name,"slew") || number(token(&save),100,&b)) return -1;
            payload[o+1]=(uint8_t)b;
            name=token(&save);
            if (!name || strcasecmp(name,"max_on_ms") || number(token(&save),600000,&b) ||
                !b || b%10 || token(&save)) return -1;
            b/=10;payload[o+2]=(uint8_t)b;payload[o+3]=(uint8_t)(b>>8);
        } else if (!strcasecmp(kind,"ir")) {
            if (number(index,1,&a)) return -1;
            unsigned o=18+7*a;payload[1]=(uint8_t)(1u<<(a+4));
            char *name=token(&save);
            if (!name || strcasecmp(name,"max_duty") || number(token(&save),25,&b)) return -1;
            payload[o]=(uint8_t)b;
            name=token(&save);
            if (!name || (strcasecmp(name,"max_on_ms") && strcasecmp(name,"max_pulse_ms")) ||
                number(token(&save),100,&b) || !b) return -1;
            payload[o+1]=(uint8_t)b;
            name=token(&save);
            if (!name || strcasecmp(name,"pwm_hz") || number(token(&save),100000,&b) || b<100) return -1;
            for (unsigned i=0;i<4;i++) payload[o+2+i]=(uint8_t)(b>>(8*i));
            name=token(&save);
            if (!name || strcasecmp(name,"min_off_ms") || number(token(&save),25500,&b) ||
                b%100 || token(&save)) return -1;
            payload[o+6]=(uint8_t)(b/100);
        } else return -1;
    }
    else if (!strcmp(cmd,"uvc_cfg")) {
        char *camera = token(&save);
        if (camera && !strcmp(camera,"get")) {
            if (token(&save)) return -1;
            *type=1;*addr=0x10;return 0;
        }
        if (number(camera,4,&a) || a<1) return -1;
        char *action=token(&save);
        if (!action) return -1;
        *type=1;*addr=0x10;payload[0]=(uint8_t)a;
        if (!strcasecmp(action,"get")) {
            if (token(&save)) return -1;
            *length=1;*addr=0x30;
        } else if (!strcasecmp(action,"set")) {
            memset(payload+1,0,31);payload[1]=1;*length=32;
            char *name;uint16_t mask=0;
            while ((name=token(&save))) {
                char *value=token(&save);if (!value) return -1;
                int bit=-1,offset=0,size=0;uint32_t v=0;
                if (!strcasecmp(name,"format")) {
                    if (strcasecmp(value,"MJPG")) return -1;
                    memcpy(payload+4,"MJPG",4);bit=0;
                } else if (!strcasecmp(name,"resolution")) {
                    unsigned w,h;char tail;
                    if (sscanf(value,"%ux%u%c",&w,&h,&tail)!=2 || !w || !h || w>65535 || h>65535) return -1;
                    payload[8]=w;payload[9]=w>>8;payload[10]=h;payload[11]=h>>8;
                    mask|=(1U<<1)|(1U<<2);continue;
                } else if (!strcasecmp(name,"fps")) {
                    if (number(value,65535,&v) || !v) return -1;
                    payload[12]=v;payload[13]=v>>8;payload[14]=1;payload[15]=0;bit=3;
                } else if (!strcasecmp(name,"exposure")) {
                    if (!strcasecmp(value,"auto")) v=0;
                    else if (!strcasecmp(value,"manual")) v=1;
                    else return -1;
                    bit=4;offset=16;size=1;
                } else if (!strcasecmp(name,"exposure_time")) {bit=5;offset=17;size=4;
                } else if (!strcasecmp(name,"gain")) {bit=6;offset=21;size=2;
                } else if (!strcasecmp(name,"autofocus")) {
                    if (!strcasecmp(value,"on")) v=1;
                    else if (!strcasecmp(value,"off")) v=0;
                    else return -1;
                    bit=7;offset=23;size=1;
                } else if (!strcasecmp(name,"focus")) {bit=8;offset=24;size=2;
                } else if (!strcasecmp(name,"white_balance")) {
                    if (!strcasecmp(value,"auto")) v=1;
                    else if (!strcasecmp(value,"manual")) v=0;
                    else return -1;
                    bit=9;offset=26;size=1;
                } else if (!strcasecmp(name,"white_balance_temp")) {bit=10;offset=27;size=2;
                } else if (!strcasecmp(name,"brightness")) {
                    char *end;errno=0;long n=strtol(value,&end,0);
                    if (errno || !end || *end || n<INT16_MIN || n>INT16_MAX) return -1;
                    v=(uint16_t)(int16_t)n;bit=11;offset=29;size=2;
                } else if (!strcasecmp(name,"led_pwm")) {
                    if (number(value,100,&v)) return -1;
                    bit=12;offset=31;size=1;
                } else return -1;
                if (bit<0 || mask & (1U<<bit)) return -1;
                if (size && bit!=4 && bit!=7 && bit!=9) {
                    if (number(value,size==4 ? UINT32_MAX : 65535,&v)) return -1;
                }
                for (int j=0;j<size;++j) payload[offset+j]=(uint8_t)(v>>(8*j));
                mask|=(uint16_t)(1U<<bit);
            }
            if (!mask) return -1;
            payload[2]=mask;payload[3]=mask>>8;
        } else return -1;
    }
    else if (!strcmp(cmd,"uvc_ctl") || !strcmp(cmd,"uvc_job") || !strcmp(cmd,"uvc_stat")) {
        bool capture=strcmp(cmd,"uvc_stat")!=0;
        char *camera = token(&save);
        if (!capture && !camera) {*type=1;*addr=0x12;return 0;}
        if (number(camera,4,&a) || a<1) return -1;
        char *action=token(&save);
        if (!action || strcasecmp(action,capture?"capture":"get") ||
            number(token(&save),UINT32_MAX,&b) || !b || token(&save)) return -1;
        *type=1;*addr=capture?(!strcmp(cmd,"uvc_job")?0x35:0x11):0x31;*length=capture?8:5;
        payload[0]=(uint8_t)a;
        if (capture) {payload[1]=1;payload[2]=0;payload[3]=0;}
        size_t off=capture?4:1;
        bee_dal_put_u32(payload,&off,b);
    }
    else if (!strcmp(cmd, "tm_get")) { *type = 4; *addr = 0; }
    else if (!strcmp(cmd, "tm_obc_get")) { *type = 4; *addr = 2; }
    else if (!strcmp(cmd, "exp_ping")) { *type = 2; *addr = BEE_DAL_EXP_PING; }
    else if (!strcmp(cmd, "sen_bme_cfg")) {
        char *board = token(&save);
        char *action = token(&save);
        char exp_command[96];
        if (!board || strcasecmp(board, "EXP") || !action) return -1;
        if (!strcasecmp(action, "get")) {
            if (token(&save)) return -1;
            snprintf(exp_command, sizeof(exp_command), "bme688_cfg_get");
        } else if (!strcasecmp(action, "set")) {
            int temp = bme_os_code(token(&save));
            int press = bme_os_code(token(&save));
            int hum = bme_os_code(token(&save));
            int filter = bme_filter_code(token(&save));
            if (temp < 0 || press < 0 || hum < 0 || filter < 0 || token(&save)) return -1;
            snprintf(exp_command, sizeof(exp_command),
                     "bme688_cfg_set %d %d %d %d 0 0 0", temp, press, hum, filter);
        } else if (!strcasecmp(action, "result")) {
            uint32_t id;
            if (number(token(&save), UINT32_MAX, &id) || id == 0 || token(&save)) return -1;
            snprintf(exp_command, sizeof(exp_command), "bme688_result %" PRIu32, id);
        } else return -1;
        *type = 2; *addr = BEE_DAL_EXP_RUN;
        *length = (uint16_t)strlen(exp_command);
        memcpy(payload, exp_command, *length);
    }
    else if (!strcmp(cmd, "sen_imu_cfg")) {
        char *board = token(&save);
        char *action = token(&save);
        char exp_command[96];
        if (!board || strcasecmp(board, "EXP") || !action) return -1;
        if (!strcasecmp(action, "get")) {
            if (token(&save)) return -1;
            snprintf(exp_command, sizeof(exp_command), "lsm6d_conf_get");
        } else if (!strcasecmp(action, "reset")) {
            if (token(&save)) return -1;
            snprintf(exp_command, sizeof(exp_command), "lsm6d_init");
        } else if (!strcasecmp(action, "result")) {
            uint32_t id;
            if (number(token(&save), UINT32_MAX, &id) || id == 0 || token(&save)) return -1;
            snprintf(exp_command, sizeof(exp_command), "lsm6d_result %" PRIu32, id);
        } else if (!strcasecmp(action, "set")) {
            char *channel = token(&save);
            if (!channel) return -1;
            if (!strcasecmp(channel, "accel") || !strcasecmp(channel, "gyro")) {
                bool gyro = !strcasecmp(channel, "gyro");
                int odr = imu_odr_code(token(&save));
                int scale = imu_scale_code(token(&save), gyro);
                if (odr < 0 || scale < 0 || token(&save)) return -1;
                snprintf(exp_command, sizeof(exp_command), "lsm6d_conf_%s %d %d",
                         gyro ? "gyro" : "accel", odr, scale);
            } else if (!strcasecmp(channel, "freefall") || !strcasecmp(channel, "wakeup")) {
                bool freefall = !strcasecmp(channel, "freefall");
                uint32_t threshold, duration;
                if (number(token(&save), freefall ? 7U : 63U, &threshold) ||
                    number(token(&save), freefall ? 63U : 3U, &duration) ||
                    token(&save)) return -1;
                snprintf(exp_command, sizeof(exp_command), "lsm6d_conf_%s %" PRIu32 " %" PRIu32,
                         freefall ? "ff" : "wu", threshold, duration);
            } else return -1;
        } else return -1;
        *type = 2; *addr = BEE_DAL_EXP_RUN;
        *length = (uint16_t)strlen(exp_command);
        memcpy(payload, exp_command, *length);
    }
    else if (!strcmp(cmd, "exp_tlm")) {
        char *arg = token(&save);
        if (!arg || strlen(arg) >= 20) return -1;
        *type = 2; *addr = BEE_DAL_EXP_TLM;
        *length = (uint16_t)strlen(arg);
        memcpy(payload, arg, *length);
    }
    else if (!strcmp(cmd, "burn_cfg_set")) {
        uint32_t wire, value;
        char *wire_text = token(&save);
        char *key = token(&save);
        char *value_text = token(&save);
        if (number(wire_text, 1, &wire) || !key ||
            number(value_text, 3000, &value) || token(&save)) return -1;
        if ((!strcmp(key, "duty_pct") && (value < 1 || value > 100)) ||
            (!strcmp(key, "burn_time_ms") && (value < 50 || value > 1000)) ||
            (!strcmp(key, "max_total_current_ma") && value < 1)) return -1;
        if (strcmp(key, "duty_pct") && strcmp(key, "burn_time_ms") &&
            strcmp(key, "max_total_current_ma")) return -1;
        int n = snprintf((char *)payload, 96, "burn_cfg_set %" PRIu32 " %s %" PRIu32,
                         wire, key, value);
        if (n <= 0 || n > 95) return -1;
        *type = 2; *addr = BEE_DAL_EXP_RUN; *length = (uint16_t)n;
        return 0;
    }
    else if (!strcmp(cmd, "exp_run")) {
        if (!save) return -1;
        while (*save == ' ' || *save == '\t') ++save;
        size_t n = strlen(save);
        if (!n || n > 95 || strchr(save, '\n')) return -1;
        *type = 2; *addr = BEE_DAL_EXP_RUN; *length = (uint16_t)n;
        memcpy(payload, save, n);
        return 0;
    }
    else if (!strcmp(cmd, "exp_csp_get") || !strcmp(cmd, "exp_csp_set")) {
        bool set = !strcmp(cmd, "exp_csp_set");
        char *field = NULL, *value = NULL;
        if (number(token(&save), 12, &a) || !(field = token(&save)) ||
            (set && (!save || !*save))) return -1;
        if (set) {
            while (*save == ' ' || *save == '\t') ++save;
            value = save;
            if (!*value) return -1;
        } else if (token(&save)) return -1;
        int n = set ? snprintf((char *)payload, 231, "set %u %s %s", a, field, value)
                    : snprintf((char *)payload, 231, "get %u %s", a, field);
        if (n < 5 || n > 230) return -1;
        *type = 2; *addr = BEE_DAL_EXP_CSP_SUBMIT; *length = (uint16_t)n;
        return 0;
    }
    else if (!strcmp(cmd, "exp_csp_run")) {
        if (!save) return -1;
        while (*save == ' ' || *save == '\t') ++save;
        int n = snprintf((char *)payload, 231, "run %s", save);
        if (n < 5 || n > 230) return -1;
        *type = 2; *addr = BEE_DAL_EXP_CSP_SUBMIT; *length = (uint16_t)n;
        return 0;
    }
    else if (!strcmp(cmd, "exp_csp_refresh")) {
        char *table = token(&save);
        if (strcmp(table ? table : "", "0") || token(&save)) return -1;
        *type = 2; *addr = BEE_DAL_EXP_CSP_SUBMIT;
        *length = (uint16_t)strlen("refresh 0");
        memcpy(payload, "refresh 0", *length);
        return 0;
    }
    else if (!strcmp(cmd, "exp_csp_result")) {
        if (number(token(&save), UINT32_MAX, &a) || a == 0) return -1;
        *type = 2; *addr = BEE_DAL_EXP_CSP_RESULT; *length = 4;
        bee_dal_put_u32(payload, &(size_t){0}, a);
    }
    else if (!strcmp(cmd, "exp_csp_snapshot")) {
        if (number(token(&save), 12, &a) || (a != 0 && a != 2 && a != 3 && a != 6 && a != 12)) return -1;
        *type = 2; *addr = BEE_DAL_EXP_CSP_SNAPSHOT; *length = 1;
        payload[0] = (uint8_t)a;
    }
    else if (!strcmp(cmd, "dat_lst")) {
        *type = 3; *addr = 0;
        if (number(token(&save), 255, &a)) return -1;
        payload[0] = (uint8_t)a; *length = 1;
    }
    else if (!strcmp(cmd, "dat_sel")) {
        if (data_type_arg(token(&save), &a) || number(token(&save), UINT32_MAX, &b) ||
            number(token(&save), BEE_DAL_MAX_PAYLOAD, &c) || c == 0) return -1;
        *type = 3; *addr = 1; *length = 7;
        size_t off = 0;
        bee_dal_put_u8(payload, &off, a);
        bee_dal_put_u32(payload, &off, b);
        bee_dal_put_u16(payload, &off, c);
    }
    else if (!strcmp(cmd, "dat_get")) {
        if (number(token(&save), UINT32_MAX, &a)) return -1;
        *type = 3; *addr = 2; *length = 4;
        bee_dal_put_u32(payload, &(size_t){0}, a);
    }
    else if (!strcmp(cmd, "dat_des")) { *type = 3; *addr = 3; }
    else if (!strcmp(cmd, "dat_del")) {
        if (data_type_arg(token(&save), &a) || number(token(&save), UINT32_MAX, &b)) return -1;
        *type = 3; *addr = 4; *length = 5;
        size_t off = 0;
        bee_dal_put_u8(payload, &off, a); bee_dal_put_u32(payload, &off, b);
    }
    else if ((!strcmp(cmd, "dat_mark") || !strcmp(cmd, "dat_mrk"))) {
        if (number(token(&save), 254, &a)) return -1;
        *type = 3; *addr = 5; *length = 1; payload[0] = a;
    }
    else if (!strcmp(cmd, "shell")) {
        while (*save == ' ' || *save == '\t') ++save;
        size_t n = strlen(save);
        if (!n || n > 2048) return -1;
        *type = 0xff; *addr = 1; *length = (uint16_t)n;
        memcpy(payload, save, n);
        return 0;
    }
    else if (!strcmp(cmd, "debug_handoff")) {
        char *arg = token(&save);
        if (!arg || strcmp(arg, "yes")) return -1;
        const char magic[] = "DEBUG_HANDOFF_OK";
        *type = 0xff; *addr = 0; *length = sizeof(magic) - 1;
        memcpy(payload, magic, *length);
    }
    else if (!strcmp(cmd, "raw")) {
        if (number(token(&save), 255, &a) || number(token(&save), 255, &b)) return -1;
        *type = a; *addr = b;
        char *arg;
        while ((arg = token(&save))) {
            if (*length >= BEE_DAL_MAX_PAYLOAD || number(arg, 255, &c)) return -1;
            payload[(*length)++] = c;
        }
    }
    else return -1;
    if (*length == 32 && payload[0] == 0 && strcmp(cmd, "raw")) {
        if (*type == 1 && *addr == 0x16) *addr = 0x36;
        else if (*type == 2 && *addr == 0x01) *addr = 0x12;
    }
    return token(&save) ? -1 : 0;
}

static void render_reply(const char *command, uint8_t type, uint8_t addr,
                         const bee_dal_local_reply_t *reply, bool json,
                         char *out, size_t cap)
{
    char info[24000];
    size_t used = 0;
    info[0] = 0;
    bool ok = reply->ack_err != BEE_DAL_ACK_ERR_MARK;
    if (!ok) append(info, sizeof(info), &used, "error code=0x%02X", reply->addr_errcode);
    else if (type == 2 && addr == BEE_DAL_EXP_CSP_SUBMIT && reply->length == 4) {
        size_t off = 0;
        append(info, sizeof(info), &used, "CSP ticket=%" PRIu32,
               bee_dal_get_u32(reply->payload, &off));
    }
    else if (type == 2 && addr == BEE_DAL_EXP_CSP_RESULT && reply->length >= 3) {
        uint16_t n = (uint16_t)(reply->payload[1] | (reply->payload[2] << 8));
        if (n > reply->length - 3) n = reply->length - 3;
        ok = reply->payload[0] != EXP_CSP_FAILED;
        append(info, sizeof(info), &used, "CSP state=%u\n", reply->payload[0]);
        for (uint16_t i = 0; i < n; i++)
            append(info, sizeof(info), &used, "%c", reply->payload[3 + i]);
    }
    else if (type == 2 && addr == BEE_DAL_EXP_CSP_SNAPSHOT && reply->length) {
        for (uint16_t i = 0; i < reply->length; ++i)
            append(info, sizeof(info), &used, "%c", reply->payload[i]);
    }
    else if (type == 2 && addr >= BEE_DAL_EXP_PING && addr <= BEE_DAL_EXP_RUN &&
             reply->length >= 3) {
        uint16_t n = (uint16_t)(reply->payload[1] | (reply->payload[2] << 8));
        if (n > reply->length - 3) n = reply->length - 3;
        ok = reply->payload[0] == 0;
        append(info, sizeof(info), &used, "EXP status=%u\n", reply->payload[0]);
        for (uint16_t i = 0; i < n; ++i)
            append(info, sizeof(info), &used, "%c", reply->payload[3 + i]);
    }
    else if (type == 0xff && addr == 1 && reply->length >= 7) {
        size_t off = 3;
        uint32_t n = bee_dal_get_u32(reply->payload, &off);
        if (n > (uint32_t)(reply->length - 7)) n = reply->length - 7;
        append(info, sizeof(info), &used, "shell status=%u exit=%u truncated=%u\n",
               reply->payload[0], reply->payload[1], reply->payload[2]);
        for (uint32_t i = 0; i < n; ++i)
            append(info, sizeof(info), &used, "%c", reply->payload[7 + i]);
    }
    else if ((type == 0 || (type == 3 && (addr == 3 || addr == 5)) ||
              (type == 1 && (addr == 0x11 || addr == 0x17 || addr == 0x1a)) ||
              (type == 2 && addr == 2)) && reply->length == 4) {
        size_t off = 0;
        append(info, sizeof(info), &used, "uptime=%" PRIu32 "s", bee_dal_get_u32(reply->payload, &off));
    }
    else if (type == 1 && addr == 0 && reply->length == SYS_INFO_PAYLOAD_SIZE &&
             reply->payload[0] == SYS_INFO_SCHEMA && reply->payload[1] == 3) {
        static const char *names[] = {"PDU", "EXP", "OBC"};
        for (size_t i = 0; i < 3; ++i) {
            const uint8_t *board = reply->payload + SYS_INFO_HEADER_SIZE + i * SYS_INFO_RECORD_SIZE;
            char build[SYS_INFO_BUILD_SIZE + 1];
            memcpy(build, board + 8, SYS_INFO_BUILD_SIZE);
            build[SYS_INFO_BUILD_SIZE] = 0;
            if (i) append(info, sizeof(info), &used, "\n");
            append(info, sizeof(info), &used, "%s %s hardware=%u.%u.%u",
                   names[i], board[1] ? "online" : "offline", board[2], board[3], board[4]);
            if (board[1]) {
                append(info, sizeof(info), &used, " firmware=%u.%u.%u",
                       board[5], board[6], board[7]);
                if (build[0]) append(info, sizeof(info), &used, " build=%s", build);
            }
        }
    }
    else if (type == 1 && addr == 1 && reply->length >= 4) {
        size_t off = 0;
        append(info, sizeof(info), &used, "epoch=%" PRIu32, bee_dal_get_u32(reply->payload, &off));
    }
    else if (type == 1 && addr == 2 && reply->length == 8) {
        size_t off = 0;
        uint16_t setpoint = bee_dal_get_u16(reply->payload, &off);
        uint16_t voltage = bee_dal_get_u16(reply->payload, &off);
        uint16_t current = bee_dal_get_u16(reply->payload, &off);
        uint16_t flags = bee_dal_get_u16(reply->payload, &off);
        append(info, sizeof(info), &used, "TRANS=%s VOUT=%s setpoint=",
               flags & GUI_PDU_VOUT_TRANS_ON ? "on" : "off",
               !(flags&GUI_PDU_VOUT_STATE_VALID) ? "unknown" : flags & GUI_PDU_VOUT_ENABLED ? "on" : "off");
        if (flags & GUI_PDU_VOUT_SET_VALID) append(info, sizeof(info), &used, "%u mV", setpoint);
        else append(info, sizeof(info), &used, "unavailable");
        append(info, sizeof(info), &used, " measured=");
        if (flags & GUI_PDU_VOUT_VOLT_VALID) append(info, sizeof(info), &used, "%u mV", voltage);
        else append(info, sizeof(info), &used, "unavailable");
        append(info, sizeof(info), &used, " current=");
        if (flags & GUI_PDU_VOUT_CURR_VALID) append(info, sizeof(info), &used, "%u mA", current);
        else append(info, sizeof(info), &used, "unavailable");
    }
    else if (type == 1 && addr == 3 && reply->length == 2) {
        size_t off = 0;
        uint16_t value = bee_dal_get_u16(reply->payload, &off);
        append(info,sizeof(info),&used,"global_on_mask=0x%04x OBC_valid=%u EXP_valid=%u PDU_valid=%u",
               value & 0x1fffu, !!(value & 0x2000u), !!(value & 0x4000u), !!(value & 0x8000u));
    }
    else if (type == 1 && addr == 0x12 && reply->length == 6) {
        size_t off = 0;
        uint16_t states = bee_dal_get_u16(reply->payload, &off);
        uint16_t errors = bee_dal_get_u16(reply->payload, &off);
        uint16_t known = bee_dal_get_u16(reply->payload, &off);
        static const char *const names[] = {"unknown", "queued", "capturing", "saved", "failed"};
        for (unsigned i = 0; i < 4; ++i) {
            unsigned state = (states >> (4 * i)) & 15;
            append(info,sizeof(info),&used,"%scamera%u known=%u state=%s error=%u",
                   i ? "\n" : "", i + 1, !!(known & (1u << i)),
                   state < 5 ? names[state] : "invalid", (errors >> (4 * i)) & 15);
        }
    }
    else if (type == 1 && addr == 0x32 && reply->length == 7) {
        size_t off=1;
        uint16_t on=bee_dal_get_u16(reply->payload,&off);
        uint16_t known=bee_dal_get_u16(reply->payload,&off);
        uint16_t failed=bee_dal_get_u16(reply->payload,&off);
        append(info,sizeof(info),&used,"board=%u on=0x%04X known=0x%04X failed=0x%04X",
               reply->payload[0],on,known,failed);
        if (failed) ok=false;
    }
    else if (type == 1 && addr == 0x1c && reply->length == 1) {
        append(info, sizeof(info), &used, "PDU fault latch %s",
               reply->payload[0] == 0 ? "cleared" : "still active");
        if (reply->payload[0] != 0) ok = false;
    }
    else if (type==1 && addr==0x1d && reply->length==9) {
        size_t off=1;
        uint32_t on_ms=bee_dal_get_u32(reply->payload,&off);
        uint32_t off_ms=bee_dal_get_u32(reply->payload,&off);
        append(info,sizeof(info),&used,"enabled=%s on=%" PRIu32 " ms off=%" PRIu32 " ms",
               reply->payload[0] ? "yes" : "no",on_ms,off_ms);
    }
    else if (type==1 && addr==0x1e && reply->length==1) {
        append(info,sizeof(info),&used,"PDU bootloader request %s",
               reply->payload[0]==1 ? "accepted" : "rejected");
        if (reply->payload[0]!=1) ok=false;
    }
    else if (type==1 && addr==4 && reply->length>=6 && reply->length<=200 &&
             reply->payload[1]==2 && reply->length==22U+6U*reply->payload[2] && reply->payload[3]==0) {
        const uint8_t *p=reply->payload;
        size_t off=4;uint16_t errors=bee_dal_get_u16(p,&off);
        append(info,sizeof(info),&used,"report=%u records=%u availability_errors=0x%04x hardware_faults=unavailable",p[0],p[2],errors&7);
        for (unsigned i=0;i<p[2];i++) {
            uint16_t d=bee_dal_get_u16(p,&off),mv=bee_dal_get_u16(p,&off);
            int16_t ma=(int16_t)bee_dal_get_u16(p,&off);unsigned flags=(d>>7)&31;
            append(info,sizeof(info),&used,"\nboard=%u source=%u flags=0x%02x state=%s voltage=",(d>>4)&7,d&15,flags,
                   !(flags&1) ? "unknown" : (flags&2) ? "on" : "off");
            if (flags&4) append(info,sizeof(info),&used,"%u mV",mv);else append(info,sizeof(info),&used,"unavailable");
            if (flags&8) append(info,sizeof(info),&used," current=%d mA",ma);else append(info,sizeof(info),&used," current=unavailable");
        }
        size_t tail=reply->length-16U, to=tail;
        uint16_t valid=bee_dal_get_u16(p,&to), status=bee_dal_get_u16(p,&to);
        uint8_t mode=p[to++], tps=p[to++];
        int16_t temp=(int16_t)bee_dal_get_u16(p,&to);
        uint32_t age=bee_dal_get_u32(p,&to);
        static const char *const names[]={"UNKNOWN","UNPOWERED","STARTING","READY","COMM_ERROR"};
        append(info,sizeof(info),&used,"\nPDU valid=0x%04x status=0x%04x mode=%u TPS=%s age_ms=%" PRIu32,
            valid,status,mode,tps<5?names[tps]:"INVALID",age);
        if(valid&PDU_VALID_TEMP) append(info,sizeof(info),&used," temperature=%.1f C",temp/10.0);

    }

    else if (type==1 && (addr==0x35 || addr==0x31) && reply->length==8) {
        const uint8_t *p=reply->payload;
        uint32_t id=(uint32_t)p[4]|((uint32_t)p[5]<<8)|((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
        const char *states[]={"unknown","queued","capturing","saved","failed"};
        append(info,sizeof(info),&used,"camera=%u request_id=%" PRIu32 " state=%s error=%u",
               p[0],id,p[1]<5?states[p[1]]:"invalid",p[2]);
        if (p[1]==3) {
            const char *root=getenv("DAL_UVC_DATA_DIR");
            if (!root || !*root) root=BEEIF_DATA_DIR "/images/usb";
            append(info,sizeof(info),&used," path=%s/USB%u_%08" PRIu32 ".jpg",root,p[0]-1,id);
        }
    }
    else if (type==1 && (addr==0x10 || addr==0x30) && reply->length==32) {
        const uint8_t *p=reply->payload;
        uint16_t mask=(uint16_t)p[2]|((uint16_t)p[3]<<8);
        append(info,sizeof(info),&used,"camera=%u mask=0x%03x",p[0],mask);
        if (mask&1) append(info,sizeof(info),&used," format=%.4s",(const char *)p+4);
        if (mask&6) append(info,sizeof(info),&used," resolution=%ux%u",
                           (unsigned)p[8]|((unsigned)p[9]<<8),(unsigned)p[10]|((unsigned)p[11]<<8));
        if (mask&8) append(info,sizeof(info),&used," fps=%u/%u",
                           (unsigned)p[12]|((unsigned)p[13]<<8),(unsigned)p[14]|((unsigned)p[15]<<8));
        if (mask&(1U<<4)) append(info,sizeof(info),&used," exposure=%s",p[16]?"manual":"auto");
        if (mask&(1U<<5)) append(info,sizeof(info),&used," exposure_time=%" PRIu32,
                                 (uint32_t)p[17]|((uint32_t)p[18]<<8)|((uint32_t)p[19]<<16)|((uint32_t)p[20]<<24));
        if (mask&(1U<<6)) append(info,sizeof(info),&used," gain=%u",(unsigned)p[21]|((unsigned)p[22]<<8));
        if (mask&(1U<<7)) append(info,sizeof(info),&used," autofocus=%s",p[23]?"on":"off");
        if (mask&(1U<<8)) append(info,sizeof(info),&used," focus=%u",(unsigned)p[24]|((unsigned)p[25]<<8));
        if (mask&(1U<<9)) append(info,sizeof(info),&used," white_balance=%s",p[26]?"auto":"manual");
        if (mask&(1U<<10)) append(info,sizeof(info),&used," white_balance_temp=%u",(unsigned)p[27]|((unsigned)p[28]<<8));
        if (mask&(1U<<11)) append(info,sizeof(info),&used," brightness=%d",(int16_t)((unsigned)p[29]|((unsigned)p[30]<<8)));
    }
    else if (type==1 && addr==5 && reply->length==8) {
        const uint8_t *p=reply->payload;
        append(info,sizeof(info),&used,"effective config: BME valid=%u t_os_code=%u p_os_code=%u h_os_code=%u filter_code=%u; IMU valid=%u accel=%u/%u gyro=%u/%u freefall=%u/%u wakeup=%u/%u",
               !!(p[0]&0x40),p[0]&7,(p[0]>>3)&7,p[1]&7,(p[1]>>3)&7,
               !!(p[6]&0x20),p[2]&7,(p[2]>>3)&3,p[2]>>5,p[3]&7,p[6]&7,p[4]&63,p[5]&63,p[4]>>6);
    }
    else if (type==1 && addr==6 && (reply->length==8 || reply->length==16 ||
             reply->length==24 || reply->length==10 || reply->length==58)) {
        static const char *const names[4][11] = {
            {"OBC_board_cC", "OBC_NTC1_cC", "OBC_NTC2_cC", "OBC_NTC3_cC", "OBC_NTC4_cC",
             "PDU_board_cC", "EXP_board_cC", "EXP_NTC1_cC", "EXP_NTC2_cC", "EXP_NTC3_cC", "EXP_NTC4_cC"},
            {"temperature_cC", "pressure_10Pa", "humidity_cPct"},
            {"temperature_cC", "accel_x_mg", "accel_y_mg", "accel_z_mg", "gyro_x_dps", "gyro_y_dps", "gyro_z_dps"},
            {"SLF1_flow_uL_min", "SLF1_temperature_cC", "SLF2_flow_uL_min", "SLF2_temperature_cC"}
        };
        static const char *const groups[] = {"Temperature", "EXP BME", "EXP IMU", "EXP Flow"};
        static const unsigned counts[] = {11, 3, 7, 4};
        unsigned selected = reply->length == 24 ? 0 : reply->length == 8 ? 1 :
                            reply->length == 16 ? 2 : 3;
        size_t off = 0;
        for (unsigned group = 0; group < 4; ++group) {
            if (reply->length != 58 && group != selected) continue;
            uint16_t mask = bee_dal_get_u16(reply->payload, &off);
            append(info,sizeof(info),&used,"%s%s valid_mask=0x%04x",off>2 ? "\n" : "",groups[group],mask);
            for (unsigned i = 0; i < counts[group]; ++i) {
                int16_t value = (int16_t)bee_dal_get_u16(reply->payload, &off);
                if (mask & (1u << i)) append(info,sizeof(info),&used," %s=%d",names[group][i],value);
                else append(info,sizeof(info),&used," %s=unavailable",names[group][i]);
            }
        }
    }
    else if (type==1 && addr==7 && reply->length>=10 &&
             reply->length==10U+12U*reply->payload[4]) {
        uint8_t board=reply->payload[0],count=reply->payload[4],next=reply->payload[5];
        size_t off=6;uint32_t epoch=bee_dal_get_u32(reply->payload,&off);
        append(info,sizeof(info),&used,"board=%s status=%s epoch=%" PRIu32 " records=%u next_cursor=",
               board==3 ? "OBC" : board==4 ? "EXP" : "PDU",
               reply->payload[3] ? "offline" : "online",epoch,count);
        if (next==255) append(info,sizeof(info),&used,"end");
        else append(info,sizeof(info),&used,"%u",next);
        for (unsigned i=0;i<count;i++) {
            uint8_t source=bee_dal_get_u8(reply->payload,&off);
            uint8_t channel=bee_dal_get_u8(reply->payload,&off);
            uint8_t flags=bee_dal_get_u8(reply->payload,&off);
            uint8_t unit=bee_dal_get_u8(reply->payload,&off);
            uint32_t age=bee_dal_get_u32(reply->payload,&off);
            int32_t value=(int32_t)bee_dal_get_u32(reply->payload,&off);
            append(info,sizeof(info),&used,"\n%s[%u] flags=0x%02X value=",
                   sensor_source_name(board,source),channel,flags);
            if (!(flags&1)) append(info,sizeof(info),&used,"%s",
                flags&0x20 ? "unsupported" : flags&0x10 ? "error" :
                flags&0x08 ? "disabled" : flags&0x02 ? "stale" : "unavailable");
            else if (unit==1) append(info,sizeof(info),&used,"%.2f C",value/100.0);
            else if (unit==2) append(info,sizeof(info),&used,"%" PRId32 " Pa",value);
            else if (unit==3) append(info,sizeof(info),&used,"%.2f %%RH",value/100.0);
            else if (unit==4) append(info,sizeof(info),&used,"%" PRId32 " ohm",value);
            else if (unit==5) append(info,sizeof(info),&used,"%" PRId32 " mg",value);
            else if (unit==6) append(info,sizeof(info),&used,"%.3f dps",value/1000.0);
            else if (unit==7) append(info,sizeof(info),&used,"%.2f uL/min",value/100.0);
            else if (unit==8) append(info,sizeof(info),&used,"%s",value ? "yes" : "no");
            else append(info,sizeof(info),&used,"%" PRId32,value);
            if (age!=UINT32_MAX) append(info,sizeof(info),&used," age=%" PRIu32 " ms",age);
        }
    }
    else if (type == 3 && (addr == 0 || addr == 4) && reply->length >= 12) {
        size_t off = 0;
        uint32_t hk = bee_dal_get_u32(reply->payload, &off);
        uint32_t sci1 = bee_dal_get_u32(reply->payload, &off);
        uint32_t sci2 = bee_dal_get_u32(reply->payload, &off);
        append(info, sizeof(info), &used, "HK=%" PRIu32 " SCI1=%" PRIu32 " SCI2=%" PRIu32, hk, sci1, sci2);
    }
    else if (type == 3 && addr == 1 && reply->length >= 41) {
        size_t off = 0;
        uint8_t dt = bee_dal_get_u8(reply->payload, &off);
        uint32_t index = bee_dal_get_u32(reply->payload, &off);
        uint32_t chunk = bee_dal_get_u32(reply->payload, &off);
        char name[21]; memcpy(name, reply->payload + off, 20); name[20] = 0; off += 20;
        uint32_t size = bee_dal_get_u32(reply->payload, &off);
        uint32_t crc = bee_dal_get_u32(reply->payload, &off);
        uint32_t count = bee_dal_get_u32(reply->payload, &off);
        append(info, sizeof(info), &used, "type=%u index=%" PRIu32 " chunk_size=%" PRIu32
               " name=%s size=%" PRIu32 " crc32=%08" PRIX32 " chunks=%" PRIu32,
               dt, index, chunk, name, size, crc, count);
    }
    else if (type == 4 && addr == 2 && reply->length == 64) {
        size_t off = 0;
        uint16_t valid = bee_dal_get_u16(reply->payload, &off);
        uint32_t epoch = bee_dal_get_u32(reply->payload, &off);
        uint32_t boot = bee_dal_get_u32(reply->payload, &off);
        append(info, sizeof(info), &used, "epoch=%" PRIu32 " bootcount=%" PRIu32
               " valid_mask=0x%04X\n", epoch, boot, valid);
        append(info, sizeof(info), &used, "onboard_temp=%.2f C\n",
               (int16_t)bee_dal_get_u16(reply->payload, &off) / 100.0);
        for (int i = 0; i < 4; ++i)
            append(info, sizeof(info), &used, "ntc%d=%.2f C%s\n", i + 1,
                   (int16_t)bee_dal_get_u16(reply->payload, &off) / 100.0,
                   valid & (1U << (i + 1)) ? "" : " (invalid)");
        double bme_temp = (int16_t)bee_dal_get_u16(reply->payload, &off) / 100.0;
        uint32_t pressure = bee_dal_get_u32(reply->payload, &off);
        double humidity = bee_dal_get_u16(reply->payload, &off) / 100.0;
        uint32_t gas = bee_dal_get_u32(reply->payload, &off);
        append(info, sizeof(info), &used, "bme_temp=%.2f C pressure=%" PRIu32
               " Pa humidity=%.2f %% gas=%" PRIu32 " ohm%s\n",
               bme_temp, pressure, humidity, gas,
               valid & (1U << 6) ? "" : " (gas invalid)");
        for (int i = 0; i < 8; ++i) {
            size_t v_off = off + 2 * i;
            size_t a_off = off + 16 + 2 * i;
            append(info, sizeof(info), &used, "pac%d_ch%d=%.3f V %.3f A%s\n", i / 4 + 1,
                   i % 4 + 1, bee_dal_get_u16(reply->payload, &v_off) / 1000.0,
                   (int16_t)bee_dal_get_u16(reply->payload, &a_off) / 1000.0,
                   valid & (1U << (i / 4 + 7)) ? "" : " (invalid)");
        }
    }
    else if (type == 4 && addr == 0 && reply->length == 57) {
        size_t off = 0;
        uint32_t epoch = bee_dal_get_u32(reply->payload, &off);
        int8_t status = reply->payload[off++];
        uint32_t boot = bee_dal_get_u32(reply->payload, &off);
        append(info, sizeof(info), &used, "epoch=%" PRIu32 " status=%d bootcount=%" PRIu32,
               epoch, status, boot);
        const char *names[]={"pdu_temp_cC","exp_temp_cC","obc_temp_cC",
            "ntc0_cC","ntc1_cC","ntc2_cC","ntc3_cC","mlx_avg_cC",
            "bme_temp_cC","bme_pressure_10Pa","bme_humidity_cPct",
            "imu_temp_cC","ax_mg","ay_mg","az_mg","gx_dps","gy_dps","gz_dps",
            "slf1_temp_cC","slf1_flow_uL_min","slf2_temp_cC","slf2_flow_uL_min","rba_status","ta_status"};
        for(unsigned i=0;i<24;i++) {
            uint16_t v=bee_dal_get_u16(reply->payload,&off);
            bool u=i==9 || i==10 || i>=22;
            if((u && v==UINT16_MAX) || (!u && v==0x8000)) append(info,sizeof(info),&used,"\n%s=unavailable",names[i]);
            else append(info,sizeof(info),&used,"\n%s=%d",names[i],u ? (int)v : (int)(int16_t)v);
        }
    }
    else if (type==1 && addr==0x14 && reply->length==4) {
        size_t off=0;
        append(info,sizeof(info),&used,"accepted uptime=%" PRIu32 "s; use thm_stat for applied PWM",
               bee_dal_get_u32(reply->payload,&off));
    }
    else if (type==1 && addr==0x15 && reply->length==8) {
        for (unsigned i=0;i<6;i++) {
            uint8_t value=reply->payload[i];
            append(info,sizeof(info),&used,"%s%s%u %s duty=%u%%",
                   i?"\n":"",i<4?"heater":"ir",i<4?i:i-4,
                   value&0x80u ? "running" : "stopped",value&0x7fu);
        }
    }
    else if (type==2 && addr==0x00 && reply->length==1) {
        static const char *const states[]={"standby","test","replace","circulation",
            "stopping","error","onground"};
        uint8_t p=reply->payload[0];
        append(info,sizeof(info),&used,"state=%s activated=%s error=%s case=%s circulation_pump_on=%s settling=%s",
            (p&7u)<7?states[p&7u]:"invalid",p&8u?"yes":"no",p&16u?"yes":"no",
            p&32u?"B":"A",p&64u?"yes":"no",p&128u?"yes":"no");
    }
    else if (type==2 && (addr==0x03 || addr==0x10) && reply->length==32) {
        const uint8_t *p=reply->payload;
        if (p[1]==0) {
            append(info,sizeof(info),&used,"heater profile=%u main_ntc=%u sec_ntc=%u target_cC=%d delta_cC=%d cutoff_cC=%d timeout_ms=%u resume=%u slew_up=%u slew_down=%u",
                p[2],p[3],p[4],(int16_t)(p[5]|(p[6]<<8)),(int16_t)(p[7]|(p[8]<<8)),
                (int16_t)(p[9]|(p[10]<<8)),(unsigned)(p[11]|(p[12]<<8)),p[13],p[14],p[15]);
        } else {
            append(info,sizeof(info),&used,"ir led=%u camera=%u roi=%u,%u,%u,%u target_cC=%d cutoff_cC=%d rise_cC=%d sample_ms=%u timeout_ms=%u max_duty=%u slew=%u",
                p[2],p[3],p[4],p[5],p[6],p[7],(int16_t)(p[8]|(p[9]<<8)),
                (int16_t)(p[10]|(p[11]<<8)),(int16_t)(p[12]|(p[13]<<8)),
                (unsigned)(p[14]|(p[15]<<8)),(unsigned)(p[16]|(p[17]<<8)),p[18],p[19]);
        }
        unsigned gain_off=p[1]?20:16;
        float gains[3];
        for (unsigned j=0;j<3;j++) {
            unsigned o=gain_off+4*j;
            uint32_t bits=(uint32_t)p[o]|((uint32_t)p[o+1]<<8)|((uint32_t)p[o+2]<<16)|((uint32_t)p[o+3]<<24);
            memcpy(&gains[j],&bits,4);
        }
        append(info,sizeof(info),&used," kp=%.3f ki=%.3f kd=%.3f",gains[0],gains[1],gains[2]);
    }
    else if (type==2 && addr==0x04 && reply->length==1) {
        uint8_t value=reply->payload[0];
        bool ir=strstr(command,"thp_ctl ir ")!=NULL;
        static const char *const heater_states[]={"disabled","soak","error"};
        static const char *const ir_states[]={"idle","starting","wait_sample","running","fault"};
        static const char *const heater_reasons[]={"none","config","sensor","over_temp","ntc_delta","power","restored","manual_timeout"};
        static const char *const ir_reasons[]={"none","config","owner","camera","stale","hot","rise","ir_service"};
        unsigned phase=value&7u,reason=(value>>3)&7u;
        if (phase>=(ir?5u:3u)) ok=false;
        append(info,sizeof(info),&used,"%s phase=%s reason=%s configured=%s running=%s",
            ir?"ir":"heater",phase<(ir?5u:3u)?(ir?ir_states[phase]:heater_states[phase]):"invalid",
            ir?ir_reasons[reason]:heater_reasons[reason],value&0x40u?"yes":"no",value&0x80u?"yes":"no");
    }
    else if (((type==1 && (addr==0x16 || addr==0x36)) || (type==2 && (addr==0x01 || addr==0x12))) && reply->length==32) {
        if (reply->payload[0]==1 && reply->payload[1]==9) {
            static const char *const states[]={"queued","accepted","done","refused",
                "io_error","power_lost","cancelled","busy","interlocked",
                "not_ready","invalid","unknown"};
            const uint8_t *p=reply->payload;
            if (p[8]>2) ok=false;
            uint32_t id=(uint32_t)p[4]|((uint32_t)p[5]<<8)|
                ((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
            uint32_t error=(uint32_t)p[9]|((uint32_t)p[10]<<8)|
                ((uint32_t)p[11]<<16)|((uint32_t)p[12]<<24);
            append(info,sizeof(info),&used,"pump %s target=%s state=%s request_id=%" PRIu32 " driver_error=%" PRIu32,
                p[2]?"result":"detect",p[3]==0?"u11":p[3]==1?"u15":"unspecified",
                p[8]<12?states[p[8]]:"invalid",id,error);
        } else if (strncmp(command,"fld_cfg get",11)!=0 &&
            strncmp(command,"pc_cfg get",10)!=0 && reply->payload[0]==1 &&
            reply->payload[1]>=1 && reply->payload[1]<=11)
            append(info,sizeof(info),&used,"configuration accepted; group=%u",reply->payload[1]);
        else {
            unsigned total=reply->payload[0]|((unsigned)reply->payload[1]<<8);
            unsigned offset=reply->payload[2]|((unsigned)reply->payload[3]<<8);
            append(info,sizeof(info),&used,"config JSON offset=%u total=%u data=",offset,total);
            for (unsigned i=0;i<28 && offset+i<total;i++)
                append(info,sizeof(info),&used,"%c",reply->payload[4+i]);
        }
    }
    else if (type==1 && addr==0x34 && reply->length==4) {
        uint32_t id=(uint32_t)reply->payload[0]|((uint32_t)reply->payload[1]<<8)|
            ((uint32_t)reply->payload[2]<<16)|((uint32_t)reply->payload[3]<<24);
        append(info,sizeof(info),&used,"pump request queued id=%" PRIu32 "; check fld_cfg pump result %" PRIu32,
               id,id);
    }
    else if (type==1 && addr==0x33 && reply->length==4) {
        uint32_t id=(uint32_t)reply->payload[0]|((uint32_t)reply->payload[1]<<8)|
            ((uint32_t)reply->payload[2]<<16)|((uint32_t)reply->payload[3]<<24);
        append(info,sizeof(info),&used,"motor request id=%" PRIu32 "; check rba_job result %" PRIu32,id,id);
    }
    else if (type==1 && addr==0x33 && reply->length==12) {
        static const char *const states[]={"queued","accepted","done","invalid","busy",
            "not_ready","refused","not_homed","limit_reached","fault","power_lost",
            "cancelled","storage_error","not_found","limit_invalid","power_off",
            "spi_error","wrong_device"};
        const uint8_t *p=reply->payload;
        uint32_t id=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
        int32_t position=(int32_t)((uint32_t)p[8]|((uint32_t)p[9]<<8)|
            ((uint32_t)p[10]<<16)|((uint32_t)p[11]<<24));
        append(info,sizeof(info),&used,"motor request id=%" PRIu32 " state=%s position=%" PRId32,
            id,p[4]<sizeof(states)/sizeof(*states)?states[p[4]]:"unknown",position);
    }
    else if (type==1 && addr==0x1b && reply->length==16) {
        static const char *const states[]={"stopped","moving","jogging","homing","holding","error"};
        const uint8_t *p=reply->payload;
        append(info,sizeof(info),&used,"driver initialized=%s asleep=%s fault=%s diagnostic=%s burn_reserved=%s limits_powered=%s",
            p[0]&1?"yes":"no",p[0]&2?"yes":"no",p[0]&4?"yes":"no",
            p[0]&8?"yes":"no",p[0]&16?"yes":"no",p[0]&32?"yes":"no");
        for (unsigned i=0;i<2;i++) {
            unsigned o=6+4*i;
            int32_t position=(int32_t)((uint32_t)p[o]|((uint32_t)p[o+1]<<8)|
                ((uint32_t)p[o+2]<<16)|((uint32_t)p[o+3]<<24));
            append(info,sizeof(info),&used,"\nmotor%u state=%s position=%" PRId32 " reference=%s direction=%s speed=%u steps/s",
                i,p[4+i]<6?states[p[4+i]]:"invalid",position,
                p[3]&(1u<<i)?"valid":"invalid",p[3]&(1u<<(i+2))?"reverse":"forward",p[14+i]);
        }
        for (unsigned i=0;i<4;i++)
            append(info,sizeof(info),&used,"\nlimit%u=%s",i,
                !(p[2]&(1u<<i))?"invalid":p[1]&(1u<<i)?"active":"inactive");
    }
    else if (type==1 && addr==0x18 && reply->length==16) {
        const uint8_t *p=reply->payload;
        append(info,sizeof(info),&used,"valves commanded=0x%02x valid=0x%02x busy=%s driver_ready=%s\n"
               "U11 type=%u rail=%s initialized=%s enabled=%s pump_busy=%s frequency=%uHz voltage=%uVpp\n"
               "U15 initialized=%s enabled=%s frequency=%uHz voltage=[%u,%u,%u,%u]Vpp",
               p[0],p[1],p[2]&32?"yes":"no",p[2]&64?"yes":"no",
               p[3],p[2]&1?"on":"off",p[2]&2?"yes":"no",p[2]&4?"yes":"no",p[2]&128?"yes":"no",
               p[4]|((unsigned)p[5]<<8),p[6],p[2]&8?"yes":"no",p[2]&16?"yes":"no",
               p[7]|((unsigned)p[8]<<8),p[9],p[10],p[11],p[12]);
    }
    else if (type==1 && addr==0x13 && reply->length==32 && reply->payload[0]==1) {
        const uint8_t *p=reply->payload;
        for (unsigned i=0;i<4;i++) {
            unsigned o=2+4*i;
            unsigned on=p[o+2]|((unsigned)p[o+3]<<8);
            append(info,sizeof(info),&used,"%sheater%u max_duty=%u%% slew=%u pct/s max_on_ms=%u",
                   i?"\n":"",i,p[o],p[o+1],on*10);
        }
        for (unsigned i=0;i<2;i++) {
            unsigned o=18+7*i;
            uint32_t hz=(uint32_t)p[o+2]|((uint32_t)p[o+3]<<8)|
                ((uint32_t)p[o+4]<<16)|((uint32_t)p[o+5]<<24);
            append(info,sizeof(info),&used,"\nir%u max_duty=%u%% max_on_ms=%u pwm_hz=%" PRIu32 " min_off_ms=%u",
                   i,p[o],p[o+1],hz,p[o+6]*100);
        }
    }
    else append(info, sizeof(info), &used, "payload_length=%u", reply->length);

    used = 0;
    out[0] = 0;
    if (json) {
        append(out, cap, &used, "{\"ok\":%s,\"command\":", ok ? "true" : "false");
        put_json_string(out, cap, &used, command);
        append(out, cap, &used, ",\"type\":%u,\"address\":%u,\"ack\":%u,\"code\":%u,\"length\":%u,\"text\":",
               type, addr, reply->ack_err, reply->addr_errcode, reply->length);
        put_json_string(out, cap, &used, info);
        append(out, cap, &used, ",\"payload_hex\":\"");
        for (uint16_t i = 0; i < reply->length; ++i) append(out, cap, &used, "%02x", reply->payload[i]);
        append(out, cap, &used, "\"}\n");
    } else {
        append(out, cap, &used, "%s %s: %s\n", ok ? "OK" : "ERROR", command, info);
        if (reply->length && !(type == 0xff && addr == 1) &&
            !(type == 2 && (addr == BEE_DAL_EXP_CSP_SNAPSHOT ||
                            (addr >= BEE_DAL_EXP_PING && addr <= BEE_DAL_EXP_RUN)))) {
            append(out, cap, &used, "payload: ");
            for (uint16_t i = 0; i < reply->length; ++i) append(out, cap, &used, "%02x", reply->payload[i]);
            append(out, cap, &used, "\n");
        }
    }
}

static void handle_client(int fd)
{
    char line[REQUEST_MAX + 1];
    size_t len = 0;
    while (len < REQUEST_MAX) {
        ssize_t n = read(fd, line + len, REQUEST_MAX - len);
        if (n <= 0) break;
        len += (size_t)n;
        if (memchr(line, '\n', len)) break;
    }
    line[len] = 0;
    char *nl = strchr(line, '\n'); if (nl) *nl = 0;
    if (len == REQUEST_MAX && !nl) {
        const char *error="ERROR command too long\n";
        send_logged(fd,line,error,strlen(error),false,false);return;
    }
    bool json = false;
    char *cmd = line;
    if (!strncmp(cmd, "json ", 5)) { json = true; cmd += 5; }
    while (*cmd == ' ' || *cmd == '\t') ++cmd;
    char command[REQUEST_MAX + 1];
    snprintf(command, sizeof(command), "%s", cmd);
    if (!strcmp(cmd, "help")) {
        const char *help = "ping | sys_info [self_test] | sys_rtc [epoch|now] | pdu_vout get|set <mV> | pdu_fault_clear\n"
                           "pdu_heartbeat get|enable on|off|on <ms>|off <ms> | pdu_bootloader enter\n"
                           "pwr_en get|on <global_mask>|off <global_mask> [force] (ICD 01/03)\n"
                           "pwr_en <OBC|EXP|PDU> get | <output> on|off (DAL extension 01/32)\n"
                           "OBC outputs: AI_power FPGA_power USB1_power USB2_power USB3_power USB4_power MIPI1_power MIPI2_power\n"
                           "EXP outputs: EXP12V SLN_HD MOTOR (force for off)\n"
                           "PDU outputs: VOUT TRANS\n"
                           "For multiple outputs: pwr_en <board> set <select_mask> <on_mask> [force for EXP off]\n"
                           "pwr_stat all|monitor|enable|error\n"
                           "sen_cfg get | sen_cfg set bme <temp_os> <press_os> <hum_os> <filter>\n"
                           "sen_cfg set imu accel|gyro <odr> <scale> | sen_cfg set imu freefall|wakeup <threshold> <duration>\n"
                           "sen_stat all|temperature|bme|imu|flow (ICD v1.2)\n"
                           "sen_stat <OBC|EXP|PDU> get <all|temperature|bme|imu|flow|power|camera|limits> [source] [cursor n]\n"
                           "uvc_cfg <1|2|3|4> get|set <setting> <value> [setting value ...]\n"
                           "uvc_ctl <1|2|3|4> capture <request_id> | uvc_stat <1|2|3|4> get <request_id>\n"
                           "thm_cfg get | thm_cfg heater <0-3> set max_duty <0-100> slew <0-100> max_on_ms <10-600000>\n"
                           "thm_cfg ir <0-1> set max_duty <0-25> max_on_ms <1-100> pwm_hz <100-100000> min_off_ms <0-25500>\n"
                           "thm_ctl heater <0-3> set <duty_pct> <duration_ms>|off\n"
                           "thm_ctl ir <0-1> set <duty_pct>|off|feedback_start\n"
                           "thm_stat [get] (all four heaters and both IR LEDs)\n"
                           "fld_cfg get [page] | valve map <v1> <v2> <v3> <v4> | valve pulse <ms>\n"
                           "fld_cfg flow channel <0|1> | flow interval <ms> | route <standby|test|replace|circulation|ground> [case|preset] <v1> <v2> <v3> <v4>\n"
                           "fld_cfg profile <global|test|replace|circulation> <field> <value> | resume <0|1> | pump_type <lowdriver|highdriver>\n"
                           "fld_cfg pump detect <u11|u15> | pump result <request_id>\n"
                           "pc_cfg get [page] | valve map/pulse ... | flow channel/interval ... | route ... | profile <global|test|replace|circulation> <field> <value> | resume <0|1> | datalog on|off | defaults\n"
                           "pc_ctl activate|deactivate|test [a|b|both]|replace [a|b|both]|circulation [a|b|both]|ground <preset>|standby|stop|reset_error\n"
                           "pc_stat [get] (one-byte PC experiment state and flags)\n"
                           "thp_cfg heater get <0-3>|set <profile> <main_ntc> <sec_ntc> <target_cC> <delta_cC> <cutoff_cC> <timeout_ms> <resume> <slew_up> <slew_down> <kp> <ki> <kd>\n"
                           "thp_cfg ir get <0-1>|set <led> <camera> <x> <y> <width> <height> <target_cC> <cutoff_cC> <rise_cC> <sample_ms> <timeout_ms> <max_duty> <slew> <kp> <ki> <kd>\n"
                           "thp_ctl heater <0-3> start|stop|status|resume | thp_ctl ir <0-1> start|stop|status\n"
                           "fld_ctl valve mask <0-15> | pump <u11|u15> on|off|frequency <Hz>|voltage [channel] <Vpp>\n"
                           "fld_stat [get] (valves and U11/U15 pump actuator state)\n"
                           "rba_ctl init|disable|move <0|1> <steps> [speed]|goto <0|1> <position> [speed]|jog <0|1> <cw|ccw> [speed]|home <0|1>|stop <0|1|all>|stop_hold <0|1>|result <id>\n"
                           "rba_job <same actions as rba_ctl, including result> (DAL 01/33, returns job IDs)\n"
                           "fld_job pump <same arguments as fld_ctl> (DAL 01/34, returns job IDs)\n"
                           "uvc_job <camera> capture <request_id> (DAL 01/35, returns job state)\n"
                           "uvc_cfg get | uvc_stat | thp_cfg get | thp_en get (native empty GET)\n"
                           "rba_stat [get] (both motors and four optical limits)\n"
                           "tm_get | tm_obc_get\n"
                           "exp_ping | exp_tlm <section> | exp_run <EXP command>\n"
                           "burn_cfg_set <0|1> <duty_pct|burn_time_ms|max_total_current_ma> <value> (EXP 02/0C)\n"
                           "exp_csp_get <table> <field> | exp_csp_set 5 <field> <value...>\n"
                           "exp_csp_run <EXP command> | exp_csp_refresh 0 | exp_csp_result <ticket>\n"
                           "exp_csp_snapshot <0|2|3|6|12>\n"
                           "sen_bme_cfg EXP get|set <temp_os> <press_os> <hum_os> <filter>|result <id>\n"
                           "sen_imu_cfg EXP get|reset|set accel|gyro <odr> <scale>|set freefall|wakeup <threshold> <duration>|result <id>\n"
                           "dat_lst <mark> | dat_sel <type> <index> <chunk_size> | dat_get <chunk>\n"
                           "dat_des | dat_del <type> <index> | dat_mrk <mark> (alias dat_mark)\n"
                           "shell <Linux command> | gui_state | gui_shell <seconds> <command>\n"
                           "port_cfg get|set <KEY> <value>|restart (local console only)\n"
                           "raw <type> <address> [byte ...] | debug_handoff yes\n"
                           "Values accept decimal or 0x hex.";
        if (json) {
            char out[4096] = "{\"ok\":true,\"help\":";
            size_t used = strlen(out);
            put_json_string(out, sizeof(out), &used, help);
            append(out, sizeof(out), &used, "}\n");
            send_logged(fd,line,out,strlen(out),false,true);
        } else {
            char out[8192];
            int n=snprintf(out,sizeof(out),"%s\nUse --json on the client for JSON replies.\n",help);
            if (n>0 && (size_t)n<sizeof(out)) send_logged(fd,line,out,(size_t)n,false,true);
        }
        return;
    }
    if (!strcmp(cmd,"gui_state")) {
        char *out=malloc(RESPONSE_MAX);
        if (!out) {
            const char *error="{\"ok\":false,\"error\":\"out of memory\"}\n";
            send_logged(fd,line,error,strlen(error),false,false);return;
        }
        size_t count=gui_state_json(out,RESPONSE_MAX);
        send_logged(fd,line,out,count,true,true);
        free(out);
        return;
    }
    if (!strncmp(cmd,"port_cfg",8) && (cmd[8] == '\0' || cmd[8] == ' ')) {
        char result[2048] = "";
        bool ok = false;
        if (!strcmp(cmd,"port_cfg get")) {
            ok = port_config_get(result,sizeof(result)) == 0;
            if (!ok) snprintf(result,sizeof(result),"cannot read port configuration");
        } else if (!strncmp(cmd,"port_cfg set ",13)) {
            char key[128], value[256], extra[2];
            if (sscanf(cmd+13,"%127s %255s %1s",key,value,extra) == 2) {
                ok = port_config_set(key,value,result,sizeof(result)) == 0;
                if (ok) snprintf(result,sizeof(result),"saved; restart flight software to apply");
            } else snprintf(result,sizeof(result),"use port_cfg set <KEY> <value>");
        } else if (!strcmp(cmd,"port_cfg restart")) {
            ok = geteuid() == 0;
            if (ok) {
                pid_t child = fork();
                if (child == 0) {
                    close(fd);
                    setsid();
                    sleep(1);
                    execlp("systemctl","systemctl","restart","dal_obc_fsw.service",(char *)NULL);
                    _exit(127);
                }
                ok = child > 0;
            }
            snprintf(result,sizeof(result),"%s",ok ? "service restart scheduled" : "cannot schedule restart (requires root and process slot)");
        } else snprintf(result,sizeof(result),"use port_cfg get|set <KEY> <value>|restart");
        char out[4096] = "";
        size_t used = 0;
        append(out,sizeof(out),&used,"{\"ok\":%s,\"text\":",ok ? "true" : "false");
        put_json_string(out,sizeof(out),&used,result);
        append(out,sizeof(out),&used,"}\n");
        send_logged(fd,line,out,strlen(out),false,ok);
        return;
    }
    if (!strncmp(cmd,"gui_shell ",10)) {
        struct gui_shell_job *job=calloc(1,sizeof(*job));
        if (!job) {
            const char *error="{\"ok\":false,\"error\":\"out of memory\"}\n";
            send_logged(fd,line,error,strlen(error),false,false);return;
        }
        job->fd=dup(fd);
        if (job->fd<0) {
            free(job);
            const char *error="{\"ok\":false,\"error\":\"dup failed\"}\n";
            send_logged(fd,line,error,strlen(error),false,false);return;
        }
        snprintf(job->request,sizeof(job->request),"%s",cmd+10);
        snprintf(job->logged_request,sizeof(job->logged_request),"%s",line);
        pthread_t worker;
        if (pthread_create(&worker,NULL,gui_shell_worker,job)!=0) {
            close(job->fd);free(job);
            const char *error="{\"ok\":false,\"error\":\"thread failed\"}\n";
            send_logged(fd,line,error,strlen(error),false,false);
        } else pthread_detach(worker);
        return;
    }
    uint8_t type, addr, payload[BEE_DAL_MAX_PAYLOAD];
    uint16_t length;
    if (parse_command(cmd, &type, &addr, payload, &length)) {
        const char *error=json ? "{\"ok\":false,\"error\":\"invalid command or arguments\"}\n" :
                                 "ERROR invalid command or arguments; type help\n";
        send_logged(fd,line,error,strlen(error),false,false);
        return;
    }
    bee_dal_local_reply_t *reply = malloc(sizeof(*reply));
    char *out = malloc(RESPONSE_MAX);
    if (!reply || !out) {
        const char *error="ERROR out of memory\n";
        send_logged(fd,line,error,strlen(error),false,false);
        free(reply); free(out); return;
    }
    if (bee_dal_tmtc_execute_local(type, addr, payload, length, reply) == XERR_OK) {
        render_reply(command, type, addr, reply, json, out, RESPONSE_MAX);
        bool success=json ? !strncmp(out,"{\"ok\":true",10) : !strncmp(out,"OK ",3);
        send_logged(fd,line,out,strlen(out),routine_console_read(type,addr),success);
    } else {
        const char *error="ERROR dispatch failed\n";
        send_logged(fd,line,error,strlen(error),false,false);
    }
    free(reply); free(out);
}

static void *console_thread(void *unused)
{
    (void)unused;
    while (g_running) {
        int fd = accept(g_listen_fd, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; if (g_running) g_running = false; break; }
        struct timeval timeout = { .tv_sec = 5 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        handle_client(fd);
        close(fd);
    }
    return NULL;
}

xerr_t flight_console_service_init(void)
{
    const char *override = getenv("DAL_FLIGHT_CONSOLE_SOCKET");
    const char *path = override && *override ? override : DAL_OBC_CONSOLE_SOCKET;
    if (strlen(path) >= sizeof(g_path)) return XERR_INVAL;
    strcpy(g_path, path);
    char dir[sizeof(g_path)]; strcpy(dir, g_path);
    char *slash = strrchr(dir, '/');
    if (!slash) return XERR_INVAL;
    *slash = 0;
    if (!override) { mkdir("/run/dal", 0755); mkdir(dir, 0700); }
    struct stat st;
    if (lstat(g_path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) return XERR_IO;
        if (unlink(g_path)) return XERR_IO;
    }
    g_listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_listen_fd < 0) return XERR_IO;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy(addr.sun_path, g_path);
    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) || chmod(g_path, 0600) ||
        listen(g_listen_fd, 4)) {
        close(g_listen_fd); g_listen_fd = -1; unlink(g_path); return XERR_IO;
    }
    printf("[CONSOLE] Listening on %s\n", g_path);
    return XERR_OK;
}

xerr_t flight_console_service_start(void)
{
    if (g_listen_fd < 0 || g_running) return XERR_STATE;
    g_running = true;
    if (pthread_create(&g_thread, NULL, console_thread, NULL)) {
        g_running = false; return XERR_IO;
    }
    return XERR_OK;
}

void flight_console_service_stop(void)
{
    if (g_running) {
        g_running = false;
        shutdown(g_listen_fd, SHUT_RDWR);
        pthread_join(g_thread, NULL);
    }
    if (g_listen_fd >= 0) { close(g_listen_fd); g_listen_fd = -1; }
    if (*g_path) unlink(g_path);
}

bool flight_console_service_is_running(void) { return g_running; }
