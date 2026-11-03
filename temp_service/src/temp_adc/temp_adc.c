/*
 * temp_adc.c - ADC read and temperature conversion
 *
 * TC1047A (per datasheet):
 *   Vout = (10 mV/°C) * T + 500 mV
 *   T(°C) = (Vout_mV - 500) / 10
 *
 * ADC voltage (SAMA7G54 IIO driver):
 *   Vadc_mV = raw * scale_mv_per_lsb
 *   scale read from: <adc_dev>/in_voltage_scale
 *   VANA = scale * (2^bits - 1)  e.g. 0.038147 * 65535 = 2500 mV
 *
 * NTC voltage divider — VDIV (3.3V) ≠ VANA (2.5V):
 *
 *   Pullup topology (NTC_PULLDOWN=0):  ← default, matches hardware
 *     VDIV --[NTC]--+-- ADC input (measured vs GND, max = VANA)
 *                   |
 *                  [RS]
 *                   |
 *                  GND
 *     Vadc = VDIV * RS / (RS + Rntc)
 *     Rntc = RS * (VDIV - Vadc) / Vadc
 *
 *   Pulldown topology (NTC_PULLDOWN=1):
 *     VDIV --[RS]--+-- ADC input
 *                  |
 *                 [NTC]
 *                  |
 *                 GND
 *     Vadc = VDIV * Rntc / (RS + Rntc)
 *     Rntc = RS * Vadc / (VDIV - Vadc)
 */

#include "temp_adc.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

/* -- Defaults ----------------------------------------------------------- */

void temp_adc_cfg_defaults(TempAdcCfg *cfg)
{
    strncpy(cfg->adc_dev, DEFAULT_ADC_DEV, sizeof(cfg->adc_dev) - 1);
    cfg->vref_mv      = DEFAULT_VREF_MV;       /* legacy, unused in math  */
    cfg->ntc_vdiv_mv  = DEFAULT_NTC_VDIV_MV;   /* 3300 mV divider supply  */
    cfg->ntc_rs       = DEFAULT_NTC_RS;
    cfg->ntc_r0       = DEFAULT_NTC_R0;
    cfg->ntc_beta     = DEFAULT_NTC_BETA;
    cfg->ntc_t0       = DEFAULT_NTC_T0;
    cfg->ntc_pulldown = DEFAULT_NTC_PULLDOWN;
    cfg->adc_bits     = 16;                     /* SAMA7G54 ADC is 16-bit  */
}

/* -- Internal helpers --------------------------------------------------- */

static int read_raw_channel(const char *adc_dev, int channel)
{
    char path[192];
    snprintf(path, sizeof(path), "%s/in_voltage%d_raw", adc_dev, channel);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[temp_adc] cannot open %s\n", path);
        return -1;
    }
    int val = -1;
    if (fscanf(f, "%d", &val) != 1)
        val = -1;
    fclose(f);
    return val;
}

int temp_adc_find_device(const char *iio_root, char *out, size_t out_size)
{
    DIR *dir = opendir(iio_root);
    if (!dir) return -1;
    const int required[] = { 9, 2, 4, 3, 7 };
    struct dirent *entry;
    int found = -1;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0)
            continue;
        char path[256], attribute[320];
        if (snprintf(path, sizeof(path), "%s/%s", iio_root, entry->d_name)
            >= (int)sizeof(path)) continue;
        if (snprintf(attribute, sizeof(attribute), "%s/in_voltage_scale", path)
            >= (int)sizeof(attribute) || access(attribute, R_OK) != 0)
            continue;
        int ok = 1;
        for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
            if (snprintf(attribute, sizeof(attribute), "%s/in_voltage%d_raw",
                         path, required[i]) >= (int)sizeof(attribute) ||
                access(attribute, R_OK) != 0) {
                ok = 0;
                break;
            }
        }
        if (!ok) continue;
        if (snprintf(out, out_size, "%s", path) >= (int)out_size)
            continue;
        found = 0;
        break;
    }
    closedir(dir);
    return found;
}

/* Read the real IIO scale in mV/LSB; never invent a scale on error. */
static int read_iio_scale(const char *adc_dev, float *scale)
{
    char path[192];
    snprintf(path, sizeof(path), "%s/in_voltage_scale", adc_dev);

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = fscanf(f, "%f", scale) == 1;
    fclose(f);
    return ok && isfinite(*scale) && *scale > 0 ? 0 : -1;
}

/*
 * TC1047A: T(°C) = (Vadc_mV - 500) / 10
 */
static float tc1047_convert(int raw, float scale_mv_per_lsb)
{
    float vadc_mv = raw * scale_mv_per_lsb;
    return (vadc_mv - 500.0f) / 10.0f;
}

/*
 * NTC Beta equation.
 *
 * Rntc is derived from the voltage divider using VDIV (3.3V supply),
 * NOT from VANA (2.5V ADC reference). These are different voltages.
 *
 *   Pullup  (pulldown=0): Rntc = RS * (VDIV - Vadc) / Vadc
 *   Pulldown(pulldown=1): Rntc = RS * Vadc / (VDIV - Vadc)
 *
 *   Beta:  1/T = 1/T0 + (1/B) * ln(Rntc / R0)
 */
static float ntc_convert(int raw, float scale_mv_per_lsb, float vdiv_mv,
                          float rs, float r0, float beta, float t0_c,
                          int pulldown)
{
    float vadc_mv = raw * scale_mv_per_lsb;

    /* clamp: Vadc must be in (0, VDIV) */
    if (vadc_mv < 0.001f)          vadc_mv = 0.001f;
    if (vadc_mv > vdiv_mv - 0.001f) vadc_mv = vdiv_mv - 0.001f;

    float rntc;
    if (pulldown == 0)
        rntc = rs * (vdiv_mv - vadc_mv) / vadc_mv;   /* pullup  */
    else
        rntc = rs * vadc_mv / (vdiv_mv - vadc_mv);   /* pulldown */

    if (rntc <= 0.0f) rntc = 0.001f;

    float t0_k  = t0_c + 273.15f;
    float inv_t = (1.0f / t0_k) + (1.0f / beta) * logf(rntc / r0);
    return (1.0f / inv_t) - 273.15f;
}

/* -- Public API --------------------------------------------------------- */

/*
 * Channel order (index → ADC channel):
 *   [0] AD9  → onboard TC1047A  (PC22)
 *   [1] AD2  → NTC1             (PC15)
 *   [2] AD4  → NTC2             (PC17)
 *   [3] AD3  → NTC3             (PC16)
 *   [4] AD7  → NTC4             (PC20)
 */
static const int adc_channels[5] = { 9, 2, 4, 3, 7 };

int temp_adc_read(TempData *out, const TempAdcCfg *cfg)
{
    char resolved[128];
    const char *adc_dev = cfg->adc_dev;
    if (strcmp(adc_dev, "auto") == 0) {
        if (temp_adc_find_device("/sys/bus/iio/devices", resolved,
                                 sizeof(resolved)) < 0)
            return -1;
        adc_dev = resolved;
    }
    /* Read IIO scale — VANA reference*/
    float scale;
    if (read_iio_scale(adc_dev, &scale) < 0)
        return -1;

    int raw[5];
    for (int i = 0; i < 5; i++) {
        raw[i] = read_raw_channel(adc_dev, adc_channels[i]);
        if (raw[i] < 0) {
            fprintf(stderr, "[temp_adc] read failed on AD%d\n",
                    adc_channels[i]);
            out->valid = 0;
            return -1;
        }
        out->raw_ad[i] = raw[i];
    }

    /* Onboard TC1047A */
    out->onboard = tc1047_convert(raw[0], scale);

    /* NTC1..4 — pass ntc_vdiv_mv (3300)*/
    for (int i = 0; i < TEMP_NTC_COUNT; i++) {
        out->ntc[i] = ntc_convert(
            raw[i + 1], scale, cfg->ntc_vdiv_mv,
            cfg->ntc_rs, cfg->ntc_r0, cfg->ntc_beta, cfg->ntc_t0,
            cfg->ntc_pulldown
        );
    }

    out->valid     = 1;
    out->timestamp = (long)time(NULL);
    return 0;
}
