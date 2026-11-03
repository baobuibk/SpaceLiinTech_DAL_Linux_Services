/* include/services/hk_service.h
 *
 * Housekeeping telemetry cache for dal_obc_fsw.
 */
#ifndef HK_SERVICE_H
#define HK_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "utils/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sys_time_epoch;  
    int8_t   sys_status;      
    uint32_t bootcount;       
    int16_t  onboard_temp_c;  
    int16_t  baro_temp_c;     
    uint16_t baro_press_pa;   
    uint16_t baro_hum_rh;     
    int16_t  mlx_temp_c;      
    uint16_t mlx_status;      
    int16_t  slf_temp_c;      
    int16_t  slf_flow_rate;   
    uint16_t rba_status;      
    uint16_t ta_status;       
    /* OBC acquisition cache. The legacy 29-byte tm_get remains unchanged. */
    bool obc_snapshot_valid;
    uint32_t obc_sample_epoch;
    uint32_t temp_sample_epoch;
    uint32_t bme_sample_epoch;
    uint32_t pac_sample_epoch;
    bool temp_valid;
    bool bme_valid;
    bool bme_gas_valid;
    bool pac_valid[2];
    float onboard_temp_full_c;
    float ntc_c[4];
    float bme_temp_full_c;
    float bme_pressure_pa;
    float bme_humidity_rh;
    float bme_gas_resistance_ohm;
    float pwr_volt_v[8];
    float pwr_curr_a[8];
} bee_dal_telemetry_t;

xerr_t hk_service_init(void);
xerr_t hk_service_start(void);
void   hk_service_stop(void);
bool   hk_service_is_running(void);

void hk_service_get_telemetry(bee_dal_telemetry_t *out);

#ifdef __cplusplus
}
#endif
#endif /* HK_SERVICE_H */
