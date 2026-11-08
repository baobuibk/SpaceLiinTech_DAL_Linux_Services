#include "services/tm_payload.h"
#include "services/hk_service.h"
#include "services/sensor_stat.h"
#include "services/exp_csp_service.h"
#include "bee_dal_proto.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static bool available=true,sensors_ok=true,overflow=false;
void hk_service_get_telemetry(bee_dal_telemetry_t *t) {memset(t,0,sizeof(*t));t->obc_snapshot_valid=available;t->bootcount=42;}
int exp_csp_icd_status(unsigned group,uint8_t *p,size_t cap) {
    if(!sensors_ok)return -1;
    memset(p,0,cap);
    if(group==2) {p[4]=2;p[5]=4;}
    else {p[0]=0x80;p[5]=0x81;}
    return 0;
}
static void record(uint8_t *p,size_t *off,unsigned source,unsigned channel,int value) {
    p[(*off)++]=source;p[(*off)++]=channel;p[(*off)++]=1;p[(*off)++]=0;
    bee_dal_put_u32(p,off,0);bee_dal_put_u32(p,off,(uint32_t)value);
}
int sensor_stat_read(uint8_t board,uint8_t group,uint8_t source,uint8_t cursor,uint8_t *p,size_t capacity,size_t *n) {
    (void)source;(void)cursor;assert(capacity>=106);memset(p,0,capacity);size_t off=10;
    if(sensors_ok) {
        if(group==1) {
            record(p,&off,1,0,board==5?2510:board==4?-1234:3025);
            if(board==4)for(unsigned c=1;c<=4;c++)record(p,&off,2,c,1000*c);
        } else if(group==2) {
            record(p,&off,3,1,2222);record(p,&off,3,2,overflow?700000:101325);record(p,&off,3,3,4850);
        } else if(group==3)for(unsigned c=1;c<=7;c++)record(p,&off,4,c,c>=5?-123456:c*100);
        else for(unsigned src=5;src<=6;src++) {
            record(p,&off,src,1,overflow?4000000:src==5?-12345:23456);
            record(p,&off,src,2,src==5?2200:2300);
        }
    }
    p[4]=(off-10)/12;*n=off;return 0;
}
static unsigned word(const uint8_t *p,unsigned at){return p[at]|p[at+1]<<8;}
int main(void) {
    uint8_t p[59];memset(p,0xa5,sizeof(p));assert(tm_get_encode(p)==0);
    assert(p[57]==0xa5 && p[58]==0xa5);assert(p[4]==1);
    size_t off=0;uint32_t epoch=bee_dal_get_u32(p,&off);assert(epoch>=(uint32_t)time(NULL)-1);
    assert(word(p,5)==42);assert((int16_t)word(p,9)==2510);assert((int16_t)word(p,11)==-1234);assert(word(p,13)==3025);
    for(unsigned i=0;i<4;i++)assert(word(p,15+2*i)==1000*(i+1));
    assert(word(p,23)==0x8000);assert(word(p,25)==2222);assert(word(p,27)==10132);assert(word(p,29)==4850);
    assert(word(p,31)==100 && word(p,37)==400);assert((int16_t)word(p,39)==-123);
    assert(word(p,45)==2200 && (int16_t)word(p,47)==-123);assert(word(p,49)==2300 && word(p,51)==234);
    assert(word(p,53)==0x0402 && word(p,55)==0x21);
    overflow=true;assert(!tm_get_encode(p));assert(word(p,27)==0xffff && word(p,47)==0x8000);
    sensors_ok=false;assert(!tm_get_encode(p));
    for(unsigned at=9;at<57;at+=2) assert(word(p,at)==(at==27 || at==29 || at>=53?0xffff:0x8000));
    available=false;assert(tm_get_encode(p)==-1);
    puts("tm_get v1.2: offsets, scaling, signed values, sentinels and bounds OK");
}
