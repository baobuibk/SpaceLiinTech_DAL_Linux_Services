#include "services/sensor_cfg.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static unsigned calls,sets;
static int unavailable,defaults,failed,wrong;
static unsigned b[4]={2,3,1,2},imu[8]={5,3,5,3,3,20,20,2};
uint8_t exp_uart_sensor_command(const char *cmd,char *out,size_t cap) {
    calls++;
    if (unavailable) return 2;
    if (!strcmp(cmd,"bme688_cfg_get")) {
        unsigned os[]={0,1,2,4,8,16},fc[]={0,1,3,7,15,31,63,127};
        snprintf(out,cap,"BME688 CFG source=%s heater_profile=STORED_ONLY gas_support=0 t_os=%ux p_os=%ux h_os=%ux filter=%u gas=0 heater_temp_c=320 heater_duration_ms=100",defaults ? "COMPILED_DEFAULT" : "SENSOR_READBACK",os[b[0]],os[b[1]],os[b[2]],fc[b[3]]);
    } else if (!strcmp(cmd,"lsm6d_conf_get")) {
        snprintf(out,cap,"config_valid=%d\r\nWAKE_THS=0x%02X WAKE_DUR=0x%02X FREE_FALL=0x%02X\r\ndesired xl=0/0 gyro=0/0 mode=0 event=0; effective xl=%u/%u gyro=%u/%u mode=0 event=0 last_error=0",!defaults,imu[6],imu[7]<<5|(imu[5]&32)<<2,(imu[5]&31)<<3|imu[4],imu[0],imu[1],imu[2],imu[3]);
    } else if (!strncmp(cmd,"bme688_cfg_set",14)) {
        sets++;
        unsigned v[4],heater,duration;assert(sscanf(cmd,"bme688_cfg_set %u %u %u %u 0 %u %u",&v[0],&v[1],&v[2],&v[3],&heater,&duration)==6);
        assert(heater==320 && duration==100);
        unsigned os[]={0,1,2,4,8,16},fc[]={0,1,3,7,15,31,63,127};
        if (!wrong) {for(int i=0;i<3;i++) for(int j=1;j<6;j++) if(v[i]==os[j]) b[i]=j;
                    for(int j=0;j<8;j++) if(v[3]==fc[j]) b[3]=j;}
        snprintf(out,cap,"BME688 CFG QUEUED id=17");
    } else if (!strncmp(cmd,"lsm6d_conf_",11)) {
        char op[32];unsigned a,c;assert(sscanf(cmd,"lsm6d_conf_%31s %u %u",op,&a,&c)==3);sets++;
        unsigned i=!strcmp(op,"accel")?0:!strcmp(op,"gyro")?2:!strcmp(op,"ff")?4:6;
        if (!wrong) {imu[i]=a;imu[i+1]=c;}
        snprintf(out,cap,"LSM6DSOX QUEUED id=18");
    } else snprintf(out,cap,"RESULT %s id=%d",failed ? "FAILED" : "DONE",strstr(cmd,"lsm6d")?18:17);
    return 0;
}
int main(void) {
    uint8_t out[8],r[8]={0};
    assert(sensor_cfg_execute(NULL,0,out)==0);
    const uint8_t expected[]={0x5a,0x11,0xbd,3,0x94,20,0x23,0};
    assert(!memcmp(out,expected,8));
    defaults=1;assert(!sensor_cfg_execute(NULL,0,out));assert(!out[0] && !out[6]);defaults=0;
    unavailable=1;assert(!sensor_cfg_execute(NULL,0,out));for(int i=0;i<8;i++) assert(!out[i]);unavailable=0;
    unsigned before=calls;
    assert(sensor_cfg_execute(r,1,out)==-2);
    r[0]=3;assert(sensor_cfg_execute(r,8,out)==-2);assert(calls==before);
    r[0]=1;r[2]=5;r[3]=4;r[4]=3;r[5]=7;
    assert(!sensor_cfg_execute(r,8,out));assert((out[0]&7)==5 && ((out[1]>>3)&7)==7);
    r[0]=2;r[1]=1;r[2]=4;r[3]=4;r[4]=r[5]=0;
    assert(!sensor_cfg_execute(r,8,out));assert(out[2]>>5==4 && out[3]==4);
    r[1]=2;r[2]=7;r[3]=63;assert(!sensor_cfg_execute(r,8,out));assert((out[4]&63)==63 && (out[6]&7)==7);
    r[1]=3;r[2]=63;r[3]=3;assert(!sensor_cfg_execute(r,8,out));assert(out[5]==63 && out[4]>>6==3);
    r[1]=0;r[2]=3;r[3]=2;failed=1;assert(sensor_cfg_execute(r,8,out)==-1);failed=0;
    wrong=1;r[2]=1;assert(sensor_cfg_execute(r,8,out)==-1);wrong=0;
    before=calls;r[7]=1;assert(sensor_cfg_execute(r,8,out)==-2);assert(calls==before);
    assert(sets==6);puts("sensor_cfg tests passed");
}
