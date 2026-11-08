"""Implemented RS-422 commands (TYPE, ADDR) -> console name. Single source of truth for the
sweep/coverage/drift tests. Keep in step with dispatch() in src/services/bee_dal_tmtc_service.c;
test_inventory.py fails when the source and this table disagree."""

IMPLEMENTED = {
    (0x00, 0x00): "ping",
    (0x01, 0x00): "sys_info", (0x01, 0x01): "sys_rtc", (0x01, 0x02): "pdu_vout",
    (0x01, 0x05): "sen_cfg", (0x01, 0x06): "sen_stat",
    (0x01, 0x03): "pwr_en", (0x01, 0x04): "pwr_stat", (0x01, 0x07): "sen_stat",
    (0x01, 0x10): "uvc_cfg", (0x01, 0x11): "uvc_ctl", (0x01, 0x12): "uvc_stat",
    (0x01, 0x13): "thm_cfg", (0x01, 0x14): "thm_ctl", (0x01, 0x15): "thm_stat",
    (0x01, 0x16): "fld_cfg", (0x01, 0x17): "fld_ctl", (0x01, 0x18): "fld_stat",
    (0x01, 0x1A): "rba_ctl", (0x01, 0x1B): "rba_stat", (0x01, 0x1C): "pdu_fault_clear",
    (0x01, 0x1D): "pdu_heartbeat", (0x01, 0x1E): "pdu_bootloader",
    (0x01, 0x30): "uvc_cfg", (0x01, 0x31): "uvc_stat", (0x01, 0x32): "pwr_en",
    (0x01, 0x33): "rba_job", (0x01, 0x34): "fld_job", (0x01, 0x35): "uvc_job",
    (0x01, 0x36): "fld_cfg", (0x02, 0x10): "thp_cfg", (0x02, 0x12): "pc_cfg",
    (0x02, 0x0F): "exp_csp_snapshot",
    (0x02, 0x00): "pc_stat", (0x02, 0x01): "pc_cfg", (0x02, 0x02): "pc_ctl", (0x02, 0x03): "thp_cfg",
    (0x02, 0x04): "thp_ctl",
    (0x02, 0x0A): "exp_ping", (0x02, 0x0B): "exp_tlm", (0x02, 0x0C): "exp_run",
    (0x02, 0x0D): "exp_csp_run", (0x02, 0x0E): "exp_csp_result",
    (0x03, 0x00): "dat_lst", (0x03, 0x01): "dat_sel", (0x03, 0x02): "dat_get",
    (0x03, 0x03): "dat_des", (0x03, 0x04): "dat_del", (0x03, 0x05): "dat_mark",
    (0x04, 0x00): "tm_get", (0x04, 0x02): "tm_obc_get",
    (0xFF, 0x00): "debug_handoff", (0xFF, 0x01): "shell",
}

# Console-only names (no TYPE/ADDR of their own).
CONSOLE_ONLY = ["help", "gui_state", "gui_shell", "raw"]
# Console routes that ride on an existing wire command.
CONSOLE_ALIASES = {"sen_bme_cfg": (0x02, 0x0C), "sen_imu_cfg": (0x02, 0x0C),
                   "burn_cfg_set": (0x02, 0x0C),
                   "pwr_adj_cfg": (0x01, 0x02)}

# A zero-length TC must be rejected with invalid-TC (0x00) by these.
NEEDS_PAYLOAD = {(1, a) for a in (4, 6, 7, 0x11, 0x14, 0x17, 0x1a, 0x30, 0x31,
                                  0x32, 0x33, 0x34, 0x35)} | {
    (2, 2), (2, 0x0b), (2, 0x0c), (2, 0x0d), (2, 0x0e), (2, 0x0f), (2, 0x10),
    (3, 0), (3, 1), (3, 2), (3, 4), (3, 5), (0xff, 1)}


def unimplemented_error(type_, addr):
    """Error code the dispatcher returns for an address that is not implemented."""
    if (type_, addr) in IMPLEMENTED:
        raise ValueError("implemented")
    if type_ in (0x01, 0x02, 0x05):
        return 0x03
    if type_ == 0x04 and addr == 0x01:   # tm_cfg is explicitly "unknown"
        return 0x03
    return 0x00                          # falls through to invalid-TC
