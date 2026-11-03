#pragma once
/*
 * hk_xio.h - Query OBC-driven power switches via the xio tool
 *
 * The OBC directly controls EXP power (see `xio list`, pin
 * pow_onoff_5v_exp1). That switch state is the
 * authoritative answer to "does EXP have power right now" —
 * far more precise than inferring it from a shared PAC1934 rail
 * reading. hk_service uses it as the sole gate before pinging a node:
 * if OBC hasn't switched a board's 5V on, that board defaults offline
 * and is never pinged.
 *
 * Example `xio list` output (name column, state column, free-text desc):
 *   pow_onoff_5v_exp1        off   Power 5V EXP1 (PB01)
 *
 * hk_xio_input_active() reads the same tool's INPUTS view for the
 * MCU->MPU data-ready handshake pin (sig_input_exp1). That table's
 * format wasn't confirmed on real hardware at the time this was
 * written -- ASSUMED to be reachable via the same `xio_path
 * input_subcmd` invocation (input_subcmd defaults to "list", i.e. the
 * same call as the power-switch table, on the theory that `xio list`
 * prints both an OUTPUTS and an INPUTS section). The parser looks for
 * a line containing pin_name and treats it as active if that line
 * contains "active" case-insensitively (matches an observed "[ACTIVE]"
 * / "[idle]" bracket style without hard-coding exact casing). If the
 * real command/format differs, override via --xio-input-cmd (no
 * rebuild needed) and, if the label scheme differs too, adjust the
 * match in hk_xio.c.
 */

typedef struct {
    const char *xio_path;
    const char *input_subcmd;  /* argv[1] passed to xio_path for the inputs view */
    int         timeout_ms;
} HkXioCfg;

/*
 * Returns 1 if pin_name's state is "ON", 0 if it is present but not
 * "ON" (e.g. "off"), -1 if xio could not be run or pin_name was not
 * found in its output at all.
 */
int hk_xio_pin_state(const HkXioCfg *cfg, const char *pin_name);

/*
 * Returns 1 if pin_name's row indicates an active/high input, 0 if
 * present but idle/low, -1 if xio could not be run or pin_name was
 * not found at all. See the ASSUMED note above.
 */
int hk_xio_input_active(const HkXioCfg *cfg, const char *pin_name);
