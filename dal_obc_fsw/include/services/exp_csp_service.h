#ifndef EXP_CSP_SERVICE_H
#define EXP_CSP_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include "services/exp_csp_can.h"

/* All CSP I/O is owned by the worker. Submit never waits for CAN. */
typedef enum { EXP_CSP_GET = 1, EXP_CSP_SET = 2, EXP_CSP_RUN = 3,
               EXP_CSP_REFRESH = 4 } exp_csp_op_t;
typedef enum { EXP_CSP_QUEUED = 1, EXP_CSP_RUNNING = 2,
               EXP_CSP_DONE = 3, EXP_CSP_FAILED = 4 } exp_csp_state_t;

int exp_csp_service_start(void);
void exp_csp_service_stop(void);
int exp_csp_submit(exp_csp_op_t op, unsigned table, const char *field,
                   const char *value, uint32_t *ticket);
int exp_csp_result(uint32_t ticket, exp_csp_state_t *state,
                   char *output, size_t output_size);
/* Copy a cached table 0, 2, 3, 6, or 12 snapshot as JSON; never performs CAN I/O. */
int exp_csp_snapshot_json(unsigned table, char *output, size_t output_size);
/* Groups 0..3: THM, FLD, RBA, PC. Fails if the group is stale or invalid. */
int exp_csp_icd_status(unsigned group, uint8_t *output, size_t output_size);
/* Cached MCU identity from table 0, qualified by the current table 12 boot ID. */
int exp_csp_identity(uint8_t firmware[3], uint8_t hardware[3],
                     char *build_id, size_t build_size);
/* Current EXP power rail feedback from the one-second table 2 poll. */
int exp_csp_rail_state(uint16_t *known, uint16_t *on);
/* Fresh table-6 inventory for the UART data collector; no transport I/O. */
int exp_csp_inventory(HkExpInventory *out);

#endif
