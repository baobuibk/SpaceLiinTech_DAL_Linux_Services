#ifndef EXP_DATA_SERVICE_H
#define EXP_DATA_SERVICE_H

#include <stddef.h>

/* Downloads EXP table-6 objects over the MCU UART CLI and archives them locally. */
int exp_data_service_start(void);
void exp_data_service_stop(void);
/* Latest file fully saved by the UART collector, or JSON null. */
int exp_data_last_saved_json(char *output, size_t output_size);

#endif
