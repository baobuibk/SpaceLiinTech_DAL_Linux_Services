#include "bee_dal_files.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void file(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, strlen(data), f) == strlen(data));
    assert(fclose(f) == 0);
}

int main(void)
{
    char root[] = "/tmp/dal-obc-files-XXXXXX";
    assert(mkdtemp(root));
    const char *dirs[] = {"logs", "logs/hk", "logs/lwl", "logs/pc",
                          "images", "images/usb", "images/mipi",
                          "data", "data/lev", "data/other"};
    char path[512];
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        assert(mkdir(path, 0700) == 0);
    }
    time_t now = time(NULL);
    struct tm utc;
    char date[9], active[24];
    assert(gmtime_r(&now, &utc));
    assert(strftime(date, sizeof(date), "%Y%m%d", &utc) == 8);
    snprintf(active, sizeof(active), "HK0_%s.csv", date);
    snprintf(path, sizeof(path), "%s/logs/hk/%s", root, active); file(path, "now\n");
    snprintf(path, sizeof(path), "%s/logs/lwl/LWL_00000001.bin", root); file(path, "lwl\n");
    snprintf(path, sizeof(path), "%s/logs/pc/PC_00000001.bin", root); file(path, "pc\n");
    snprintf(path, sizeof(path), "%s/images/usb/USB0_00000001.jpg", root); file(path, "jpg\n");
    snprintf(path, sizeof(path), "%s/images/mipi/MIPI1.3_H_000001.raw", root); file(path, "raw\n");
    snprintf(path, sizeof(path), "%s/images/mipi/MIPI1.3_H_000001.png", root); file(path, "png\n");
    snprintf(path, sizeof(path), "%s/data/lev/LEV_00000001.bin", root); file(path, "lev\n");
    snprintf(path, sizeof(path), "%s/data/other/OTHER_00000001.bin", root); file(path, "other\n");
    assert(bee_dal_files_init(root) == XERR_OK);
    uint32_t snap, logs, images, data;
    assert(bee_dal_files_scan(0xFF, &snap, &logs, &images, &data) == XERR_OK);
    assert(logs == 1 && images == 3 && data == 2);
    bee_dal_file_entry_t entry;
    assert(bee_dal_files_get_entry(snap, 1, 0, &entry) == XERR_OK);
    assert(strcmp(entry.name, "MIPI1.3_H_000001.png") == 0);
    assert(bee_dal_files_get_entry(snap, 0, 0, &entry) == XERR_OK);
    assert(strcmp(entry.name, active) == 0);
    assert(bee_dal_files_get_entry(snap, 2, 0, &entry) == XERR_OK);
    assert(strcmp(entry.name, "LWL_00000001.bin") == 0);
    assert(bee_dal_files_get_entry(snap, 2, 1, &entry) == XERR_OK);
    assert(strcmp(entry.name, "PC_00000001.bin") == 0);
    uint8_t chunk[32]; uint16_t length = 0;
    assert(bee_dal_files_read_chunk(snap, 1, 0, 0, sizeof(chunk), chunk, &length) == XERR_OK);
    assert(length == 4 && !memcmp(chunk, "png\n", 4));
    assert(bee_dal_files_delete(snap, 0, 0, &logs, &images, &data) == XERR_STATE);
    assert(bee_dal_files_mark(snap, 1, 0, 7) == XERR_OK);
    assert(bee_dal_files_scan(7, &snap, &logs, &images, &data) == XERR_OK);
    assert(logs == 0 && images == 1 && data == 0);
    snprintf(path, sizeof(path), "rm -rf %s", root);
    assert(system(path) == 0);
    puts("Three-category file listing and chunk read: OK");
    return 0;
}
