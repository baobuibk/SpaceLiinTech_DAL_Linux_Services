#pragma once

#include "services/exp_csp_can.h"

int exp_uart_getall_sensors(HkExpSensors *out);
int exp_uart_getall_thermal(HkExpThermal *out);
int exp_uart_getall_inventory(HkExpInventory *out);
int exp_uart_getall_icd_status(HkExpIcdStatus *out);
int exp_uart_getall_identity(HkExpIdentity *out);
