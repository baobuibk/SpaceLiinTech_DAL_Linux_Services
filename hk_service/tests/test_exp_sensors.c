#include "hk_can.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Lite-client's getall row format and the EXP firmware's RPARAM table 2. */
static const char dump[] =
    "  0x0000 schema           U16 5 \r\n"
    "  0x0002 seq              U32 42 \r\n"
    "  0x0006 tick_ms          U32 123456 \r\n"
    "  0x000A supported        U16 1023 \r\n"
    "  0x000C valid            U16 1023 \r\n"
    "  0x000E status           U8 0 0 0 0 0 0 0 0 0 0 \r\n"
    "  0x0018 age_ms           U32 1 2 3 4 5 6 7 8 9 10 \r\n"
    "  0x0040 ntc_mC           I32 25000 26000 -5000 28000 \r\n"
    "  0x0050 board_mC         I32 30000 \r\n"
    "  0x0054 bme_mC           I32 23000 \r\n"
    "  0x0058 bme_Pa           U32 100000 \r\n"
    "  0x005C bme_mRH          U32 45678 \r\n"
    "  0x0060 accel_mg         I32 1 -2 3 \r\n"
    "  0x006C gyro_mdps        I32 -1000 0 2000 \r\n"
    "  0x0078 imu_mC           I32 22000 \r\n"
    "  0x007C flow_x100        I32 1234 5678 \r\n"
    "  0x0084 flow_mC          I32 10000 20000 \r\n"
    "  0x008C voltage_mV       U32 12000 12100 5000 5100 \r\n"
    "  0x009C current_mA       I32 1 -2 3 4 \r\n"
    "  0x00AC power_mW         I32 12 -24 15 20 \r\n"
    "  0x00BC rail_enabled     U8 1 0 1 \r\n"
    "  0x00BF sample_seq       U32 1 1 1 1 1 1 1 1 1 1 \r\n"
    "  0x00E7 last_error       U8 0 0 0 0 0 0 0 0 0 0 \r\n"
    "  0x00F1 desired          U8 1 1 1 1 1 1 1 1 1 1 \r\n"
    "  0x00FB applied          U8 1 1 1 1 1 1 1 1 1 1 \r\n"
    "  0x0105 imu_mask         U8 7 \r\n";

int main(void)
{
    HkExpSensors data;
    assert(hk_can_parse_sensors(dump, &data) == 0);
    assert(data.schema == 5 && data.seq == 42 && data.tick_ms == 123456);
    assert(data.supported == 1023 && data.valid == 1023);
    assert(data.status[9] == 0 && data.age_ms[9] == 10);
    assert(data.ntc_c[2] == -5.0f && data.board_temp_c == 30.0f);
    assert(data.bme_press_pa == 100000 && data.bme_hum_rh > 45.67f);
    assert(data.accel_mg[1] == -2 && data.gyro_dps[0] == -1.0f);
    assert(data.flow_ul_min[0] == 12.34f && data.flow_temp_c[1] == 20.0f);
    assert(data.pwr_volt_mv[1] == 12100 && data.pwr_curr_ma[1] == -2);
    assert(data.pwr_mw[1] == -24 && data.rail_enabled[1] == 0);
    assert(data.imu_mask == 7);

    char malformed[sizeof(dump)];
    strcpy(malformed, dump);
    char *row = strstr(malformed, "U8 7 ");
    assert(row);
    memcpy(row, "U8 8 ", 5);
    assert(hk_can_parse_sensors(malformed, &data) == 0);
    row = strstr(malformed, "U16 5 ");
    assert(row);
    memcpy(row, "U16 4 ", 6);
    assert(hk_can_parse_sensors(malformed, &data) < 0);
    assert(hk_can_parse_sensors("  0x0000 schema U16 5\n", &data) < 0);
    puts("EXP table 2 parser fixture passed");
    return 0;
}
