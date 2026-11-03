#include "services/sensor_stat.h"
#include "services/hk_service.h"
#include "services/gui_state.h"
#include "bee_dal_proto.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXP_PATH "/run/dal/dal_obc_fsw/exp_data.json"
#define VALID 0x01U
#define STALE 0x02U
#define ERROR 0x10U
#define UNSUPPORTED 0x20U
#define UNIT_NONE 0U
#define UNIT_CENTIDEG 1U
#define UNIT_PA 2U
#define UNIT_CENTIPCT 3U
#define UNIT_OHM 4U
#define UNIT_MG 5U
#define UNIT_MDPS 6U
#define UNIT_CENTIFLOW 7U
#define UNIT_BOOL 8U
#define UNIT_COUNT 9U

typedef struct { uint8_t group, source, channel, unit; } item_t;
typedef struct { uint8_t flags; uint32_t age; int32_t value; } sample_t;
#define R(g,s,c,u) {g,s,c,u}
static const item_t obc_items[]={
 R(1,1,0,1),R(1,2,1,1),R(1,2,2,1),R(1,2,3,1),R(1,2,4,1),
 R(2,3,1,1),R(2,3,2,2),R(2,3,3,3),R(2,3,4,4),
 R(5,4,0,8),R(5,5,0,8),R(6,6,0,0),R(6,7,0,0)
};
static const item_t exp_items[]={
 R(1,1,0,1),R(1,2,1,1),R(1,2,2,1),R(1,2,3,1),R(1,2,4,1),
 R(2,3,1,1),R(2,3,2,2),R(2,3,3,3),R(2,3,4,4),
 R(3,4,1,1),R(3,4,2,5),R(3,4,3,5),R(3,4,4,5),
 R(3,4,5,6),R(3,4,6,6),R(3,4,7,6),
 R(4,5,1,7),R(4,5,2,1),R(4,6,1,7),R(4,6,2,1),
 R(5,7,0,8),R(6,8,0,0),R(6,9,0,0),
 R(7,10,1,8),R(7,10,2,8),R(7,10,3,8),R(7,10,4,8)
};
static const item_t pdu_items[]={R(1,1,0,1),R(5,2,0,8)};
#undef R

static uint32_t age_ms(time_t now,uint32_t epoch) {
    if (!epoch || now<(time_t)epoch) return UINT32_MAX;
    uint64_t v=(uint64_t)(now-(time_t)epoch)*1000U;
    return v>UINT32_MAX ? UINT32_MAX : (uint32_t)v;
}
static int32_t scaled(double value,double factor) {
    value=round(value*factor);
    return isfinite(value) && value>INT32_MIN && value<=INT32_MAX ? (int32_t)value : INT32_MIN;
}
static void set(sample_t *s,double value,double factor,uint32_t age,uint32_t max_age) {
    s->age=age;
    s->value=scaled(value,factor);
    if (s->value==INT32_MIN) s->flags=ERROR;
    else s->flags=age<=max_age ? VALID : STALE;
}
static const char *field(const char *json,const char *key) {
    char pattern[64];
    if (snprintf(pattern,sizeof(pattern),"\"%s\"",key)>=(int)sizeof(pattern)) return NULL;
    const char *p=strstr(json,pattern);
    if (!p || !(p=strchr(p+strlen(pattern),':'))) return NULL;
    do {++p;} while (*p==' ' || *p=='\n' || *p=='\t');
    return p;
}
static int scalar(const char *json,const char *key,double *out) {
    const char *p=field(json,key);char *end;
    if (!p) return -1;
    errno=0;double v=strtod(p,&end);
    if (errno || end==p || !isfinite(v) || (*end!=',' && *end!='\n' && *end!='}')) return -1;
    *out=v;return 0;
}
static int element(const char *json,const char *key,unsigned index,double *out) {
    const char *p=field(json,key);char *end;
    if (!p || *p!='[') return -1;
    ++p;
    for (unsigned i=0;i<=index;i++) {
        while (*p==' ' || *p=='\n') ++p;
        errno=0;double v=strtod(p,&end);
        if (errno || end==p || !isfinite(v)) return -1;
        p=end;while (*p==' ' || *p=='\n') ++p;
        if (i==index) {if (*p!=',' && *p!=']') return -1;*out=v;return 0;}
        if (*p++!=',') return -1;
    }
    return -1;
}
static int read_exp(char json[16384],time_t now,uint32_t *epoch) {
    const char *path=getenv("DAL_EXP_HK_SNAPSHOT_PATH");
    if (!path || !*path) path=EXP_PATH;
    FILE *fp=fopen(path,"r");if (!fp) return -1;
    size_t n=fread(json,1,16383,fp);int extra=fgetc(fp);fclose(fp);json[n]=0;
    double online,valid,ts;
    if (extra!=EOF || scalar(json,"online",&online) || online!=1 ||
        scalar(json,"data_valid",&valid) || valid!=1 ||
        scalar(json,"obc_timestamp",&ts) || ts<=0 || ts>UINT32_MAX) return -1;
    *epoch=(uint32_t)ts;
    return age_ms(now,*epoch)<=30000U ? 0 : -1;
}
static void obc_sample(const item_t *item,sample_t *s,const bee_dal_telemetry_t *tm,time_t now) {
    if (item->group==6) {s->flags=UNSUPPORTED;return;}
    if (item->group==5) {
        s->age=age_ms(now,tm->pac_sample_epoch);
        if (tm->obc_snapshot_valid && tm->pac_valid[item->source-4] && s->age<=30000U) {
            s->flags=VALID;s->value=1;
        } else s->flags=STALE;
        return;
    }
    uint32_t epoch=item->group==2 ? tm->bme_sample_epoch : tm->temp_sample_epoch;
    uint32_t age=age_ms(now,epoch);
    if (!tm->obc_snapshot_valid || (item->group==2 ? !tm->bme_valid : !tm->temp_valid)) {
        s->flags=STALE;s->age=age;return;
    }
    if (item->source==1) set(s,tm->onboard_temp_full_c,100,age,30000);
    else if (item->source==2) {
        /* obc_snapshot maps hk_service's "N/A" thermistor marker to -50 C. */
        if (tm->ntc_c[item->channel-1]<=-50.0f) {s->flags=ERROR;s->age=age;}
        else set(s,tm->ntc_c[item->channel-1],100,age,30000);
    }
    else if (item->channel==1) set(s,tm->bme_temp_full_c,100,age,30000);
    else if (item->channel==2) set(s,tm->bme_pressure_pa,1,age,30000);
    else if (item->channel==3) set(s,tm->bme_humidity_rh,100,age,30000);
    else if (tm->bme_gas_valid) set(s,tm->bme_gas_resistance_ohm,1,age,30000);
    else {s->flags=ERROR;s->age=age;}
}
static void exp_sample(const item_t *item,sample_t *s,const char *json,time_t now,uint32_t epoch) {
    if (item->group==6 || item->group==7 || (item->source==3 && item->channel==4)) {
        s->flags=UNSUPPORTED;return;
    }
    double mask,age,status,value=0;
    unsigned idx=item->source==1 ? 4 : item->source==2 ? item->channel-1 :
        item->source==3 ? 5 : item->source==4 ? 6 : item->source==7 ? 7 : item->source==5 ? 8 : 9;
    if (scalar(json,"sensor_valid_mask",&mask) ||
        element(json,"sensor_age_ms",idx,&age) ||
        element(json,"sensor_status",idx,&status)) {s->flags=ERROR;return;}
    s->age=age>=0 && age<=UINT32_MAX-age_ms(now,epoch) ?
        (uint32_t)age+age_ms(now,epoch) : UINT32_MAX;
    if (!((uint32_t)mask&(1U<<idx)) || status!=2 || s->age>30000U) {
        s->flags=status==5 ? 0x08U : status==4 ? ERROR : STALE;return;
    }
    const char *key=NULL;unsigned element_index=0;double factor=1;
    if (item->source==1) {key="board_temp_c";factor=100;}
    else if (item->source==2) {key="ntc_c";element_index=item->channel-1;factor=100;}
    else if (item->source==3) {
        key=item->channel==1 ? "bme_temp_c" : item->channel==2 ? "bme_press_pa" : "bme_hum_rh";
        factor=item->channel==2 ? 1 : 100;
    } else if (item->source==4) {
        if (item->channel==1) {key="imu_temp_c";factor=100;}
        else if (item->channel<=4) {key="imu_accel_mg";element_index=item->channel-2;}
        else {key="imu_gyro_dps";element_index=item->channel-5;factor=1000;}
        double imu_mask;
        unsigned bit=item->channel==1 ? 2 : item->channel<=4 ? 0 : 1;
        if (scalar(json,"imu_mask",&imu_mask) || !((uint32_t)imu_mask&(1U<<bit))) {
            s->flags=STALE;return;
        }
    } else if (item->source==5 || item->source==6) {
        key=item->channel==1 ? "flow_ul_min" : "flow_temp_c";
        element_index=item->source-5;factor=item->channel==1 ? 100 : 100;
    } else if (item->source==7) {s->flags=VALID;s->value=1;return;}
    if ((item->source==2 || (item->source==4 && item->channel>=2) ||
         item->source==5 || item->source==6 ? element(json,key,element_index,&value) :
         scalar(json,key,&value))!=0) {s->flags=ERROR;return;}
    set(s,value,factor,s->age,30000);
}
int sensor_stat_read(uint8_t board,uint8_t group,uint8_t source,uint8_t cursor,
                     uint8_t *payload,size_t capacity,size_t *length) {
    if (!payload || !length || capacity<SENSOR_STAT_MAX_PAYLOAD) return -1;
    const item_t *items;size_t total;
    if (board==3) {items=obc_items;total=sizeof(obc_items)/sizeof(*items);}
    else if (board==4) {items=exp_items;total=sizeof(exp_items)/sizeof(*items);}
    else if (board==5) {items=pdu_items;total=sizeof(pdu_items)/sizeof(*items);}
    else return -2;
    if (group>7 || cursor==255) return -2;
    item_t selected[32];size_t count=0;
    for (size_t i=0;i<total;i++) if ((!group || items[i].group==group) &&
        (!source || items[i].source==source)) selected[count++]=items[i];
    if (!count || cursor>=count) return -2;
    time_t now=time(NULL);uint32_t epoch=now>=0 && (uint64_t)now<=UINT32_MAX ? (uint32_t)now : 0;
    bee_dal_telemetry_t obc={0};char exp_json[16384];gui_pdu_status_t pdu={0};
    bool online=true;
    if (board==3) hk_service_get_telemetry(&obc);
    else if (board==4) online=read_exp(exp_json,now,&epoch)==0;
    else online=gui_pdu_status(&pdu)==0;
    size_t off=0;
    bee_dal_put_u8(payload,&off,board);bee_dal_put_u8(payload,&off,group);
    bee_dal_put_u8(payload,&off,source);bee_dal_put_u8(payload,&off,online ? 0 : 1);
    size_t count_offset=off;bee_dal_put_u8(payload,&off,0);
    size_t next_offset=off;bee_dal_put_u8(payload,&off,255);
    bee_dal_put_u32(payload,&off,epoch);
    if (!online) {*length=off;return 0;}
    for (size_t i=cursor;i<count && i<cursor+SENSOR_STAT_MAX_RECORDS;i++) {
        const item_t *item=&selected[i];sample_t s={.age=UINT32_MAX,.value=INT32_MIN};
        if (board==3) obc_sample(item,&s,&obc,now);
        else if (board==4) exp_sample(item,&s,exp_json,now,epoch);
        else if (item->source==1 && pdu.temp_tenth!=INT16_MIN)
            set(&s,(int16_t)pdu.temp_tenth,10,0,30000);
        else if (item->source==2) {
            s.age=0;
            if (pdu.vout_mv!=UINT16_MAX && pdu.trans_mv!=UINT16_MAX) {
                s.flags=VALID;s.value=1;
            } else s.flags=ERROR;
        }
        else s.flags=ERROR;
        bee_dal_put_u8(payload,&off,item->source);
        bee_dal_put_u8(payload,&off,item->channel);
        bee_dal_put_u8(payload,&off,s.flags);
        bee_dal_put_u8(payload,&off,item->unit);
        bee_dal_put_u32(payload,&off,s.age);
        bee_dal_put_u32(payload,&off,(uint32_t)s.value);
        payload[count_offset]++;
    }
    if ((size_t)cursor+payload[count_offset]<count)
        payload[next_offset]=(uint8_t)(cursor+payload[count_offset]);
    *length=off;return 0;
}
