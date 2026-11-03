#include "services/obc_snapshot.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void write_fixture(const char *path, time_t stamp, int online)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f,
        "{\"board\":\"obc\",\"schema_version\":1,\"online\":%d,"
        "\"boot_count\":3,\"obc_timestamp\":%ld,"
        "\"temp_valid\":1,\"temp_sample_unix_s\":%ld,"
        "\"temp_onboard_c\":25.25,\"temp_ntc_c\":[12.34,\"N/A\",30,40],"
        "\"bme_valid\":1,\"bme_sample_unix_s\":%ld,"
        "\"bme_temp_c\":24,\"bme_pressure_pa\":101325,"
        "\"bme_humidity_rh\":48.5,\"bme_gas_valid\":1,"
        "\"bme_gas_resistance_ohm\":123456,"
        "\"pac_sample_unix_s\":%ld,\"pac_u6_valid\":1,\"pac_u2_valid\":0,"
        "\"pwr_rail_names\":[\"5V_SAMA7\",\"5V_FPGA\",\"3V3\",\"5V_AI\","
        "\"PMIC_3V3\",\"PMIC_DDR_1V35\",\"PMIC_CORE_1V15\",\"PMIC_CPU_1V15\"],"
        "\"pwr_volt_v\":[5.01,5,3.3,5,3.3,0,0,0],"
        "\"pwr_curr_a\":[-0.125,0.2,0.3,0.4,0.1,0,0,0]}\n",
        online, (long)stamp, (long)stamp, (long)stamp, (long)stamp);
    assert(fclose(f) == 0);
}

int main(void)
{
    char path[] = "/tmp/dal-obc-snapshot-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    time_t now = time(NULL);
    bee_dal_telemetry_t s;
    write_fixture(path, now, 1);
    assert(obc_snapshot_read(path, now, 30, &s) == 0);
    assert(s.obc_snapshot_valid && s.temp_valid && s.bme_valid);
    assert(s.pac_valid[0] && !s.pac_valid[1]);
    assert(s.bootcount == 3 && s.onboard_temp_c == 2525);
    assert(s.ntc_c[1] < -40);
    assert(s.baro_press_pa == 10133 && s.baro_hum_rh == 485);
    assert(fabsf(s.pwr_volt_v[0] - 5.01f) < 0.001f);
    assert(s.pwr_volt_v[4] == 0 && s.pwr_curr_a[4] == 0);

    write_fixture(path, now - 31, 1);
    assert(obc_snapshot_read(path, now, 30, &s) != 0);
    write_fixture(path, now, 0);
    assert(obc_snapshot_read(path, now, 30, &s) != 0);
    unlink(path);
    puts("OBC RAM snapshot parsing and age: OK");
    return 0;
}
