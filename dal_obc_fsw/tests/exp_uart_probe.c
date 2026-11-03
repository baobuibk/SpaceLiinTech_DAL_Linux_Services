#include "services/exp_uart_service.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    char output[16384];
    unsigned status = !strcmp(argv[1], "tlm")
        ? exp_uart_telemetry(argv[2], output, sizeof(output))
        : exp_uart_command(argv[2], output, sizeof(output));
    printf("%u\n%s\n", status, output);
    return 0;
}
