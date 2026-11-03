#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Copy the periodically refreshed PDU identity cache. */
int gui_pdu_read_version(uint8_t version[3], uint32_t *build_code);

/* Cached PDU VOUT read; a write uses Modbus holding register 40001;
 * PDU firmware powers TRANS before it accesses the buck-boost IC. */
typedef struct {
    uint16_t setpoint_mv;
    uint16_t voltage_mv;
    uint16_t current_ma;
    uint16_t flags;
} gui_pdu_vout_t;
#define GUI_PDU_VOUT_TRANS_ON    0x0001U
#define GUI_PDU_VOUT_ENABLED     0x0002U
#define GUI_PDU_VOUT_SET_VALID   0x0004U
#define GUI_PDU_VOUT_VOLT_VALID  0x0008U
#define GUI_PDU_VOUT_CURR_VALID  0x0010U
int gui_pdu_vout(bool set, uint16_t target_mv, gui_pdu_vout_t *result);

/* PDU VOUT (bit 0) and TRANS (bit 1) Modbus coils; zero select reads the cache. */
int gui_pdu_power(uint16_t select, uint16_t on, uint16_t *state,
                  uint16_t *known, uint16_t *failed);
/* Clear the PDU's latched eFuse faults via Modbus coil 00003. */
int gui_pdu_fault_clear(void);
/* Heartbeat coil 00010 and 32-bit on/off periods at holdings 40020..40023. */
int gui_pdu_heartbeat(uint8_t action, uint32_t period_ms,
                      uint8_t *enabled, uint32_t *on_ms, uint32_t *off_ms);
/* Write the PDU bootloader key; the MCU resets shortly after acknowledging. */
int gui_pdu_enter_bootloader(void);

typedef struct {
    uint16_t vout_mv, vout_ma, trans_mv, trans_ma, input_12v_mv, input_5v_mv;
    int16_t temp_tenth;
    bool vout_on, trans_on;
} gui_pdu_status_t;
/* Copy a Modbus sample no older than 15 seconds. */
int gui_pdu_status(gui_pdu_status_t *result);

/* Local console JSON view for the terminal GUI. Returns the number of bytes. */
size_t gui_state_json(char *out, size_t capacity);
void gui_state_start(void);
void gui_state_stop(void);
