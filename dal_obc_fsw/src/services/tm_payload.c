#include "services/tm_payload.h"
#include "services/hk_service.h"
#include "services/sensor_stat.h"
#include "services/exp_csp_service.h"
#include "bee_dal_proto.h"
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

static void put16(uint8_t *p, unsigned at, int32_t value, bool unsign)
{
    size_t off=at;
    if (unsign) bee_dal_put_u16(p,&off,value>=0 && value<UINT16_MAX ? (uint16_t)value : UINT16_MAX);
    else bee_dal_put_i16(p,&off,value>INT16_MIN && value<=INT16_MAX ? (int16_t)value : INT16_MIN);
}

/* sensor_stat uses the same cached samples/validity checks as native sen_stat.
 * No hardware transactions are performed by these reads. */
static void sensors(uint8_t *p, uint8_t board, uint8_t group)
{
    uint8_t raw[SENSOR_STAT_MAX_PAYLOAD];size_t n=0;
    if(sensor_stat_read(board,group,0,0,raw,sizeof(raw),&n)!=0 || n<10 ||
       n!=10U+12U*raw[4] || raw[3]) return;
    for(unsigned i=0;i<raw[4];i++) {
        const uint8_t *r=raw+10+12*i;
        if(!(r[2]&1)) continue;
        size_t off=8;int32_t v=(int32_t)bee_dal_get_u32(r,&off);
        unsigned source=r[0],channel=r[1],at=UINT_MAX;
        bool unsign=false;
        if(group==1) {
            if(source==1 && channel==0) at=board==5 ? 9 : board==4 ? 11 : 13;
            else if(board==4 && source==2 && channel>=1 && channel<=4) at=15+2*(channel-1);
        } else if(board==4 && group==2 && source==3 && channel>=1 && channel<=3) {
            at=25+2*(channel-1);unsign=channel!=1;
            if(channel==2) v/=10; /* Pa -> 10 Pa */
        } else if(board==4 && group==3 && source==4 && channel>=1 && channel<=7) {
            at=31+2*(channel-1);
            if(channel>=5) v/=1000; /* mdps -> dps */
        } else if(board==4 && group==4 && (source==5 || source==6)) {
            if(channel==2) at=45+4*(source-5); /* temperature cC */
            else if(channel==1) {at=47+4*(source-5);v/=100;} /* centi uL/min -> uL/min */
        }
        if(at!=UINT_MAX) put16(p,at,v,unsign);
    }
}

int tm_get_encode(uint8_t p[TM_GET_V12_LEN])
{
    bee_dal_telemetry_t obc;hk_service_get_telemetry(&obc);
    if(!p || !obc.obc_snapshot_valid) return -1;
    memset(p,0,TM_GET_V12_LEN);
    for(unsigned at=9;at<57;at+=2) put16(p,at,INT32_MIN,false);
    for(unsigned at=27;at<=29;at+=2) put16(p,at,-1,true);
    put16(p,53,-1,true);put16(p,55,-1,true);
    time_t now=time(NULL);size_t off=0;
    bee_dal_put_u32(p,&off,now>=0 && (uint64_t)now<UINT32_MAX ? (uint32_t)now : UINT32_MAX);
    p[4]=1; /* Partial until every field has a valid source; MLX average unavailable. */
    off=5;bee_dal_put_u32(p,&off,obc.bootcount);
    sensors(p,5,1);sensors(p,4,1);sensors(p,3,1);
    sensors(p,4,2);sensors(p,4,3);sensors(p,4,4);
    /* No verified full-frame camera average is published by the current cache.
     * Never substitute a maximum/point temperature or trigger a capture. */
    uint8_t rba[16],thm[6];
    if(exp_csp_icd_status(2,rba,sizeof(rba))==0) {
        put16(p,53,(uint16_t)(rba[4]|rba[5]<<8),true);
    }
    if(exp_csp_icd_status(0,thm,sizeof(thm))==0) {
        unsigned running=0;
        for(unsigned i=0;i<6;i++) if(thm[i]&0x80) running|=1U<<i;
        put16(p,55,(int32_t)running,true);
    }
    return 0;
}
