#include "services/port_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char path[] = "/tmp/dal-port-config-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs("# board settings\nUNRELATED=kept\nDAL_GUI_PDU_PORT=/dev/ttyS3\n", file);
    assert(fclose(file) == 0);
    assert(setenv("DAL_OBC_PORT_CONFIG_FILE", path, 1) == 0);

    char out[2048], error[256];
    assert(port_config_get(out, sizeof(out)) == 0);
    assert(strstr(out, "DAL_EXP_UART_PORT=/dev/ttyS4\n"));
    assert(port_config_set("DAL_EXP_UART_PORT", "/dev/ttyS5", error, sizeof(error)) == 0);
    assert(port_config_set("DAL_EXP_CSP_RS485_PORT", "/dev/ttyS3", error, sizeof(error)) != 0);
    assert(port_config_set("DAL_GUI_PDU_UNIT", "248", error, sizeof(error)) != 0);
    assert(port_config_set("DAL_EXP_CSP_RS485_BAUD", "1000001", error, sizeof(error)) != 0);
    assert(port_config_set("DAL_EXP_CSP_RS485_ADDRESS", "31", error, sizeof(error)) != 0);
    assert(port_config_set("DAL_GUI_PDU_UNIT", "7", error, sizeof(error)) == 0);
    assert(port_config_set("DAL_EXP_UART_PORT", "/dev/ttyS6;bad", error, sizeof(error)) != 0);
    assert(port_config_get(out, sizeof(out)) == 0);
    assert(strstr(out, "DAL_EXP_UART_PORT=/dev/ttyS5\n"));
    assert(strstr(out, "DAL_GUI_PDU_UNIT=7\n"));
    file = fopen(path, "r");
    assert(file);
    char contents[2048] = "";
    assert(fread(contents, 1, sizeof(contents) - 1, file) > 0);
    fclose(file);
    assert(strstr(contents, "UNRELATED=kept\n"));
    unlink(path);
    puts("port config OK");
    return 0;
}
