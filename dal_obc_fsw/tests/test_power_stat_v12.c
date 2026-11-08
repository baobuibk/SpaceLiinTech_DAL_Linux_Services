#include "services/power_stat.h"
#include "services/hk_service.h"
#include "services/gui_state.h"
#include "services/power_en.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
static int overflow;
void hk_service_get_telemetry(bee_dal_telemetry_t *t) {
    memset(t,0,sizeof(*t));t->obc_snapshot_valid=true;t->obc_sample_epoch=t->pac_sample_epoch=(uint32_t)time(NULL);
    t->pac_valid[0]=t->pac_valid[1]=true;
    for (unsigned i=0;i<8;i++) {t->pwr_volt_v[i]=5.0f;t->pwr_curr_a[i]=-0.125f;}
    if (overflow) {t->pwr_volt_v[0]=100.0f;t->pwr_curr_a[0]=40.0f;}
}
int power_en_execute(uint8_t b,uint16_t s,uint16_t on,uint8_t f,power_en_result_t *r) {
    (void)b;(void)s;(void)on;(void)f;*r=(power_en_result_t){.on=1,.known=255};return 0;
}
int exp_csp_rail_state(uint16_t *known,uint16_t *on) {*known=7;*on=5;return 0;}
int gui_pdu_status(gui_pdu_status_t *r) {
    *r=(gui_pdu_status_t){.valid=127,.tps_state=PDU_TPS_READY,.vout_mv=12000,.vout_ma=125,.trans_mv=5000,.trans_ma=250,
        .input_12v_mv=12000,.input_5v_mv=5000,.vout_on=true,.trans_on=true};return 0;
}
static unsigned u16(const uint8_t *p) {return p[0]|p[1]<<8;}
int main(void) {
    setenv("DAL_EXP_HK_SNAPSHOT_PATH","/does/not/exist/power-test",1);
    uint8_t out[202];size_t length=0;
    memset(out,0xa5,sizeof(out));
    assert(!power_stat_report(0,out,200,&length));assert(length==184 && u16(out)==0x200 && u16(out+2)==27);
    assert(out[200]==0xa5 && out[201]==0xa5);
    assert(u16(out+4)==0x8003); /* EXP PAC missing, known control state retained */
    assert((u16(out+6)&127)==0x30);assert(u16(out+8)==5000);assert((int16_t)u16(out+10)==-125);
    assert(!power_stat_report(1,out,200,&length) && length==118 && u16(out+2)==16);
    assert(!power_stat_report(2,out,200,&length) && length==100 && u16(out+2)==13);
    assert(!power_stat_report(3,out,200,&length) && length==46 && u16(out+2)==4);
    for(unsigned i=0;i<4;i++) assert((u16(out+6+6*i)&127)==(0x40|i));
    overflow=1;assert(!power_stat_report(0,out,200,&length));
    assert(((u16(out+6)>>7)&12)==0 && u16(out+8)==0 && u16(out+10)==0);
    assert(power_stat_report(4,out,200,&length)==-2);
    assert(power_stat_report(0,out,10,&length)==-1);
    puts("power_stat v1.2 selectors, bounds, signed values and validity: OK");
}
