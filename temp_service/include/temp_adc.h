#pragma once
/*
 * temp_adc.h - ADC read + temperature conversion
 *
 * Supports:
 *   - TC1047A onboard temperature sensor (linear voltage output)
 *   - NTC thermistor via voltage divider (Beta equation)
 *
 * Channel mapping (SAMA7G54):
 *   AD9 / PC22  → onboard TC1047A (verify PC22 net on hardware)
 *   AD2 / PC15  → NTC1
 *   AD4 / PC17  → NTC2
 *   AD3 / PC16  → NTC3
 *   AD7 / PC20  → NTC4
 *
 * Voltage domains:
 *   VANA  = 2.5V  — ADC reference (read from in_voltage_scale)
 *   VDIV  = 3.3V  — NTC voltage divider supply (DEFAULT_NTC_VDIV_MV)
 *   These are DIFFERENT and must not be confused.
 */

#include "temp_fs.h"
#include <stddef.h>

#define DEFAULT_ADC_DEV      "auto"
#define DEFAULT_NTC_VDIV_MV  3300.0f    /* NTC divider supply voltage mV  */
#define DEFAULT_NTC_RS       10000.0f   /* series resistor ohm            */
#define DEFAULT_NTC_R0       10000.0f   /* NTC nominal ohm @ T0           */
#define DEFAULT_NTC_BETA     3950.0f    /* B-coefficient                  */
#define DEFAULT_NTC_T0       25.0f      /* nominal temp °C                */
#define DEFAULT_NTC_PULLDOWN 0          /* 0 = pullup:   VDIV-[NTC]-+-[RS]-GND
                                         * 1 = pulldown: VDIV-[RS]-+-[NTC]-GND */

/* Kept for CLI backward compat only — NOT used in ADC conversion.
 * Actual VANA is derived from in_voltage_scale * (2^bits - 1). */
#define DEFAULT_VREF_MV      2500.0f

typedef struct {
    char  adc_dev[128];   /* sysfs IIO device path                      */
    float vref_mv;        /* legacy field, not used in conversion        */
    float ntc_vdiv_mv;    /* NTC divider supply voltage mV (e.g. 3300)  */
    float ntc_rs;         /* series resistor ohm                        */
    float ntc_r0;         /* NTC nominal resistance ohm @ t0            */
    float ntc_beta;       /* NTC B-coefficient                          */
    float ntc_t0;         /* NTC nominal temperature °C                 */
    int   ntc_pulldown;   /* 0: pullup VDIV-NTC-RS-GND / 1: pulldown   */
    int   adc_bits;       /* ADC resolution bits (12 or 16)             */
} TempAdcCfg;

/* Fill cfg with defaults. */
void temp_adc_cfg_defaults(TempAdcCfg *cfg);

/* Read all channels and convert. Returns 0 on success, -1 on error. */
int  temp_adc_read(TempData *out, const TempAdcCfg *cfg);

/* Locate the ADC by its channel attributes, avoiding unstable iio:deviceN. */
int temp_adc_find_device(const char *iio_root, char *out, size_t out_size);
