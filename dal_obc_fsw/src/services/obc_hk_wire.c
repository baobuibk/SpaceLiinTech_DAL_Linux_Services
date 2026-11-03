#include "services/obc_hk_wire.h"
#include "bee_dal_proto.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static int16_t signed_scaled(double value, double scale)
{
    value = round(value * scale);
    if (!isfinite(value)) return 0;
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    return (int16_t)value;
}

static uint16_t unsigned_scaled(double value, double scale)
{
    value = round(value * scale);
    if (!isfinite(value) || value < 0) return 0;
    if (value > UINT16_MAX) value = UINT16_MAX;
    return (uint16_t)value;
}

static uint32_t unsigned_32(double value)
{
    value = round(value);
    if (!isfinite(value) || value < 0) return 0;
    if (value > UINT32_MAX) value = UINT32_MAX;
    return (uint32_t)value;
}

void obc_hk_encode(const bee_dal_telemetry_t *s, uint8_t out[OBC_HK_WIRE_LEN])
{
    size_t off = 0;
    uint16_t valid = 0;
    if (s->temp_valid) {
        valid |= 1U;
        for (unsigned i = 0; i < 4; i++)
            if (isfinite(s->ntc_c[i]) && s->ntc_c[i] >= -40.0f)
                valid |= (uint16_t)(1U << (i + 1));
    }
    if (s->bme_valid) valid |= 1U << 5;
    if (s->bme_valid && s->bme_gas_valid) valid |= 1U << 6;
    if (s->pac_valid[0]) valid |= 1U << 7;
    if (s->pac_valid[1]) valid |= 1U << 8;

    bee_dal_put_u16(out, &off, valid);
    bee_dal_put_u32(out, &off, s->obc_sample_epoch);
    bee_dal_put_u32(out, &off, s->bootcount);
    bee_dal_put_i16(out, &off,
                    s->temp_valid ? signed_scaled(s->onboard_temp_full_c, 100) : 0);
    for (unsigned i = 0; i < 4; i++)
        bee_dal_put_i16(out, &off,
                        (valid & (1U << (i + 1))) ? signed_scaled(s->ntc_c[i], 100) : 0);
    bee_dal_put_i16(out, &off,
                    s->bme_valid ? signed_scaled(s->bme_temp_full_c, 100) : 0);
    bee_dal_put_u32(out, &off,
                    s->bme_valid ? unsigned_32(s->bme_pressure_pa) : 0);
    bee_dal_put_u16(out, &off,
                    s->bme_valid ? unsigned_scaled(s->bme_humidity_rh, 100) : 0);
    bee_dal_put_u32(out, &off,
                    (s->bme_valid && s->bme_gas_valid) ?
                    unsigned_32(s->bme_gas_resistance_ohm) : 0);
    for (unsigned i = 0; i < 8; i++)
        bee_dal_put_u16(out, &off,
                        s->pac_valid[i / 4] ? unsigned_scaled(s->pwr_volt_v[i], 1000) : 0);
    for (unsigned i = 0; i < 8; i++)
        bee_dal_put_i16(out, &off,
                        s->pac_valid[i / 4] ? signed_scaled(s->pwr_curr_a[i], 1000) : 0);
    (void)off; /* OBC_HK_WIRE_LEN is the sum of the fixed fields above. */
}
