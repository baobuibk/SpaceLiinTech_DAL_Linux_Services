#include "services/sensor_cfg.h"
#include "services/exp_uart_service.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Only SENSOR_READBACK / effective configuration is eligible for valid bits.
 * Missing devices leave their portion zero; defaults are never called applied. */
static int os_code(unsigned value) {
    static const unsigned values[]={0,1,2,4,8,16};
    for (unsigned i=0;i<6;i++) if (values[i]==value) return (int)i;
    return -1;
}
static int filter_code(unsigned value) {
    static const unsigned values[]={0,1,3,7,15,31,63,127};
    for (unsigned i=0;i<8;i++) if (values[i]==value) return (int)i;
    return -1;
}
static void read_config(uint8_t out[8]) {
    char text[4096];unsigned t,p,h,f;
    memset(out,0,8);
    if (!exp_uart_sensor_command("bme688_cfg_get",text,sizeof(text))) {
        const char *s=strstr(text," t_os=");
        if (strstr(text,"source=SENSOR_READBACK") && s &&
            sscanf(s," t_os=%ux p_os=%ux h_os=%ux filter=%u",&t,&p,&h,&f)==4) {
            int tc=os_code(t),pc=os_code(p),hc=os_code(h),fc=filter_code(f);
            if (tc>=0 && pc>=0 && hc>=0 && fc>=0) {
                out[0]=(uint8_t)(tc | pc<<3 | 0x40);out[1]=(uint8_t)(hc | fc<<3);
            }
        }
    }
    if (!exp_uart_sensor_command("lsm6d_conf_get",text,sizeof(text))) {
        unsigned a,as,g,gs,mode,event,wu,wd,ff;
        const char *effective=strstr(text,"effective xl=");
        const char *regs=strstr(text,"WAKE_THS=");
        if (strstr(text,"config_valid=1") && effective && regs &&
            sscanf(effective,"effective xl=%u/%u gyro=%u/%u mode=%u event=%u",&a,&as,&g,&gs,&mode,&event)==6 &&
            sscanf(regs,"WAKE_THS=0x%x WAKE_DUR=0x%x FREE_FALL=0x%x",&wu,&wd,&ff)==3 &&
            a<=7 && as<=3 && g<=7 && gs<=4 && mode==0 && event==0 && wu<=255 && wd<=255 && ff<=255) {
            out[2]=(uint8_t)(a | as<<3 | g<<5);out[3]=(uint8_t)gs;
            out[4]=(uint8_t)((ff>>3) | ((wd>>7)&1)<<5 | ((wd>>5)&3)<<6);
            out[5]=(uint8_t)(wu&63);out[6]=(uint8_t)((ff&7)|0x20);
        }
    }
}
static int apply(const char *command, int bme) {
    char text[4096],query[64];unsigned long id=0;
    if (exp_uart_sensor_command(command,text,sizeof(text))) return -1;
    const char *queued=strstr(text,"QUEUED id=");
    if (!queued || sscanf(queued,"QUEUED id=%lu",&id)!=1 || !id || id>UINT32_MAX) return -1;
    snprintf(query,sizeof(query),"%s %lu",bme ? "bme688_result" : "lsm6d_result",id);
    struct timespec start;clock_gettime(CLOCK_MONOTONIC,&start);
    /* MCU requests are asynchronous. Never acknowledge QUEUED as applied. */
    for (unsigned i=0;i<20;i++) {
        struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
        if ((now.tv_sec-start.tv_sec)*1000+(now.tv_nsec-start.tv_nsec)/1000000>=3000) return -1;
        const struct timespec pause={0,100000000};nanosleep(&pause,NULL);
        if (exp_uart_sensor_command(query,text,sizeof(text))) return -1;
        char done[64];snprintf(done,sizeof(done)," DONE id=%lu",id);
        const char *completed=strstr(text,done);
        if (completed && (completed[strlen(done)]=='\0' || completed[strlen(done)]==' ' ||
                          completed[strlen(done)]=='\r' || completed[strlen(done)]=='\n')) return 0;
        if (!strstr(text," QUEUED") && !strstr(text," ACCEPTED")) return -1;
    }
    return -1;
}
int sensor_cfg_execute(const uint8_t *r,size_t length,uint8_t out[8]) {
    char command[128];
    if (length!=0 && length!=8) return -2;
    if (length) {
        if (r[6] || r[7]) return -2;
        if (r[0]==1) {
            if (r[1] || !r[2] || !r[3] || !r[4] || r[2]>5 || r[3]>5 || r[4]>5 || r[5]>7) return -2;
            /* Current MCU CLI takes physical coefficients, not enum codes.
             * Preserve its stored-only heater profile; zero is not valid. */
            char text[4096];unsigned heater,duration;
            if (exp_uart_sensor_command("bme688_cfg_get",text,sizeof(text))) return -1;
            const char *profile=strstr(text,"heater_temp_c=");
            if (!strstr(text,"source=SENSOR_READBACK") || !profile ||
                sscanf(profile,"heater_temp_c=%u heater_duration_ms=%u",&heater,&duration)!=2 ||
                heater<200 || heater>400 || !duration || duration>4032) return -1;
            static const unsigned os[]={0,1,2,4,8,16},filter[]={0,1,3,7,15,31,63,127};
            snprintf(command,sizeof(command),"bme688_cfg_set %u %u %u %u 0 %u %u",
                     os[r[2]],os[r[3]],os[r[4]],filter[r[5]],heater,duration);
        } else if (r[0]==2) {
            if (r[4] || r[5] || r[1]>3) return -2;
            static const char *ops[]={"accel","gyro","ff","wu"};
            static const uint8_t max_a[]={7,7,7,63},max_b[]={3,4,63,3};
            if (r[2]>max_a[r[1]] || r[3]>max_b[r[1]]) return -2;
            snprintf(command,sizeof(command),"lsm6d_conf_%s %u %u",ops[r[1]],r[2],r[3]);
        } else return -2; /* one sensor only; no combined mask */
        if (apply(command,r[0]==1)) return -1;
    }
    read_config(out);
    if (length && ((r[0]==1 && !(out[0]&0x40)) || (r[0]==2 && !(out[6]&0x20)))) return -1;
    if (length) {
        if (r[0]==1) {
            if ((out[0]&7)!=r[2] || ((out[0]>>3)&7)!=r[3] ||
                (out[1]&7)!=r[4] || ((out[1]>>3)&7)!=r[5]) return -1;
        } else {
            uint8_t a[]={out[2]&7,out[2]>>5,out[6]&7,out[5]&63};
            uint8_t b[]={(out[2]>>3)&3,out[3]&7,out[4]&63,out[4]>>6};
            if (a[r[1]]!=r[2] || b[r[1]]!=r[3]) return -1;
        }
    }
    return 0;
}
