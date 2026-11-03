#include "services/obc_hk_wire.h"
#include "bee_dal_proto.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    bee_dal_telemetry_t s = {0};
    s.obc_sample_epoch = 0x12345678;
    s.bootcount = 7;
    s.temp_valid = true;
    s.onboard_temp_full_c = 25.25f;
    s.ntc_c[0] = 12.34f;
    s.ntc_c[1] = -5.50f;
    s.ntc_c[2] = -50.0f; /* unpopulated channel */
    s.ntc_c[3] = 30.0f;
    s.bme_valid = true;
    s.bme_gas_valid = true;
    s.bme_temp_full_c = 24.0f;
    s.bme_pressure_pa = 101325.0f;
    s.bme_humidity_rh = 48.5f;
    s.bme_gas_resistance_ohm = 123456.0f;
    s.pac_valid[0] = true;
    s.pwr_volt_v[0] = 5.01f;
    s.pwr_curr_a[0] = -0.125f;
    s.pwr_volt_v[4] = 3.3f; /* invalid U2 must be suppressed */

    uint8_t payload[OBC_HK_WIRE_LEN];
    memset(payload, 0xAA, sizeof(payload));
    obc_hk_encode(&s, payload);

    size_t off = 0;
    assert(bee_dal_get_u16(payload, &off) == 0x00F7);
    assert(bee_dal_get_u32(payload, &off) == 0x12345678);
    assert(bee_dal_get_u32(payload, &off) == 7);
    assert((int16_t)bee_dal_get_u16(payload, &off) == 2525);
    assert((int16_t)bee_dal_get_u16(payload, &off) == 1234);
    assert((int16_t)bee_dal_get_u16(payload, &off) == -550);
    assert(bee_dal_get_u16(payload, &off) == 0);
    assert((int16_t)bee_dal_get_u16(payload, &off) == 3000);
    assert((int16_t)bee_dal_get_u16(payload, &off) == 2400);
    assert(bee_dal_get_u32(payload, &off) == 101325);
    assert(bee_dal_get_u16(payload, &off) == 4850);
    assert(bee_dal_get_u32(payload, &off) == 123456);
    assert(off == 32);
    assert(bee_dal_get_u16(payload, &off) == 5010);
    for (unsigned i = 1; i < 8; i++)
        assert(bee_dal_get_u16(payload, &off) == 0);
    assert(off == 48);
    assert((int16_t)bee_dal_get_u16(payload, &off) == -125);
    for (unsigned i = 1; i < 8; i++)
        assert(bee_dal_get_u16(payload, &off) == 0);
    assert(off == OBC_HK_WIRE_LEN);
    puts("OBC HK wire payload: OK");
    return 0;
}
