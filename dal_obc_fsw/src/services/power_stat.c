#include "services/power_stat.h"
#include "services/hk_service.h"
#include "services/gui_state.h"
#include "services/power_en.h"
#include "services/exp_csp_service.h"
#include "bee_dal_proto.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXP_SNAPSHOT "/run/dal/dal_obc_fsw/exp_data.json"
/* RPARAM table 2: PAC1934 is group 7; groups 8 and 9 are flow sensors. */
#define EXP_PAC_GROUP 7U
#define FLAG_CONTROL_KNOWN 0x01U
#define FLAG_CONTROL_ON    0x02U
#define FLAG_VOLT_VALID    0x04U
#define FLAG_CURR_VALID    0x08U
#define FLAG_FRESH         0x10U

typedef struct {
    uint8_t flags;
    uint16_t age_ms;
    uint32_t voltage_mv;
    int32_t current_ma;
} source_t;

static uint16_t age_ms(time_t now, uint32_t epoch)
{
    if (!epoch || now<(time_t)epoch) return UINT16_MAX;
    uint64_t diff=(uint64_t)(now-(time_t)epoch)*1000U;
    return diff>=UINT16_MAX ? UINT16_MAX : (uint16_t)diff;
}

static uint32_t scaled_voltage(float value)
{
    double v=round((double)value*1000.0);
    return isfinite(v) && v>=0 && v<=UINT32_MAX ? (uint32_t)v : 0;
}

static int32_t scaled_current(float value)
{
    double v=round((double)value*1000.0);
    return isfinite(v) && v>=INT32_MIN && v<=INT32_MAX ? (int32_t)v : 0;
}

/* Parse only the integer fields produced by hk_service's atomic EXP JSON.
 * A missing or malformed field leaves the PAC measurements unavailable. */
static const char *field(const char *json, const char *key)
{
    char pattern[64];
    if (snprintf(pattern,sizeof(pattern),"\"%s\"",key)>=(int)sizeof(pattern)) return NULL;
    const char *p=strstr(json,pattern);
    if (!p) return NULL;
    p=strchr(p+strlen(pattern),':');
    if (!p) return NULL;
    do { ++p; } while (*p==' ' || *p=='\n' || *p=='\t');
    return p;
}

static int integer(const char *json, const char *key, long long *value)
{
    const char *p=field(json,key);
    if (!p) return -1;
    char *end;
    errno=0;
    long long n=strtoll(p,&end,10);
    if (errno || end==p || (*end!=',' && *end!='\n' && *end!='}' && *end!=']')) return -1;
    *value=n;
    return 0;
}

static int array_n(const char *json, const char *key, long long *values, int count)
{
    const char *p=field(json,key);
    if (!p || *p!='[') return -1;
    ++p;
    for (int i=0;i<count;i++) {
        while (*p==' ') ++p;
        char *end;
        errno=0;
        long long n=strtoll(p,&end,10);
        if (errno || end==p) return -1;
        values[i]=n;p=end;
        while (*p==' ') ++p;
        if (*p!=(i==count-1 ? ']' : ',')) return -1;
        ++p;
    }
    return 0;
}

static uint32_t read_obc(source_t sources[16], time_t now)
{
    bee_dal_telemetry_t tm;
    hk_service_get_telemetry(&tm);
    uint32_t epoch=tm.obc_snapshot_valid ? tm.obc_sample_epoch : 0;
    uint16_t age=age_ms(now,tm.pac_sample_epoch);
    for (unsigned i=0;i<8;i++) {
        if (!tm.obc_snapshot_valid || !tm.pac_valid[i/4] ||
            age==UINT16_MAX || age>30000U ||
            !isfinite(tm.pwr_volt_v[i]) || tm.pwr_volt_v[i]<0 ||
            !isfinite(tm.pwr_curr_a[i])) continue;
        sources[i].voltage_mv=scaled_voltage(tm.pwr_volt_v[i]);
        sources[i].current_ma=scaled_current(tm.pwr_curr_a[i]);
        sources[i].age_ms=age;
        sources[i].flags=FLAG_VOLT_VALID|FLAG_CURR_VALID|FLAG_FRESH;
    }
    power_en_result_t state;
    if (power_en_execute(3,0,0,0,&state)==0) {
        for (unsigned i=0;i<8;i++) if (state.known&(1U<<i)) {
            sources[i+8].flags=FLAG_CONTROL_KNOWN|FLAG_FRESH|
                ((state.on&(1U<<i)) ? FLAG_CONTROL_ON : 0);
            sources[i+8].age_ms=0;
        }
    }
    return epoch;
}

static uint32_t read_exp(source_t sources[16], time_t now)
{
    const char *path=getenv("DAL_EXP_HK_SNAPSHOT_PATH");
    if (!path || !*path) path=EXP_SNAPSHOT;
    char json[16384];
    FILE *fp=fopen(path,"r");
    uint32_t epoch=0;
    if (fp) {
        size_t n=fread(json,1,sizeof(json)-1,fp);
        int extra=fgetc(fp);fclose(fp);
        json[n]=0;
        long long valid=0, online=0, ts=0, mask=0;
        long long volt[4],curr[4],sensor_age[10],sensor_status[10];
        if (extra==EOF && integer(json,"data_valid",&valid)==0 && valid==1 &&
            integer(json,"online",&online)==0 && online==1 &&
            integer(json,"obc_timestamp",&ts)==0 && ts>0 && ts<=UINT32_MAX &&
            integer(json,"sensor_valid_mask",&mask)==0 && (mask&(1U<<EXP_PAC_GROUP)) &&
            array_n(json,"pwr_volt_mv",volt,4)==0 &&
            array_n(json,"pwr_curr_ma",curr,4)==0 &&
            array_n(json,"sensor_age_ms",sensor_age,10)==0 &&
            array_n(json,"sensor_status",sensor_status,10)==0 &&
            sensor_status[EXP_PAC_GROUP]==2 && sensor_age[EXP_PAC_GROUP]>=0 &&
            sensor_age[EXP_PAC_GROUP]<=30000) {
            uint16_t snapshot_age=age_ms(now,(uint32_t)ts);
            uint32_t full_age=(uint32_t)snapshot_age+(uint32_t)sensor_age[EXP_PAC_GROUP];
            if (snapshot_age!=UINT16_MAX && full_age<=30000U) {
                epoch=(uint32_t)ts;
                for (unsigned i=0;i<4;i++) if (volt[i]>=0 && volt[i]<=UINT32_MAX &&
                    curr[i]>=INT32_MIN && curr[i]<=INT32_MAX) {
                    sources[i].voltage_mv=(uint32_t)volt[i];
                    sources[i].current_ma=(int32_t)curr[i];
                    sources[i].age_ms=(uint16_t)full_age;
                    sources[i].flags=FLAG_VOLT_VALID|FLAG_CURR_VALID|FLAG_FRESH;
                }
            }
        }
    }
    uint16_t known=0, on=0;
    if (exp_csp_rail_state(&known,&on)==0) {
        for (unsigned i=0;i<3;i++) if (known&(1U<<i)) {
            sources[i+4].flags=FLAG_CONTROL_KNOWN|FLAG_FRESH|
                ((on&(1U<<i)) ? FLAG_CONTROL_ON : 0);
            sources[i+4].age_ms=0;
        }
    }
    return epoch;
}

static uint32_t read_pdu_sample(source_t sources[16], time_t now, const gui_pdu_status_t *sample)
{
    if (!sample->valid) return 0;
    gui_pdu_status_t pdu=*sample;
    uint16_t volts[4]={pdu.vout_mv,pdu.trans_mv,pdu.input_12v_mv,pdu.input_5v_mv};
    uint16_t amps[2]={pdu.vout_ma,pdu.trans_ma};
    for (unsigned i=0;i<4;i++) {
        sources[i].age_ms=0;
        sources[i].flags=FLAG_FRESH;
        if (volts[i]!=UINT16_MAX) {
            sources[i].voltage_mv=volts[i];
            sources[i].flags|=FLAG_VOLT_VALID;
        }
        if (i<2 && amps[i]!=UINT16_MAX) {
            sources[i].current_ma=amps[i];
            sources[i].flags|=FLAG_CURR_VALID;
        }
        if (i<2 && (pdu.valid&(i==0 ? PDU_VALID_VOUT : PDU_VALID_TRANS))) sources[i].flags|=FLAG_CONTROL_KNOWN|
            ((i==0 ? pdu.vout_on : pdu.trans_on) ? FLAG_CONTROL_ON : 0);
    }
    return now>=0 && (uint64_t)now<=UINT32_MAX ? (uint32_t)now : 0;
}

static uint32_t read_pdu(source_t sources[16], time_t now) {
    gui_pdu_status_t sample={0};
    if(gui_pdu_status(&sample)!=0) return 0;
    return read_pdu_sample(sources,now,&sample);
}

int power_stat_read(uint8_t board, uint16_t mask, uint8_t *payload,
                    size_t capacity, size_t *length)
{
    unsigned count=board==3 ? 16U : board==4 ? 7U : board==5 ? 4U : 0U;
    if (!count || (count<16 && (mask>>count))) return -2;
    if (!payload || !length || capacity<POWER_STAT_MAX_PAYLOAD) return -1;
    source_t sources[16]={0};
    for (unsigned i=0;i<count;i++) sources[i].age_ms=UINT16_MAX;
    time_t now=time(NULL);
    uint32_t epoch=board==3 ? read_obc(sources,now) :
                   board==4 ? read_exp(sources,now) : read_pdu(sources,now);
    size_t off=0;
    bee_dal_put_u8(payload,&off,board);
    size_t count_offset=off;
    bee_dal_put_u8(payload,&off,0);
    bee_dal_put_u32(payload,&off,epoch);
    for (unsigned i=0;i<count;i++) if (!mask || (mask&(1U<<i))) {
        bee_dal_put_u8(payload,&off,(uint8_t)i);
        bee_dal_put_u8(payload,&off,sources[i].flags);
        bee_dal_put_u16(payload,&off,sources[i].age_ms);
        bee_dal_put_u32(payload,&off,sources[i].voltage_mv);
        bee_dal_put_u32(payload,&off,(uint32_t)sources[i].current_ma);
        payload[count_offset]++;
    }
    *length=off;
    return 0;
}

int power_stat_report(uint8_t report, uint8_t *payload, size_t capacity, size_t *length)
{
    if (report>3) return -2;
    if (!payload || !length || capacity<POWER_STAT_V12_MAX_PAYLOAD) return -1;
    gui_pdu_status_t pdu={0};
    if (gui_pdu_status(&pdu)!=0) {
        pdu=(gui_pdu_status_t){.age_ms=UINT32_MAX,.temp_tenth=(int16_t)0x8000,.setpoint_mv=UINT16_MAX};
    }
    size_t off=0;uint16_t records=0,errors=0x8000;
    /* Bit 15 explicitly declares hardware fault latches unavailable in this
     * snapshot interface. Bits 0..2 describe missing expected readbacks only. */
    bee_dal_put_u16(payload,&off,(uint16_t)(0x0200|report));
    bee_dal_put_u16(payload,&off,0);
    bee_dal_put_u16(payload,&off,0);
    time_t now=time(NULL);
    for (uint8_t board=3;board<=5;board++) {
        source_t sources[16]={0};
        unsigned count=board==3 ? 16 : board==4 ? 7 : 4;
        if (board==3) read_obc(sources,now);
        else if (board==4) read_exp(sources,now);
        else read_pdu_sample(sources,now,&pdu);
        for (unsigned i=0;i<count;i++) {
            source_t *s=&sources[i];
            bool voltage=board==3 ? i<8 : board==4 ? i<4 : true;
            bool current=board==5 ? i<2 : voltage;
            bool control=board==3 ? i>=8 : board==4 ? i>=4 : i<2;
            bool expected_control=control && !(board==5 && i==0 && pdu.tps_state==PDU_TPS_UNPOWERED);
            uint8_t flags=s->flags;
            if (s->voltage_mv>UINT16_MAX) flags&=(uint8_t)~FLAG_VOLT_VALID;
            if (s->current_ma<INT16_MIN || s->current_ma>INT16_MAX) flags&=(uint8_t)~FLAG_CURR_VALID;
            uint16_t missing=(voltage && !(flags&FLAG_VOLT_VALID) ? 1 : 0) |
                             (current && !(flags&FLAG_CURR_VALID) ? 2 : 0) |
                             (expected_control && !(flags&FLAG_CONTROL_KNOWN) ? 4 : 0);
            errors|=missing;
            if ((report==1 && !voltage) || (report==2 && !control) || (report==3 && !missing)) continue;
            if (off+6>capacity || off+6>POWER_STAT_V12_MAX_PAYLOAD) return -1;
            uint16_t descriptor=(uint16_t)(i | board<<4 | flags<<7);
            bee_dal_put_u16(payload,&off,descriptor);
            bee_dal_put_u16(payload,&off,(flags&FLAG_VOLT_VALID) ? (uint16_t)s->voltage_mv : 0);
            bee_dal_put_i16(payload,&off,(flags&FLAG_CURR_VALID) ? (int16_t)s->current_ma : 0);
            records++;
        }
    }
    /* Fixed 16-byte PDU status trailer, independent of the report filter. */
    if (off+16>capacity || off+16>POWER_STAT_V12_MAX_PAYLOAD) return -1;
    bee_dal_put_u16(payload,&off,pdu.valid);
    bee_dal_put_u16(payload,&off,pdu.status_flags);
    bee_dal_put_u8(payload,&off,pdu.mode);
    bee_dal_put_u8(payload,&off,pdu.tps_state);
    bee_dal_put_i16(payload,&off,pdu.temp_tenth);
    bee_dal_put_u32(payload,&off,pdu.age_ms);
    bee_dal_put_u16(payload,&off,pdu.setpoint_mv);
    bee_dal_put_u16(payload,&off,0);
    size_t header=2;bee_dal_put_u16(payload,&header,records);bee_dal_put_u16(payload,&header,errors);
    *length=off;return 0;
}
