#include "hk_csv.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    char root[] = "/tmp/dal-hk-csv-XXXXXX";
    assert(mkdtemp(root));
    HkCsv csv;
    hk_csv_init(&csv, root, "HK0", 60);
    time_t now = 1767268800; /* 2026-01-01 12:00 UTC */
    assert(hk_csv_append(&csv, "ts,value", now, "first") == 0);
    assert(hk_csv_append(&csv, "ts,value", now + 5, "skipped") == 0);
    assert(hk_csv_append(&csv, "ts,value", now + 60, "second") == 0);
    hk_csv_close(&csv);

    struct tm utc;
    gmtime_r(&now, &utc);
    char date[9], dir[256], path[512], line[100];
    assert(strftime(date, sizeof(date), "%Y%m%d", &utc) == 8);
    snprintf(dir, sizeof(dir), "%s", root);
    snprintf(path, sizeof(path), "%s/HK0_%s.csv", dir, date);
    FILE *f = fopen(path, "r");
    assert(f);
    assert(fgets(line, sizeof(line), f) && strcmp(line, "ts,value\n") == 0);
    assert(fgets(line, sizeof(line), f) && strstr(line, "first"));
    assert(fgets(line, sizeof(line), f) && strstr(line, "second"));
    assert(fgets(line, sizeof(line), f) == NULL);
    fclose(f);
    assert(unlink(path) == 0);
    assert(rmdir(root) == 0);
    puts("CSV history interval fixture passed");
    return 0;
}
