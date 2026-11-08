#include "services/gui_state.h"
#include "services/hk_service.h"
#include "services/exp_uart_service.h"
#include "dal_obc_config.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <linux/rtc.h>

#define EXP_PATH "/run/dal/dal_obc_fsw/exp_data.json"
/* OBC PC/104 RS232_1 to PDU USART0. RS485 is /dev/ttyS2. */
#define PDU_RS232_PORT "/dev/ttyS3"
#define PWM_PATH "/sys/class/pwm/pwmchip0/pwm3"

typedef struct {
    bool online;
    time_t stamp;
    int temp_tenth;
    unsigned mode, flags;
    uint16_t valid;
    uint8_t tps_state;
    bool vout, trans;
    unsigned set_mv, vout_mv, vout_ma, trans_mv, trans_ma, in12_mv, in5_mv;
} pdu_t;
static pthread_t g_pdu_thread;
static pthread_mutex_t g_pdu_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_pdu_io_lock = PTHREAD_MUTEX_INITIALIZER;
static pdu_t g_pdu;
static uint8_t g_pdu_version[3];
static uint32_t g_pdu_build;
static time_t g_pdu_version_stamp;
static atomic_bool g_pdu_running;
static int poll_pdu(pdu_t *result);
static uint64_t tps_started_ms;
static bool tps_was_on;
static uint64_t monotonic_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000U+(uint64_t)t.tv_nsec/1000000U;
}

static void store_pdu(const pdu_t *sample)
{
    pthread_mutex_lock(&g_pdu_lock);
    g_pdu = *sample;
    pthread_mutex_unlock(&g_pdu_lock);
}

static int cached_pdu(pdu_t *sample)
{
    pthread_mutex_lock(&g_pdu_lock);
    *sample = g_pdu;
    pthread_mutex_unlock(&g_pdu_lock);
    return sample->online && sample->stamp &&
        time(NULL) >= sample->stamp && time(NULL) - sample->stamp <= 15 ? 0 : -1;
}

static void add(char *out, size_t cap, size_t *used, const char *fmt, ...)
{
    if (*used >= cap) return;
    va_list args; va_start(args,fmt);
    int n=vsnprintf(out+*used,cap-*used,fmt,args);
    va_end(args);
    if (n>0) *used += (size_t)n<cap-*used ? (size_t)n : cap-*used-1;
}
static const char *setting(const char *name, const char *fallback)
{
    const char *value=getenv(name);
    return value && *value ? value : fallback;
}
static int read_int(const char *path, int *value)
{
    FILE *fp=fopen(path,"r"); if (!fp) return -1;
    int ok=fscanf(fp,"%d",value)==1 ? 0 : -1;
    fclose(fp); return ok;
}
static void raw_file(char *out, size_t cap, size_t *used, const char *path)
{
    char data[16384];
    FILE *fp=fopen(path,"r");
    if (!fp) {add(out,cap,used,"null");return;}
    size_t n=fread(data,1,sizeof(data)-1,fp);
    int extra=fgetc(fp); fclose(fp);
    if (extra!=EOF || n<2) {add(out,cap,used,"null");return;}
    while (n && (data[n-1]=='\n' || data[n-1]=='\r' || data[n-1]==' ')) --n;
    data[n]=0;
    if (n<2 || data[0]!='{' || data[n-1]!='}') {add(out,cap,used,"null");return;}
    add(out,cap,used,"%s",data);
}
static uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc=0xffff;
    for (size_t i=0;i<length;i++) {
        crc^=data[i];
        for (int b=0;b<8;b++) crc=(crc>>1) ^ ((crc&1) ? 0xa001 : 0);
    }
    return crc;
}
static int read_exact(int fd, uint8_t *buf, size_t length)
{
    size_t used=0;
    while (used<length) {
        struct pollfd pollfd={.fd=fd,.events=POLLIN};
        if (poll(&pollfd,1,500)<=0) return -1;
        ssize_t n=read(fd,buf+used,length-used);
        if (n<=0) return -1;
        used+=(size_t)n;
    }
    return 0;
}
static int modbus_read(int fd, uint8_t unit, uint8_t func, uint16_t offset,
                       uint16_t count, uint16_t *words)
{
    uint8_t req[8]={unit,func,offset>>8,offset&255,count>>8,count&255};
    uint16_t crc=crc16(req,6);req[6]=crc&255;req[7]=crc>>8;
    tcflush(fd,TCIFLUSH);
    if (write(fd,req,sizeof(req))!=(ssize_t)sizeof(req) || tcdrain(fd)!=0) return -1;
    uint8_t reply[64];
    if (read_exact(fd,reply,3)!=0 || reply[0]!=unit) {
        fprintf(stderr,"[PDU] Modbus read failed: FC=%u offset=%u count=%u header/timeout\n",func,offset,count);
        return -1;
    }
    if(reply[1]==(uint8_t)(func|0x80)) {
        if(read_exact(fd,reply+3,2)==0 && crc16(reply,3)==(uint16_t)(reply[3]|reply[4]<<8))
            fprintf(stderr,"[PDU] Modbus exception: FC=%u offset=%u count=%u exception=0x%02X\n",func,offset,count,reply[2]);
        return -1;
    }
    if(reply[1]!=func) return -1;
    size_t bytes=reply[2], expected=func<=2 ? (count+7)/8 : count*2;
    if (bytes!=expected || bytes+5>sizeof(reply) || read_exact(fd,reply+3,bytes+2)!=0) return -1;
    crc=crc16(reply,bytes+3);
    if (reply[bytes+3]!=(uint8_t)crc || reply[bytes+4]!=(uint8_t)(crc>>8)) return -1;
    for (uint16_t i=0;i<count;i++)
        words[i]=func<=2 ? (reply[3+i/8]>>(i%8))&1 : (uint16_t)(reply[3+2*i]<<8|reply[4+2*i]);
    return 0;
}
static int modbus_write_single(int fd, uint8_t unit, uint16_t offset, uint16_t value)
{
    uint8_t request[8]={unit,6,offset>>8,offset&255,value>>8,value&255};
    uint16_t crc=crc16(request,6);
    request[6]=(uint8_t)crc;request[7]=(uint8_t)(crc>>8);
    tcflush(fd,TCIFLUSH);
    if (write(fd,request,sizeof(request))!=(ssize_t)sizeof(request) || tcdrain(fd)!=0) return -1;
    uint8_t reply[8];
    if (read_exact(fd,reply,2)!=0 || reply[0]!=unit) return -1;
    if (reply[1]==(uint8_t)(6|0x80)) {
        (void)read_exact(fd,reply+2,3); /* Modbus exception response. */
        return -1;
    }
    if (reply[1]!=6 || read_exact(fd,reply+2,6)!=0) return -1;
    return memcmp(reply,request,sizeof(request))==0 ? 0 : -1;
}
static int modbus_write_pair(int fd, uint8_t unit, uint16_t offset,
                             uint16_t first, uint16_t second)
{
    uint8_t request[13]={unit,16,offset>>8,offset&255,0,2,4,
                         first>>8,first&255,second>>8,second&255};
    uint16_t crc=crc16(request,11);
    request[11]=(uint8_t)crc;request[12]=(uint8_t)(crc>>8);
    tcflush(fd,TCIFLUSH);
    if (write(fd,request,sizeof(request))!=(ssize_t)sizeof(request) || tcdrain(fd)!=0) return -1;
    uint8_t reply[8];
    if (read_exact(fd,reply,2)!=0 || reply[0]!=unit) return -1;
    if (reply[1]==(uint8_t)(16|0x80)) {
        (void)read_exact(fd,reply+2,3);
        return -1;
    }
    if (reply[1]!=16 || read_exact(fd,reply+2,6)!=0) return -1;
    if (memcmp(reply,request,6)!=0) return -1;
    crc=crc16(reply,6);
    return reply[6]==(uint8_t)crc && reply[7]==(uint8_t)(crc>>8) ? 0 : -1;
}
static int modbus_write_coil(int fd, uint8_t unit, uint16_t offset, bool on)
{
    uint16_t value=on ? 0xff00 : 0;
    uint8_t request[8]={unit,5,offset>>8,offset&255,value>>8,value&255};
    uint16_t crc=crc16(request,6);
    request[6]=(uint8_t)crc;request[7]=(uint8_t)(crc>>8);
    tcflush(fd,TCIFLUSH);
    if (write(fd,request,sizeof(request))!=(ssize_t)sizeof(request) || tcdrain(fd)!=0) return -1;
    uint8_t reply[8];
    if (read_exact(fd,reply,2)!=0 || reply[0]!=unit) return -1;
    if (reply[1]==(uint8_t)(5|0x80)) {
        (void)read_exact(fd,reply+2,3);
        return -1;
    }
    if (reply[1]!=5 || read_exact(fd,reply+2,6)!=0) return -1;
    return memcmp(reply,request,sizeof(request))==0 ? 0 : -1;
}
static int open_pdu(void)
{
    const char *path=setting("DAL_GUI_PDU_PORT",PDU_RS232_PORT);
    int fd=open(path,O_RDWR|O_NOCTTY|O_CLOEXEC);
    if (fd<0) return -1;
    struct termios term;
    if (tcgetattr(fd,&term)!=0) {close(fd);return -1;}
    cfmakeraw(&term);cfsetispeed(&term,B115200);cfsetospeed(&term,B115200);
    term.c_cflag=(term.c_cflag&~(PARENB|CSTOPB|CSIZE|CRTSCTS))|CS8|CLOCAL|CREAD;
    term.c_cc[VMIN]=0;term.c_cc[VTIME]=1;
    if (tcsetattr(fd,TCSANOW,&term)!=0) {close(fd);return -1;}
    return fd;
}
static int pdu_unit(void)
{
    unsigned unit=1;
    const char *unit_env=getenv("DAL_GUI_PDU_UNIT");
    if (unit_env && *unit_env) {char *end;unsigned long n=strtoul(unit_env,&end,10);if (*end || n<1 || n>247) return -1;unit=(unsigned)n;}
    return (int)unit;
}
static int poll_pdu_version(uint8_t version[3], uint32_t *build_code)
{
    if (!version || !build_code) return -1;
    pthread_mutex_lock(&g_pdu_io_lock);
    int fd=open_pdu(), unit=pdu_unit(), ok=-1;
    uint16_t words[10];
    if (fd>=0) {
        if (unit>0 && modbus_read(fd,(uint8_t)unit,3,99,10,words)==0 &&
            words[0]==0x5044 && words[1]==0x5500 &&
            (words[3]>>8)==0) {
            version[0]=(uint8_t)(words[2]>>8);
            version[1]=(uint8_t)words[2];
            version[2]=(uint8_t)words[3];
            *build_code=((uint32_t)words[7]<<16)|words[8];
            ok=0;
        }
        close(fd);
    }
    pthread_mutex_unlock(&g_pdu_io_lock);
    return ok;
}
int gui_pdu_read_version(uint8_t version[3], uint32_t *build_code)
{
    if (!version || !build_code) return -1;
    pthread_mutex_lock(&g_pdu_lock);
    int ok = g_pdu_version_stamp && time(NULL) >= g_pdu_version_stamp &&
        time(NULL) - g_pdu_version_stamp <= 120;
    if (ok) {
        memcpy(version, g_pdu_version, 3);
        *build_code = g_pdu_build;
    }
    pthread_mutex_unlock(&g_pdu_lock);
    return ok ? 0 : -1;
}
int gui_pdu_vout(bool set, uint16_t target_mv, gui_pdu_vout_t *result)
{
    if (!result) return -1;
    if (!set) {
        pdu_t sample;
        if (cached_pdu(&sample) != 0) return -1;
        *result = (gui_pdu_vout_t){
            .setpoint_mv=(uint16_t)sample.set_mv,
            .voltage_mv=(uint16_t)sample.vout_mv,
            .current_ma=(uint16_t)sample.vout_ma,
            .flags=(uint16_t)((sample.trans ? GUI_PDU_VOUT_TRANS_ON : 0) |
                (sample.vout ? GUI_PDU_VOUT_ENABLED : 0) |
                ((sample.valid&PDU_VALID_VOUT) ? GUI_PDU_VOUT_STATE_VALID : 0) |
                (sample.set_mv!=UINT16_MAX ? GUI_PDU_VOUT_SET_VALID : 0) |
                (sample.vout_mv!=UINT16_MAX ? GUI_PDU_VOUT_VOLT_VALID : 0) |
                (sample.vout_ma!=UINT16_MAX ? GUI_PDU_VOUT_CURR_VALID : 0))
        };
        return 0;
    }
    pthread_mutex_lock(&g_pdu_io_lock);
    int fd=open_pdu(), unit=pdu_unit(), ok=-1;
    if (fd>=0) {
        uint16_t input[3], vout[1], trans[1], identity[2];
        if (unit>0 && (!set ||
            (modbus_read(fd,(uint8_t)unit,3,99,2,identity)==0 &&
             identity[0]==0x5044 && identity[1]==0x5500 &&
             modbus_write_single(fd,(uint8_t)unit,0,target_mv)==0)) &&
            modbus_read(fd,(uint8_t)unit,4,22,3,input)==0 &&
            modbus_read(fd,(uint8_t)unit,1,0,1,vout)==0 &&
            modbus_read(fd,(uint8_t)unit,2,10,1,trans)==0) {
            result->setpoint_mv=input[0];
            result->voltage_mv=input[1];
            result->current_ma=input[2];
            result->flags=(uint16_t)((trans[0] ? GUI_PDU_VOUT_TRANS_ON : 0U) |
                GUI_PDU_VOUT_STATE_VALID | (vout[0] ? GUI_PDU_VOUT_ENABLED : 0U) |
                (input[0]!=0xffff ? GUI_PDU_VOUT_SET_VALID : 0U) |
                (input[1]!=0xffff ? GUI_PDU_VOUT_VOLT_VALID : 0U) |
                (input[2]!=0xffff ? GUI_PDU_VOUT_CURR_VALID : 0U));
            ok=0;
        }
        close(fd);
    }
    pthread_mutex_unlock(&g_pdu_io_lock);
    if (set) {
        pdu_t sample;
        if (ok == 0 && poll_pdu(&sample) == 0) store_pdu(&sample);
        else { memset(&sample, 0, sizeof(sample)); store_pdu(&sample); }
    }
    return ok;
}
int gui_pdu_power(uint16_t select, uint16_t on, uint16_t *state,
                  uint16_t *known, uint16_t *failed)
{
    if (!state || !known || !failed || (select & ~3U) || (on & ~select) ||
        ((select & 3U)==3U && (on & 3U)==1U)) return -1;
    *state=*known=*failed=0;
    if (!select) {
        pdu_t sample;
        if (cached_pdu(&sample) != 0) return -1;
        *known = ((sample.valid&PDU_VALID_VOUT) ? 1U : 0U) |
                 ((sample.valid&PDU_VALID_TRANS) ? 2U : 0U);
        *state = (uint16_t)((sample.vout ? 1U : 0U) |
                            (sample.trans ? 2U : 0U));
        return 0;
    }
    pthread_mutex_lock(&g_pdu_io_lock);
    int fd=open_pdu(), unit=pdu_unit();
    if (fd<0 || unit<0) {
        if (fd>=0) close(fd);
        pthread_mutex_unlock(&g_pdu_io_lock);
        return -1;
    }
    uint16_t identity[2];
    bool valid=modbus_read(fd,(uint8_t)unit,3,99,2,identity)==0 &&
        identity[0]==0x5044 && identity[1]==0x5500;
    if (valid && select) {
        /* First energize TRANS, then handle VOUT, then de-energize TRANS. */
        if ((select & 2U) && (on & 2U) &&
            modbus_write_coil(fd,(uint8_t)unit,1,true)!=0) *failed|=2U;
        if ((select & 1U) && !(*failed & 2U) &&
            modbus_write_coil(fd,(uint8_t)unit,0,(on & 1U)!=0)!=0) *failed|=1U;
        else if ((select & 1U) && (*failed & 2U)) *failed|=1U;
        if ((select & 2U) && !(on & 2U) &&
            modbus_write_coil(fd,(uint8_t)unit,1,false)!=0) *failed|=2U;
    }
    close(fd);
    pthread_mutex_unlock(&g_pdu_io_lock);
    pdu_t sample;
    if (valid && poll_pdu(&sample) == 0) {
        store_pdu(&sample);
        *known=((sample.valid&PDU_VALID_VOUT) ? 1U : 0U) |
               ((sample.valid&PDU_VALID_TRANS) ? 2U : 0U);
        *state=(sample.vout ? 1U : 0U)|(sample.trans ? 2U : 0U);
        /* Confirmed TRANS OFF physically guarantees VOUT de-energized after an OFF request. */
        if ((select&1U) && !(on&1U) && (sample.valid&PDU_VALID_TRANS) && !sample.trans)
            *failed&=(uint16_t)~1U;
    } else { memset(&sample,0,sizeof(sample));store_pdu(&sample); }
    if (!valid) return -1;
    uint16_t unconfirmed=(uint16_t)(select & ~*known);
    if ((select&1U) && !(on&1U) && (*known&2U) && !(*state&2U)) unconfirmed&=(uint16_t)~1U;
    *failed|=unconfirmed;
    *failed|=(uint16_t)(select & *known & (*state ^ on));
    return *known ? 0 : -1;
}

int gui_pdu_fault_clear(void)
{
    pthread_mutex_lock(&g_pdu_io_lock);
    int fd = open_pdu(), unit = pdu_unit();
    if (fd < 0 || unit < 0) {
        if (fd >= 0) close(fd);
        pthread_mutex_unlock(&g_pdu_io_lock);
        return -1;
    }
    uint16_t identity[2], latch[1];
    int ok = modbus_read(fd, (uint8_t)unit, 3, 99, 2, identity) == 0 &&
        identity[0] == 0x5044 && identity[1] == 0x5500 &&
        modbus_write_coil(fd, (uint8_t)unit, 2, true) == 0 &&
        modbus_read(fd, (uint8_t)unit, 1, 2, 1, latch) == 0 && latch[0] == 0;
    close(fd);
    pthread_mutex_unlock(&g_pdu_io_lock);
    return ok ? 0 : -1;
}

int gui_pdu_heartbeat(uint8_t action, uint32_t period_ms,
                      uint8_t *enabled, uint32_t *on_ms, uint32_t *off_ms)
{
    if (!enabled || !on_ms || !off_ms || action > 3 ||
        ((action == 2 || action == 3) && (period_ms < 1 || period_ms > 60000))) return -1;
    pthread_mutex_lock(&g_pdu_io_lock);
    int fd=open_pdu(), unit=pdu_unit();
    if (fd<0 || unit<0) {
        if (fd>=0) close(fd);
        pthread_mutex_unlock(&g_pdu_io_lock);
        return -1;
    }
    uint16_t identity[2], coil[1], periods[4];
    int ok=modbus_read(fd,(uint8_t)unit,3,99,2,identity)==0 &&
        identity[0]==0x5044 && identity[1]==0x5500;
    if (ok && action==1)
        ok=modbus_write_coil(fd,(uint8_t)unit,9,period_ms!=0)==0;
    if (ok && (action==2 || action==3))
        ok=modbus_write_pair(fd,(uint8_t)unit,action==2 ? 19 : 21,
                             0,(uint16_t)period_ms)==0;
    if (ok) ok=modbus_read(fd,(uint8_t)unit,1,9,1,coil)==0 &&
               modbus_read(fd,(uint8_t)unit,3,19,4,periods)==0;
    close(fd);
    pthread_mutex_unlock(&g_pdu_io_lock);
    if (!ok) return -1;
    *enabled=(uint8_t)(coil[0]!=0);
    *on_ms=((uint32_t)periods[0]<<16)|periods[1];
    *off_ms=((uint32_t)periods[2]<<16)|periods[3];
    if ((action==1 && *enabled!=(uint8_t)(period_ms!=0)) ||
        (action==2 && *on_ms!=period_ms) ||
        (action==3 && *off_ms!=period_ms)) return -1;
    return 0;
}

int gui_pdu_enter_bootloader(void)
{
    pthread_mutex_lock(&g_pdu_io_lock);
    int fd=open_pdu(), unit=pdu_unit();
    if (fd<0 || unit<0) {
        if (fd>=0) close(fd);
        pthread_mutex_unlock(&g_pdu_io_lock);
        return -1;
    }
    uint16_t identity[2];
    int ok=modbus_read(fd,(uint8_t)unit,3,99,2,identity)==0 &&
        identity[0]==0x5044 && identity[1]==0x5500 &&
        modbus_write_single(fd,(uint8_t)unit,59,0xB007)==0;
    close(fd);
    pthread_mutex_unlock(&g_pdu_io_lock);
    return ok ? 0 : -1;
}
static int poll_pdu(pdu_t *result)
{
    memset(result,0,sizeof(*result));
    result->temp_tenth=(int16_t)0x8000;
    result->set_mv=result->vout_mv=result->vout_ma=result->trans_mv=
        result->trans_ma=result->in12_mv=result->in5_mv=UINT16_MAX;
    pthread_mutex_lock(&g_pdu_io_lock);
    int fd=open_pdu(), unit=pdu_unit();
    if (fd<0 || unit<0) {if (fd>=0) close(fd);pthread_mutex_unlock(&g_pdu_io_lock);return -1;}
    uint16_t input[12],trans[1],vout[1];
    bool got_input=modbus_read(fd,unit,4,19,12,input)==0;
    if (got_input) {
        result->valid=PDU_VALID_FLAGS|PDU_VALID_MODE|PDU_VALID_TRANS|PDU_VALID_RAILS;
        result->flags=input[0];result->mode=input[2];
        result->trans=(input[0]&2U)!=0;
        result->temp_tenth=(int16_t)input[1];
        if (input[1]!=0x8000) result->valid|=PDU_VALID_TEMP;
        result->set_mv=input[3];
        if (input[3]!=UINT16_MAX) result->valid|=PDU_VALID_SETPOINT;
        result->vout_mv=input[4];result->vout_ma=input[5];
        result->in12_mv=input[6];result->trans_mv=input[8];
        result->trans_ma=input[9];result->in5_mv=input[10];
    }
    /* TRANS feedback does not require access to the unpowered TPS. */
    if (modbus_read(fd,unit,2,10,1,trans)==0) {
        result->trans=trans[0]!=0;result->valid|=PDU_VALID_TRANS;
    }
    uint64_t now=monotonic_ms();
    if (result->valid&PDU_VALID_TRANS) {
        if (!result->trans) {
            result->tps_state=PDU_TPS_UNPOWERED;
            result->valid&=(uint16_t)~PDU_VALID_SETPOINT;
            result->set_mv=UINT16_MAX;
            tps_was_on=false;
            /* No FC01 request to coil 1 while TPS is unpowered. */
        } else {
            if (!tps_was_on) {tps_started_ms=now;tps_was_on=true;}
            if (modbus_read(fd,unit,1,0,1,vout)==0) {
                result->vout=vout[0]!=0;result->valid|=PDU_VALID_VOUT;
                result->tps_state=PDU_TPS_READY;
            } else result->tps_state=now-tps_started_ms<2000U ?
                PDU_TPS_STARTING : PDU_TPS_COMM_ERROR;
        }
    } else tps_was_on=false;
    if(result->tps_state!=PDU_TPS_READY) {
        result->valid&=(uint16_t)~PDU_VALID_SETPOINT;result->set_mv=UINT16_MAX;
    }
    close(fd);pthread_mutex_unlock(&g_pdu_io_lock);
    if (!result->valid) return -1;
    result->online=true;result->stamp=time(NULL);
    return 0;
}

int gui_pdu_status(gui_pdu_status_t *result)
{
    if (!result) return -1;
    pdu_t sample={0};
    if (cached_pdu(&sample)!=0) return -1;
    *result=(gui_pdu_status_t){
        .valid=sample.valid, .status_flags=(uint16_t)sample.flags,
        .mode=(uint8_t)sample.mode, .tps_state=sample.tps_state,
        .age_ms=(uint32_t)(time(NULL)-sample.stamp)*1000U,
        .setpoint_mv=(uint16_t)sample.set_mv,
        .vout_mv=(uint16_t)sample.vout_mv, .vout_ma=(uint16_t)sample.vout_ma,
        .trans_mv=(uint16_t)sample.trans_mv, .trans_ma=(uint16_t)sample.trans_ma,
        .input_12v_mv=(uint16_t)sample.in12_mv, .input_5v_mv=(uint16_t)sample.in5_mv,
        .temp_tenth=(int16_t)sample.temp_tenth,
        .vout_on=sample.vout, .trans_on=sample.trans
    };
    return 0;
}
static void *pdu_thread(void *unused)
{
    (void)unused;
    unsigned cycle = 0;
    while (atomic_load(&g_pdu_running)) {
        pdu_t next={0};
        if (poll_pdu(&next)!=0) next.online=false;
        store_pdu(&next);
        if (cycle++ % 12 == 0) {
            uint8_t version[3]; uint32_t build;
            int ok = poll_pdu_version(version, &build);
            pthread_mutex_lock(&g_pdu_lock);
            g_pdu_version_stamp = ok == 0 ? time(NULL) : 0;
            if (ok == 0) { memcpy(g_pdu_version, version, 3); g_pdu_build = build; }
            pthread_mutex_unlock(&g_pdu_lock);
        }
        for (int i=0;i<50 && atomic_load(&g_pdu_running);i++) usleep(100000);
    }
    return NULL;
}
void gui_state_start(void)
{
    if (atomic_load(&g_pdu_running)) return;
    atomic_store(&g_pdu_running,true);
    if (pthread_create(&g_pdu_thread,NULL,pdu_thread,NULL)!=0) atomic_store(&g_pdu_running,false);
    exp_uart_service_start();
}
void gui_state_stop(void)
{
    exp_uart_service_stop();
    if (!atomic_load(&g_pdu_running)) return;
    atomic_store(&g_pdu_running,false);pthread_join(g_pdu_thread,NULL);
}
static void gpio(char *out,size_t cap,size_t *used,const char *name,bool comma)
{
    char path[160];snprintf(path,sizeof(path),"/sys/class/leds/%s/brightness",name);
    int value;
    add(out,cap,used,"%s\"%s\":",comma?",":"",name);
    if (read_int(path,&value)==0) add(out,cap,used,value?"true":"false");
    else add(out,cap,used,"null");
}
size_t gui_state_json(char *out, size_t capacity)
{
    if (!out || capacity<256) return 0;
    size_t used=0;out[0]=0;
    add(out,capacity,&used,"{\"ok\":true,\"obc\":");
    bee_dal_telemetry_t tm;hk_service_get_telemetry(&tm);
    if (tm.obc_snapshot_valid)
        raw_file(out,capacity,&used,setting("DAL_OBC_HK_SNAPSHOT_PATH",DAL_OBC_HK_SNAPSHOT_PATH));
    else add(out,capacity,&used,"null");
    add(out,capacity,&used,",\"exp\":");
    raw_file(out,capacity,&used,setting("DAL_EXP_HK_SNAPSHOT_PATH",EXP_PATH));
    char exp_uart[50000];
    if (exp_uart_snapshot(exp_uart,sizeof(exp_uart)))
        add(out,capacity,&used,",\"exp_uart\":%s",exp_uart);
    else add(out,capacity,&used,",\"exp_uart\":null");
    pdu_t pdu;pthread_mutex_lock(&g_pdu_lock);pdu=g_pdu;pthread_mutex_unlock(&g_pdu_lock);
    add(out,capacity,&used,",\"pdu\":");
    if (pdu.online && time(NULL)-pdu.stamp<=15) {
        const char *modes[]={"NORMAL","MANUAL","WARNING","ERROR"};
        add(out,capacity,&used,"{\"board\":\"pdu\",\"schema_version\":1,\"obc_timestamp\":%ld,",(long)pdu.stamp);
        if (!(pdu.valid&PDU_VALID_TEMP)) add(out,capacity,&used,"\"board_temp_c\":null,");
        else add(out,capacity,&used,"\"board_temp_c\":%.1f,",pdu.temp_tenth/10.0);
        add(out,capacity,&used,"\"valid_mask\":%u,\"tps_state\":%u,\"mode\":\"%s\",\"status_flags\":%u,\"vout_on\":%s,\"trans_on\":%s,",
            pdu.valid,pdu.tps_state,(pdu.valid&PDU_VALID_MODE) && pdu.mode<4?modes[pdu.mode]:"UNKNOWN",pdu.flags,(pdu.valid&PDU_VALID_VOUT) ? (pdu.vout?"true":"false") : "null",(pdu.valid&PDU_VALID_TRANS) ? (pdu.trans?"true":"false") : "null");
        add(out,capacity,&used,"\"vout_set_mv\":%u,\"vout_mv\":%u,\"vout_ma\":%u,\"trans_mv\":%u,\"trans_ma\":%u,\"input_12v_mv\":%u,\"input_5v_mv\":%u}",
            pdu.set_mv,pdu.vout_mv,pdu.vout_ma,pdu.trans_mv,pdu.trans_ma,pdu.in12_mv,pdu.in5_mv);
    } else add(out,capacity,&used,"null");
    static const char *names[]={"pow_onoff_ai","pow_onoff_fpga","usb_1_pwr_en","usb_2_pwr_en",
        "usb_3_pwr_en","usb_4_pwr_en","pow_mipicam_1","pow_mipicam_2",
        "en_i2c_mipicam_1","en_i2c_mipicam_2","sel_mipicam_on2_off1"};
    add(out,capacity,&used,",\"gpio\":{");
    for (size_t i=0;i<sizeof(names)/sizeof(names[0]);i++) gpio(out,capacity,&used,names[i],i>0);
    add(out,capacity,&used,"},\"devices\":{");
    static const char *device_keys[]={"i2c0","spi_cat93","spi_s25","can0","can1"};
    static const char *device_paths[]={"/dev/i2c-0","/dev/spidev0.0","/dev/spidev2.0",
                                       "/sys/class/net/can0","/sys/class/net/can1"};
    for (size_t i=0;i<5;i++) add(out,capacity,&used,"%s\"%s\":%s",i?",":"",device_keys[i],
                                access(device_paths[i],F_OK)==0?"true":"false");
    add(out,capacity,&used,"},\"rtc\":");
    int fd=open(DAL_OBC_RTC_DEVICE,O_RDONLY|O_CLOEXEC);
    struct rtc_time rtc;
    if (fd>=0 && ioctl(fd,RTC_RD_TIME,&rtc)==0)
        add(out,capacity,&used,"\"%04d-%02d-%02d %02d:%02d:%02d UTC\"",rtc.tm_year+1900,rtc.tm_mon+1,rtc.tm_mday,rtc.tm_hour,rtc.tm_min,rtc.tm_sec);
    else add(out,capacity,&used,"\"unavailable (/dev/rtc2)\"");
    if (fd>=0) close(fd);
    int lane,sensor,select,duty,period,enabled;
    const char *lane_path=setting("DAL_GUI_MIPI_LANE","/sys/bus/i2c/devices/1-0070/lane_switch/current_lane");
    const char *sensor_path=setting("DAL_GUI_MIPI_SENSOR","/sys/bus/i2c/devices/1-0020/sensor_switch/current_sensor");
    add(out,capacity,&used,",\"mipi\":{");
    if (read_int("/sys/class/leds/sel_mipicam_on2_off1/brightness",&select)==0)
        add(out,capacity,&used,"\"board\":%d,",select?2:1);
    else add(out,capacity,&used,"\"board\":null,");
    if (read_int(lane_path,&lane)==0) add(out,capacity,&used,"\"lane\":%d,",lane);
    else add(out,capacity,&used,"\"lane\":null,");
    if (read_int(sensor_path,&sensor)==0) add(out,capacity,&used,"\"sensor\":%d},",sensor);
    else add(out,capacity,&used,"\"sensor\":null},");
    const char *pwm=setting("DAL_GUI_CAMERA_LED_PWM",PWM_PATH);
    char path[200];snprintf(path,sizeof(path),"%s/period",pwm);
    int have_period=read_int(path,&period)==0;
    snprintf(path,sizeof(path),"%s/duty_cycle",pwm);int have_duty=read_int(path,&duty)==0;
    snprintf(path,sizeof(path),"%s/enable",pwm);int have_enable=read_int(path,&enabled)==0;
    if (have_enable && !enabled) add(out,capacity,&used,"\"camera_led_duty_pct\":0}");
    else if (have_period && have_duty && have_enable && period>0)
        add(out,capacity,&used,"\"camera_led_duty_pct\":%d}",duty*100/period);
    else add(out,capacity,&used,"\"camera_led_duty_pct\":null}");
    add(out,capacity,&used,"\n");
    return used;
}
