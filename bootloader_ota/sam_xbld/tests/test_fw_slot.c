#include <assert.h>
#include <stdio.h>

#include "fw_catalog.h"

int main(void)
{
    assert(fw_catalog_slot_from_name(
        "/home/bee/firmware/EXP/DAL_EXP_hw110_build429_4dc7bbb-dirty_v1.0.0_20261024_133547_slot1_signed.hex") == 1);
    assert(fw_catalog_slot_from_name("EXP_backup.hex") == 1);
    assert(fw_catalog_slot_from_name("EXP_s1.hex") == 1);
    assert(fw_catalog_slot_from_name("EXP_main.hex") == 0);
    assert(fw_catalog_slot_from_name("EXP_slot1_signed.hex.tmp") == 0);
    puts("Firmware slot filename detection: OK");
    return 0;
}
