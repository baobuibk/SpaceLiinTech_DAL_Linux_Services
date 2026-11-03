# GUI panels and MCU command review

The SSH terminal frontend keeps six section tabs and twenty panels in
`src/tui_catalog.rs`; the twenty-page mapping below describes the optional
desktop frontend from which the commands were taken.
Terminal replies are text. Camera heatmaps and BMP conversion need the
desktop frontend, while verified UART2 binary download is available in the
terminal frontend.

Reviewed against the copied GUI under `src/` and the reference MCU source under
`../ref/https-github.com-baobuibk-SpaceLiin_BEE-DAL_EXP_V1.0_FW/src/`.
The MCU command registry is
`dev/M1_SysApp/EmbeddedCLI/CLI_Command/cli_command.c` (its
`cliStaticBindings_internal` table); the corresponding `cli_*.c` handlers and
`Bubble/bubble_cli.c` supply command behavior. This is a source review, not an
electrical or live UART test.

## Control path

All peripheral buttons call `send` on the selected link. `main.rs` logs QUEUE,
then its worker logs TX after a successful serial write and RX on reply. QUEUE
or TX alone does not show MCU acceptance. The port settings page opens one
serial device per link. The separate console editors send on their own link.

| GUI link | Wire protocol | MCU side |
| --- | --- | --- |
| Console, UART2, 115200 | CLI text plus CR; XMODEM for bulk image download | `app_cli.c`, `cli_command.c`, `cli_xmodem_collect.c` |
| XCLI, UART0, 230400 | xTP/xCLI `cli <command>`; polls `obc_result` for queued remote work | `app_xtp.c`, `xcli_commands.c`, `obc_xtp.c` |
| RS485, UART4, 115200 | CSP v1 KISS, RPARAM table 1 shell; source 2, destination 11, port 7 | `sh_handler.c`, `dal_rparam_rows.inc` |
| USB-CAN SLCAN serial adapter | SLCAN 1 Mbit/s, extended CFP/CSP, RPARAM table 1 shell; source 1 | `sh_handler.c`, CSP/CAN transport |

The Home RPARAM page reads table IDs 0-10. Firmware has those tables plus a
table 11 definition, which this GUI does not offer. Remote shell traffic is
serialized while a request is pending. UART2 is required for XMODEM; xTP and
RPARAM do not provide GUI bulk image transfer.

## Every page

| Section / page | GUI action and firmware match |
| --- | --- |
| Home / Overview | `help`, `power_all_get`, `sensor_status`, `rs485_status`, `pc_status`; all registered in `cli_command.c`. Read only. |
| Home / RPARAM tables | GET table 0-10 via RS485 or CAN. Table 1 remote shell is implemented by `sh_handler.c`; it is also how peripheral commands use those links. |
| Power / Switched rails | Read and switch SLN/HD, MOTOR, and 12V_EXP via `power_*_{get,on,off}`. `cli_power.c` controls commanded GPIO state; GUI does not measure voltage. Owner checks can refuse OFF. |
| Power / PAC1934 | `pac1934_detect` plus `pac1934_result` polling; `pac1934_read` returns cached four channel V/I/P. `cli_pac1934.c` agrees. |
| Pumps / U11 single pump | `pump_type_get/set`, `pump_detect/init`, `pump_set_freq/volt`, `pump_enable/disable`, `pump_state/status`. `cli_pump.c` selects lowdriver or highdriver and controls U11; type should change with SLN/HD off. |
| Pumps / U15 four-channel | `pump4_detect/init`, `pump4_set_freq`, `pump4_set_volt <0-3>`, `pump4_enable/disable`, `pump4_state/status`. Registry shows shared frequency/enable and per-channel voltage. |
| Sensors / NTC | `ntc_read_all` reads NTC 0-3 and board 4 through `cli_ntc.c`; GUI displays raw, validity, and age. |
| Sensors / BME688 | Probe/init and cached measurements (`bme688_*`) match `cli_bme688.c`; reset completion uses `bme688_result`. Reads do not force an I2C sample. |
| Sensors / IMU | `lsm6d_*` probe, configuration and cached reads match `cli_lsm6dsox.c`; request completion uses `lsm6d_result`. MCU documents polling only for interrupts. |
| Sensors / Flow sensors | `flow_init`, `flow_read_all`, `flow_get/status/id`, `flow_result` target indexes 0/1. `cli_flow_sen.c` uses queued operations and cached result readback. |
| Sensors / RTC | `rtc_ping/read`, `rtc_set_date/time` match `cli_rtc.c`; GUI validates entered time and reads back. |
| Actuators / Heaters | `power_exp12v_*`, `heater_set <0-3> <duty> <duration>`, `heater_stop`, `heater_get`, `heater_result`, `ntc_read_all`. `cli_command.c` says manual PWM does **not** check NTCs; owner, duty, duration and power rules still apply. |
| Actuators / Motors / DRV8910 | `drv8910_*`, `motor mode`, `motor5_run/stop/home/status/limit_*` match `cli_motor5.c` and DRV8910 handlers. Jog is continuous until explicit stop; the GUI does not expose all firmware movement/configuration commands. |
| Actuators / Optical limits | `limit_init/get/raw/status/cfg_get/cfg_set` match `cli_fluidic.c` limit handler. The isolated inputs are registered as unavailable because MCU pins are unmapped; GUI uses optical inputs. |
| Actuators / IR LEDs | `ir_cfg_set`, `ir_set`, `ir_off`, `ir_status`, `ir_result` match `cli_ir.c`. Firmware queues configuration and exposures, with bounded duration/duty and one active exposure at a time. |
| Actuators / IR camera 0 | `camera_buffer_*`, `camera_scan/config/address_set/detect/init/capture/status` target index 0. `cli_camera.c` implements them; image metadata/list/download/release uses `cli_data_transfer.c` and UART2 XMODEM. |
| Actuators / IR camera 1 | Same command path with index 1 and separate GUI draft. Firmware data catalogue remains shared; GUI filters image objects by camera source. |
| Actuators / Bubble inputs | `bubble_raw 0/1` matches `Bubble/bubble_cli.c`. Displays raw OUT1/OUT2 GPIO levels, without a qualified bubble decision or interlock. |
| Actuators / Burn wire | `burn_status/cfg_set/arm/fire/abort/clear` match `cli_burn_wire.c`. Firmware reserves the motor domain for a five second arm window; PAC MOTOR values are shared rail measurements. |
| Experiment / PC experiment | Page sends only `pc_status`. `cli_pc_exp.c` registers activation, test, replace, circulation, stop, and configuration commands, but this page has no controls for them. They remain available through the console editor. |

UI indexes are zero based, as in firmware command registration. Printed board
connector names remain physical labels. The GUI's response cards are snapshots:
read/refresh explicitly after an action. Several sensor and actuator commands
return request IDs; the GUI polls where implemented, and the console remains
available for `*_result <id>` inspection.

## OBC differences and limits

- The reference copy pinned a Windows GNU Rust toolchain. This project pins
  host `stable`, and `make target` uses the SDK compiler as Rust's ARMv7 linker.
- The serial library uses its Linux `/sys/class/tty` port scan so the host and
  OBC builds do not require `libudev` development files. A device path can
  also be entered directly in Port settings.
- On Linux, camera downloads use the save folder typed in the camera panel.
  Local BMP conversion uses the typed `.bin` path and writes a sibling `.bmp`.
  Windows keeps its native file dialogs.
- The GUI needs an X11/GL capable runtime, a serial device, and permission to
  open it. Cross compilation verifies the binary, not the OBC display stack or
  MCU behavior. Firmware version and transport configuration must match the
  reviewed source for this mapping to apply.
