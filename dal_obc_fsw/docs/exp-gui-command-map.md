# OBC GUI command to RS-422 mapping

The board's `obc_gui` is one terminal application. Its EXP device catalog was
transcribed from `exp_test_gui` under `service/ref/.../tools/exp_test_gui`.
All EXP control requests go to `dal_obc_fsw` over its local console socket;
the same dispatcher accepts the binary RS-422 counterpart. Flight software
sends the text command on the EXP UART0 CLI and waits for the prompt or a
15-second timeout. A dedicated flight software CSP worker also reads EXP
RPARAM table 2 and handles queued CAN read/write commands.

Set `DAL_EXP_UART_PORT=/dev/ttySx` in `/etc/default/dal-obc-fsw` after
identifying the OBC device wired to EXP UART0. Flight software defaults to
250000 baud; `DAL_EXP_UART_BAUD` can override it. The DAL device tree does
not currently identify a dedicated EXP serial alias, so no port is assumed.
Periodic `tlm_json` requests are disabled by default. Set
`DAL_EXP_UART_POLL=1` in the same file to enable them; on-demand EXP commands
remain available either way.

## EXP transport and response

| GUI action | Console request | RS-422 TC | Reply |
| --- | --- | --- | --- |
| Check EXP UART | `exp_ping` | `02/0A`, empty payload | UART `tlm_json system` text |
| Read EXP sensor telemetry | `exp_tlm sensors` | `02/0B`, ASCII `sensors` | Reassembled schema-2 JSON |
| EXP system/power/other telemetry | `exp_tlm <section>` | `02/0B`, ASCII section | Reassembled schema-2 JSON |
| Free-form EXP command | `exp_run <command>` | `02/0C`, ASCII EXP command | UART CLI response |

Every `02/0A`–`02/0C` TM payload begins with status U8, text length U16 LE,
then text bytes. Status 0 means the UART exchange completed; inspect the EXP
text for a firmware refusal. Status 1 is an I/O or command error, 2 timeout,
3 UART not configured, and 4 oversized output. These addresses are DAL
extensions and are absent from the revision 1.0 PDF.

For each catalog row below, `02/0C` carries the ASCII command in the TC
payload, with no NUL terminator. A trailing space in the TUI command template
opens an editor for channel or value arguments. Device/channel indexes follow
the EXP firmware's zero-based convention. Enter the complete command before
sending. The UART request is limited to 95 bytes.

## EXP device catalog

| EXP test GUI control | EXP command template | Flight console | RS-422 TC |
| --- | --- | --- | --- |
| SYSTEM: firmware help | `help` | `exp_run help` | `02/0C` |
| SYSTEM: RS485 status | `rs485_status` | `exp_run rs485_status` | `02/0C` |
| POWER: read all rails | `power_all_get` | `exp_run power_all_get` | `02/0C` |
| POWER: SLN/HD on | `power_sln_hd_on` | `exp_run power_sln_hd_on` | `02/0C` |
| POWER: SLN/HD off | `power_sln_hd_off` | `exp_run power_sln_hd_off` | `02/0C` |
| POWER: SLN/HD status | `power_sln_hd_get` | `exp_run power_sln_hd_get` | `02/0C` |
| POWER: motor on | `power_motor_on` | `exp_run power_motor_on` | `02/0C` |
| POWER: motor off | `power_motor_off` | `exp_run power_motor_off` | `02/0C` |
| POWER: motor status | `power_motor_get` | `exp_run power_motor_get` | `02/0C` |
| POWER: EXP 12V on | `power_exp12v_on` | `exp_run power_exp12v_on` | `02/0C` |
| POWER: EXP 12V off | `power_exp12v_off` | `exp_run power_exp12v_off` | `02/0C` |
| POWER: EXP 12V status | `power_exp12v_get` | `exp_run power_exp12v_get` | `02/0C` |
| PAC: detect | `pac1934_detect` | `exp_run pac1934_detect` | `02/0C` |
| PAC: read | `pac1934_read` | `exp_run pac1934_read` | `02/0C` |
| PAC: poll result | `pac1934_result <request_id>` | `exp_run pac1934_result <request_id>` | `02/0C` |
| PUMP U11: type read | `pump_type_get` | `exp_run pump_type_get` | `02/0C` |
| PUMP U11: type set | `pump_type_set <lowdriver/highdriver>` | `exp_run pump_type_set <lowdriver/highdriver>` | `02/0C` |
| PUMP U11: detect | `pump_detect` | `exp_run pump_detect` | `02/0C` |
| PUMP U11: initialize | `pump_init` | `exp_run pump_init` | `02/0C` |
| PUMP U11: set frequency | `pump_set_freq <hz>` | `exp_run pump_set_freq <hz>` | `02/0C` |
| PUMP U11: set voltage | `pump_set_volt <vpp>` | `exp_run pump_set_volt <vpp>` | `02/0C` |
| PUMP U11: enable | `pump_enable` | `exp_run pump_enable` | `02/0C` |
| PUMP U11: disable | `pump_disable` | `exp_run pump_disable` | `02/0C` |
| PUMP U11: state | `pump_state` | `exp_run pump_state` | `02/0C` |
| PUMP U11: hardware status | `pump_status` | `exp_run pump_status` | `02/0C` |
| PUMP U15: detect | `pump4_detect` | `exp_run pump4_detect` | `02/0C` |
| PUMP U15: initialize | `pump4_init` | `exp_run pump4_init` | `02/0C` |
| PUMP U15: set frequency | `pump4_set_freq <hz>` | `exp_run pump4_set_freq <hz>` | `02/0C` |
| PUMP U15: set channel voltage | `pump4_set_volt <channel> <vpp>` | `exp_run pump4_set_volt <channel> <vpp>` | `02/0C` |
| PUMP U15: enable | `pump4_enable` | `exp_run pump4_enable` | `02/0C` |
| PUMP U15: disable | `pump4_disable` | `exp_run pump4_disable` | `02/0C` |
| PUMP U15: state | `pump4_state` | `exp_run pump4_state` | `02/0C` |
| PUMP U15: hardware status | `pump4_status` | `exp_run pump4_status` | `02/0C` |
| SENSOR: status | `sensor_status` | `exp_run sensor_status` | `02/0C` |
| SENSOR: NTC read | `ntc_read_all` | `exp_run ntc_read_all` | `02/0C` |
| SENSOR: BME initialize | `bme688_init` | `exp_run bme688_init` | `02/0C` |
| SENSOR: BME result | `bme688_result <request_id>` | `exp_run bme688_result <request_id>` | `02/0C` |
| SENSOR: BME read | `bme688_read_all` | `exp_run bme688_read_all` | `02/0C` |
| SENSOR: IMU probe | `lsm6d_probe` | `exp_run lsm6d_probe` | `02/0C` |
| SENSOR: IMU initialize | `lsm6d_init` | `exp_run lsm6d_init` | `02/0C` |
| SENSOR: IMU result | `lsm6d_result <request_id>` | `exp_run lsm6d_result <request_id>` | `02/0C` |
| SENSOR: IMU read | `lsm6d_read_all` | `exp_run lsm6d_read_all` | `02/0C` |
| SENSOR: flow initialize | `flow_init <sensor>` | `exp_run flow_init <sensor>` | `02/0C` |
| SENSOR: flow read | `flow_read_all <sensor>` | `exp_run flow_read_all <sensor>` | `02/0C` |
| SENSOR: flow result | `flow_result <request_id>` | `exp_run flow_result <request_id>` | `02/0C` |
| SENSOR: RTC detect | `rtc_ping` | `exp_run rtc_ping` | `02/0C` |
| SENSOR: RTC read | `rtc_read` | `exp_run rtc_read` | `02/0C` |
| SENSOR: RTC set | `rtc_set <hour> <minute> <second> <day> <month> <year_2digit>` | `exp_run rtc_set <hour> <minute> <second> <day> <month> <year_2digit>` | `02/0C` |
| HEATER: read | `heater_get` | `exp_run heater_get` | `02/0C` |
| HEATER: set duty/time | `heater_set <channel> <duty_pct> <on_ms>` | `exp_run heater_set <channel> <duty_pct> <on_ms>` | `02/0C` |
| HEATER: stop channel/all | `heater_stop <channel/all>` | `exp_run heater_stop <channel/all>` | `02/0C` |
| HEATER: result | `heater_result <request_id>` | `exp_run heater_result <request_id>` | `02/0C` |
| MOTOR: driver initialize | `drv8910_init` | `exp_run drv8910_init` | `02/0C` |
| MOTOR: driver status | `drv8910_status` | `exp_run drv8910_status` | `02/0C` |
| MOTOR: fault status | `drv8910_fault_get` | `exp_run drv8910_fault_get` | `02/0C` |
| MOTOR: clear faults | `drv8910_fault_clear` | `exp_run drv8910_fault_clear` | `02/0C` |
| MOTOR: save config | `motor config save` | `exp_run motor config save` | `02/0C` |
| MOTOR: mode | `motor mode <motor> <manual/operation>` | `exp_run motor mode <motor> <manual/operation>` | `02/0C` |
| MOTOR: limits set | `motor limits set <motor> <min_limit> <max_limit> <home_min/max>` | `exp_run motor limits set <motor> <min_limit> <max_limit> <home_min/max>` | `02/0C` |
| MOTOR: limit read | `motor5_limit_get <motor>` | `exp_run motor5_limit_get <motor>` | `02/0C` |
| MOTOR: run | `motor5_run <motor> <cw/ccw> <speed_sps>` | `exp_run motor5_run <motor> <cw/ccw> <speed_sps>` | `02/0C` |
| MOTOR: stop | `motor5_stop <motor/all>` | `exp_run motor5_stop <motor/all>` | `02/0C` |
| MOTOR: home | `motor5_home <motor>` | `exp_run motor5_home <motor>` | `02/0C` |
| MOTOR: status | `motor5_status <motor/all>` | `exp_run motor5_status <motor/all>` | `02/0C` |
| MOTOR: result | `motor5_result <request_id>` | `exp_run motor5_result <request_id>` | `02/0C` |
| LIMIT: status | `limit_status all` | `exp_run limit_status all` | `02/0C` |
| LIMIT: raw | `limit_raw all` | `exp_run limit_raw all` | `02/0C` |
| LIMIT: get | `limit_get all` | `exp_run limit_get all` | `02/0C` |
| LIMIT: active level | `limit_cfg_set <channel> active_level <low/high>` | `exp_run limit_cfg_set <channel> active_level <low/high>` | `02/0C` |
| IR: status | `ir_status` | `exp_run ir_status` | `02/0C` |
| IR: configure | `ir_cfg_set <channel> <key> <value>` | `exp_run ir_cfg_set <channel> <key> <value>` | `02/0C` |
| IR: pulse | `ir_set <channel> <duty_pct> <on_ms>` | `exp_run ir_set <channel> <duty_pct> <on_ms>` | `02/0C` |
| IR: stop | `ir_off <channel/all>` | `exp_run ir_off <channel/all>` | `02/0C` |
| IR: result | `ir_result <request_id>` | `exp_run ir_result <request_id>` | `02/0C` |
| CAMERA: detect | `camera_detect <camera>` | `exp_run camera_detect <camera>` | `02/0C` |
| CAMERA: initialize | `camera_init <camera>` | `exp_run camera_init <camera>` | `02/0C` |
| CAMERA: scan | `camera_scan <camera>` | `exp_run camera_scan <camera>` | `02/0C` |
| CAMERA: status | `camera_status <camera>` | `exp_run camera_status <camera>` | `02/0C` |
| CAMERA: configure | `camera_config <camera> <model> <i2c_addr> <encoding> <hz> <emissivity>` | `exp_run camera_config <camera> <model> <i2c_addr> <encoding> <hz> <emissivity>` | `02/0C` |
| CAMERA: set I2C address | `camera_address_set <camera> <i2c_addr>` | `exp_run camera_address_set <camera> <i2c_addr>` | `02/0C` |
| CAMERA: enable buffer | `camera_buffer_enable <camera>` | `exp_run camera_buffer_enable <camera>` | `02/0C` |
| CAMERA: disable buffer | `camera_buffer_disable <camera>` | `exp_run camera_buffer_disable <camera>` | `02/0C` |
| CAMERA: capture | `camera_capture <camera>` | `exp_run camera_capture <camera>` | `02/0C` |
| CAMERA: data inventory | `data_ready_collect` | `exp_run data_ready_collect` | `02/0C` |
| CAMERA: object metadata | `data_ready_collect <object_id>` | `exp_run data_ready_collect <object_id>` | `02/0C` |
| CAMERA: data list | `data_list` | `exp_run data_list` | `02/0C` |
| CAMERA: data list page | `data_list <offset>` | `exp_run data_list <offset>` | `02/0C` |
| BUBBLE: raw | `bubble_raw <sensor>` | `exp_run bubble_raw <sensor>` | `02/0C` |
| BURN: status | `burn_status` | `exp_run burn_status` | `02/0C` |
| BURN: configure | `burn_cfg_set <wire> <key> <value>`; keys `duty_pct`, `burn_time_ms`, `max_total_current_ma` | `burn_cfg_set <wire> <key> <value>` (or `exp_run burn_cfg_set ...`) | `02/0C` |
| BURN: arm | `burn_arm <wire>` | `exp_run burn_arm <wire>` | `02/0C` |
| BURN: fire | `burn_fire` | `exp_run burn_fire` | `02/0C` |
| BURN: abort | `burn_abort` | `exp_run burn_abort` | `02/0C` |
| PC EXPERIMENT: status | `pc_status` | `exp_run pc_status` | `02/0C` |

## OBC actions in the same application

| GUI action | Local flight request | RS-422 counterpart |
| --- | --- | --- |
| Set OBC RTC | `sys_rtc <epoch>` | `01/01`, U32 LE epoch |
| AI/FPGA eFuse, USB camera power, GPIO status, CAMERA_LED PWM, MIPI power/route/capture | Flight `--gui-hw` through local `gui_shell` | `FF/01` debug shell command invoking the same installed flight hardware helper |
| USB camera JPEG capture | `usb_cam` through local `gui_shell` | `FF/01` debug shell command invoking `usb_cam` with camera, dimensions and output path |
| OBC housekeeping | `tm_obc_get` or `gui_state` | `04/02` 64-byte OBC telemetry; `gui_state` itself is local socket only |
| EXP housekeeping | Periodic UART `tlm_json` through flight software | `02/0B` section request |

MIPI camera/sensor selection in the TUI changes a local draft; **Apply route**
or **Capture** sends the selected values. Tab navigation, log scrolling, and
JSON display formatting are local UI operations and have no wire command.

## Remaining GUI operations without a dedicated binary command

| Operation | Current access | Missing work |
| --- | --- | --- |
| EXP image download and heatmap | Original desktop GUI uses UART2 XMODEM; TUI can request capture and inventory over EXP UART0 | Add a separate CAN/RS-485 RPARAM Table 11 collector, store the verified image on the OBC, then expose it through `03/00`–`03/05`. The current file service lists OBC files, not EXP images. |
| Raw CAN RPARAM table read/write | Flight CSP worker publishes the EXP snapshot and handles `02/0D` submissions plus `02/0E` results | Table 2 is read-only; table 5 configuration writes require polling the ticket result. `02/0B` still returns UART `tlm_json`. |
| Combined `gui_state` and PDU snapshot | Local flight socket | No native RS-422 typed PDU or combined GUI-state command. `FF/01` can call the local console as a debug workaround, subject to its output cap. |
| OBC GPIO, USB, MIPI and LED controls | Local flight hardware helper | `FF/01` can invoke the same helper; dedicated binary control addresses are not defined. |

The EXP catalog's 94 text commands all have the working `02/0C` gateway
counterpart. A received prompt means the transport completed; asynchronous
EXP operations can return a request ID and require a result query before
their physical outcome is known.
