#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int gui_hw_main(int argc, char **argv);
int gui_hw_set_led(unsigned duty);
int gui_hw_set_mipi_power(unsigned board, bool on);
int gui_hw_route_mipi(unsigned board, unsigned sensor);
int gui_hw_capture_mipi(unsigned board, unsigned sensor, char *saved_path,
                        size_t path_capacity, uint64_t *saved_bytes);
