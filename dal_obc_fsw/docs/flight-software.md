# DAL OBC flight software: commands and interfaces

`dal_obc_fsw` runs the OBC flight command dispatcher. It serves the binary
RS422 link and a local Unix socket in one process. Both interfaces use the
same command handlers and file selection state. The `dal_flight_console`
program is a client for the local socket; it is not a separate service.
The source protocol is transcribed in the [editable BEE-DAL ICD page](icd.md);
this page describes what the current flight software implements.

## Interfaces and data flow

| Interface | Location | Format | Purpose |
| --- | --- | --- | --- |
| RS422 | `/dev/ttyS1`, 921600 baud, 8N1, no flow control | BEE-DAL binary ICD frames | Commands and telemetry from the external controller |
| Local console socket | `/run/dal/dal_obc_fsw/console.sock` | One newline-terminated text command per connection; reply text then connection closes | Commands from the OBC itself |
| Console client | `dal_flight_console` | Interactive text, one-shot text, or optional JSON | Human operation and local scripts |
| Housekeeping input | `/run/dal/dal_obc_fsw/obc_data.json` | Schema version 1 JSON snapshot | Latest OBC sensor values |
| GUI telemetry | Local console socket, `gui_state` | JSON object with OBC, EXP, PDU and board status | Terminal GUI read path |
| EXP UART0 CLI | Set `DAL_EXP_UART_PORT`; 250000 baud by default | Text commands and schema-2 `tlm_json` | EXP control and telemetry through flight software |
| File backend | `/home/bee/data/` | Daily CSV files and legacy HK logs | ICD file listing and chunk transfer |

The CSP worker caches EXP identity from RPARAM table 0 and compact actuator
status from table 12. The EXP data worker watches table 6 and uses the MCU
UART CLI's `data_ready_collect`, `data_read`, and `data_ready_done` commands
to archive objects. It verifies length and CRC-32 before acknowledging the
MCU. The default archive root is `/home/bee/data` (override with
`DAL_EXP_DATA_DIR`); thermal camera frames are saved in `images/exp`, PC logs
in `logs/pc`, and LWL data in `logs/lwl`. Use the same root for `DAL_DATA_DIR`
to make those files visible to `dat_*`.

With CSP disabled, the UART worker caches legacy compact status only. Periodic
EXP UART `tlm_json` GUI cache updates require `DAL_EXP_UART_POLL=1` in
`/etc/default/dal-obc-fsw`. On-demand `exp_run`, `exp_ping`, and `exp_tlm`
requests still access EXP UART; `sys_info` uses the identity cache.

The local socket has mode `0600`, so the console requires root access by
default. `temp_service` and `bme_service` publish their measurements;
The flight software OBC collector reads those and PAC1934 into the OBC snapshot.
The flight software polls that snapshot once per second and keeps the most
recent valid result in memory. A snapshot older than 30 seconds is rejected.
When no valid snapshot is available, `tm_get` and `tm_obc_get` return ICD data
error `0x01`. The separate [`dal` CLI](dal-commands.md) reads the snapshot
directly and offers field-level JSON queries.

RS422 and the local console serialize calls through the same dispatcher.
File selection is shared between them: finish a `dat_sel`/`dat_get` sequence
before starting a file operation on the other interface. A long-running
`shell` command also delays other commands while it executes. GUI `gui_shell`
requests run in separate socket workers so telemetry remains responsive
during a camera capture.

## Use the local console

On the OBC:

```sh
dal_flight_console
```

At `flight>`, type `help` to display the command list. Up/Down browse the
last 32 commands; Left/Right move the cursor; Backspace and Delete edit the
line. `exit` and `quit` close the client. `json on` and `json off` change the
response format for subsequent interactive commands.

Run one command without entering the prompt:

```sh
dal_flight_console ping
dal_flight_console tm_obc_get
dal_flight_console --json tm_obc_get
dal_flight_console --json exp_tlm sensors
dal_flight_console exp_run pc_status
dal_flight_console sen_bme_cfg EXP get
dal_flight_console sen_bme_cfg EXP set 2x 4x 1x 3
dal_flight_console sen_bme_cfg EXP result 17
dal_flight_console sen_imu_cfg EXP get
dal_flight_console sen_imu_cfg EXP set accel 208Hz 8g
dal_flight_console sen_imu_cfg EXP result 18
dal_flight_console sen_stat OBC get bme
dal_flight_console sen_stat EXP get all cursor 8
dal_flight_console sen_stat PDU get temperature
dal_flight_console uvc_cfg 2 get
dal_flight_console uvc_cfg 2 set format MJPG resolution 1920x1080 fps 30
dal_flight_console uvc_ctl 2 capture 1042
dal_flight_console uvc_stat 2 get 1042
dal_flight_console shell ls -l /home/root
```

Client options must precede the command. `--json` requests a JSON response;
`--socket PATH` selects another socket path; `--help` prints client usage.
All numeric command arguments accept decimal or `0x` hexadecimal.

The socket protocol is also available to a local application: connect to
the socket, send a command followed by `\n`, and read until EOF. Prefix the
command with `json ` to receive JSON. The server handles one request per
connection. Use `dal_flight_console` when possible so the client handles the
socket exchange and interactive input.

### Flight commands

The [flight command register](flight-command-register.md) maps every supported
console command to its RS-422 TYPE/ADDRESS, binary TC payload, and TM reply.
It also lists console-only operations such as `gui_state` and `gui_shell`.

Thermal (`thm_*`), fluidic (`fld_*`), RBA motor (`rba_*`),
PC experiment (`pc_*`) and thermal-profile (`thp_*`) commands are bridged to the EXP over
its UART CLI; the register lists each TYPE/ADDRESS.

File types are `0` = HK housekeeping (`logs/hk`), `1` = SCI1 images, and `2` = SCI2 LWL/PC logs (`logs/lwl`, `logs/pc`). File indexes are
zero-based and fit in a U16. `chunk_size` is 1–32768 bytes; `dat_get` uses a
zero-based U32 chunk index. `dat_mark` accepts 0–254; `0xFF` is reserved.
`dat_lst` returns counts, so select files by type and index to obtain names.
The logs list includes `HK0_YYYYMMDD.csv`, `HK1_YYYYMMDD.csv`, LWL logs,
and PC experiment logs. See the [storage layout](icd.md#current-three-category-storage-layout).

### Local GUI requests

`obc_gui` uses the same flight socket for all telemetry and commands. It
requests `gui_state` once per second; the response contains `obc`, `exp`,
`exp_uart`, `pdu`, `gpio`, `devices`, `rtc`, `mipi`, and `camera_led_duty_pct`. OBC values
come from the flight software's validated housekeeping cache. Flight software
reads the separate CAN EXP snapshot, reads GPIO/RTC/MIPI/PWM state, and polls
PDU Modbus. It also polls EXP UART `tlm_json` system/sensors/power when
`DAL_EXP_UART_POLL=1` is configured.
An unavailable source is `null`, while GPIO entries can be `true`, `false`,
or `null` when readback fails.

The GUI sends RTC edits through `sys_rtc` and EXP requests through
`exp_ping`, `exp_tlm`, and `exp_run`. Other OBC board actions use local
`gui_shell <timeout_seconds> <command>` requests; their JSON replies contain
`ok`, `exit`, `timeout`, and `text`. Flight software executes `usb_cam` and
its own `--gui-hw` helper for the corresponding controls. That helper
implements device tree GPIO power control with readback, MIPI camera
power/route/capture, and CAMERA_LED PWM control. These
OBC hardware helper requests are local socket operations. The EXP requests
have dedicated RS-422 extensions `02/0A`–`02/0C`. The
[GUI command map](exp-gui-command-map.md) lists every device control.

`shell` accepts at most 2048 command bytes and captures at most 20480 output
bytes. Its reply reports whether output was truncated. Each command starts a
new shell, so `cd` does not persist between calls. `debug_handoff yes` ends
the flight software process after acknowledging the command; the systemd unit
uses `Restart=on-failure`, so a clean handoff does not restart it.

`raw` can address entries without a named client command. The original ICD
experiment controls `02/00`–`02/04` are routed to EXP UART handlers; `02/05`–
`02/09`, SFU (`05`), and telemetry config (`04/01`) remain unrouted. The EXP
UART gateway extensions `02/0A`–`02/0C` provide `exp_ping`, `exp_tlm`, and
`exp_run`. Sending an unrouted address with `raw` does not add support; see
the [architecture coverage table](flight-software-architecture.md#revision-10-icd-coverage).

`sen_bme_cfg EXP get|set|result` is a readable EXP BME console command
through `exp_run` (`02/0C`). Use the actual request ID returned by `set`
when polling `result`. The [ICD BME section](icd.md#sen_bme_cfg-0x05)
distinguishes this route from the printed, unimplemented `01/05` frame.
`sen_imu_cfg EXP get|set|reset|result` likewise uses `02/0C`; its
[ICD section](icd.md#sen_imu_cfg-0x06) lists the supported rates,
scales, thresholds, and printed `01/06` frame.

`sen_stat <board> get <group> [source] [cursor n]` uses the implemented
`01/07` RS-422 frame. Each reply carries up to eight sensor records; use
`next_cursor` to read the next page. The [ICD layout](icd.md#current-sen_stat-implementation-type-0x01-addr-0x07)
defines source IDs, units, validity flags, and unavailable camera/limit status.

`uvc_cfg <1|2|3|4> get|set ...` selects an OBC USB camera and reads or
updates its saved V4L2 profile and per-camera `led_pwm` duty (0–100%). The [ICD layout](icd.md#current-uvc_cfg-implementation-type-0x01-addr-0x10)
defines all 32 bytes, setting names, and the one-byte camera selector for get.

`uvc_ctl` applies the selected camera’s saved LED duty before capturing one JPEG, turns the LED off afterward, and `uvc_stat` reports completion. Images are
saved under `/home/bee/data/images/usb/USBn_NNNNNNNN.jpg` with camera `n=0..3`. The [ICD layout](icd.md#current-uvc_ctl-and-uvc_stat-implementation)
defines their RS-422 frames and status codes.

`pdu_vout get` reads the PDU VOUT state, and `pdu_vout set <mV>` writes
800–20000 mV to the PDU. Both use ICD TYPE/ADDR `01/02` (`pwr_adj_cfg`).
The two-byte set payload and eight-byte reply are defined in the
[ICD mapping](icd-console-mapping.md#pwr_adj_cfg-0x02). The PDU turns TRANS
on if necessary before accessing the TPS55287.

The unified `pwr_en` command accepts a board and readable output name: for
example, `pwr_en OBC AI_power off`, `pwr_en EXP MOTOR on`, and
`pwr_en PDU VOUT on`. `pwr_en <board> get` reads the selected board's control
states. For several outputs use `pwr_en <board> set <select_mask> <on_mask>`.
EXP accepts `force` only for off requests. All forms use TYPE/ADDR `01/03`:
the board ID is `03` OBC, `04` EXP, or `05` PDU. The get payload is one
byte; the set payload is six bytes; the reply is seven bytes. See the
[ICD power tables](icd-console-mapping.md#unified-power-enable-command-current-flight-software).
VOUT requires a configured setpoint; `pwr_en PDU TRANS off` also turns VOUT off.

`pdu_fault_clear` clears the PDU's latched eFuse faults through Modbus coil
`00003` (ICD extension `01/1C`). The PDU rejects the operation while a live
PC4 or PC0 fault remains. An accepted reply contains one zero byte; a Modbus
failure returns flight error `03`.

`pdu_heartbeat get` reads the PDU LED enable state and its on/off periods.
`pdu_heartbeat enable on|off`, `pdu_heartbeat on <ms>`, and
`pdu_heartbeat off <ms>` change one setting at a time; periods are 1–60000 ms.
All forms use extension `01/1D`. `pdu_bootloader enter` uses `01/1E` and
writes key `0xB007` to PDU holding register `40060`. The PDU acknowledges,
then turns off TRANS and VOUT and resets into its bootloader. Its application
returns after the bootloader timeout if no update begins.

`pwr_stat <board> get` returns all power measurements and switch states for one board.
Add a source name for one record, or `mask <mask>` for several. TYPE/ADDR is `01/04`;
the three-byte request is board U8 plus source mask U16 LE. See the
[ICD status layout](icd-console-mapping.md#pwr_stat-current-flight-software-0104).

`sys_rtc` with no argument reads the Linux system epoch using a zero-length
TC payload and a four-byte U32 little-endian TM payload. `sys_rtc <epoch>`
sets the system clock and attempts to update `/dev/rtc2`. In the OBC local
console, `sys_rtc now` uses the OBC's current time; in the PC RS-422 client,
`sys_rtc now` uses the PC's current time. The [ICD mapping](icd-console-mapping.md#sys_rtc_cfg-0x01)
lists every frame field and an example.

`sys_info` reads the cached PDU identity registers (holding 40100–40109) and
EXP RPARAM table 0/12 identity. Table 0 is read at startup, after an MCU reboot,
or by `exp_csp_refresh 0`; no periodic UART `version` request is sent. The OBC
identity comes from flight software constants. A missing or stale EXP cache
reports that board offline. OBC remains online while flight software is running.

The 96-byte TM payload has a 12-byte header and three 28-byte records in
PDU, EXP, OBC order. Header byte 0 is schema version 1; byte 1 is board count
3; byte 2 is the online bit mask (bit 0 PDU, bit 1 EXP, bit 2 OBC); byte 3 is
reserved; bytes 4–7 are the little-endian Unix sample time; bytes 8–11 are
reserved. Each record holds board ID (1 PDU, 2 EXP, 3 OBC), online flag,
hardware major/minor/patch, firmware major/minor/patch, then a 20-byte
zero-padded ASCII build ID. An offline board uses `FF FF FF` for firmware and
an empty build ID. The PDU build code is eight hexadecimal digits; EXP uses
the build ID in RPARAM table 12. The OBC build ID is empty.

### Text and JSON replies

Text replies start with `OK` or `ERROR`, then show decoded information and
the exact payload as hexadecimal where applicable. For example:

```text
flight> sys_info
OK sys_info: PDU offline hardware=1.0.0
EXP online hardware=1.0.0 firmware=1.0.0 build=4dc7bbb-dirty
OBC online hardware=1.0.0 firmware=0.1.0
payload: 0103060040c87e67000000000100010000ffffff00000000000000000000000000000000000000000201010000010000346463376262622d64697274790000000000000003010100000001000000000000000000000000000000000000000000
```

`--json` returns one JSON object. This example shows the field names; uptime
and its payload change with the board:

```json
{"ok":true,"command":"ping","type":0,"address":0,"ack":0,"code":0,"length":4,"text":"uptime=123s","payload_hex":"7b000000"}
```

`ok` means the ICD handler acknowledged the command. For `shell`, inspect the
exit code in `text` or decode the first seven payload bytes as described
below; a shell command can exit nonzero even when `ok` is `true`. For
`tm_obc_get`, `text` contains the decoded sensor values and `payload_hex`
contains the 64-byte wire payload. Invalid sensor values are zero on the wire
and must be interpreted with the validity mask.

| JSON field | Meaning |
| --- | --- |
| `ok` | `false` for an ICD error reply; `true` for an ACK. |
| `command` | The submitted local command string. |
| `type`, `address` | Requested ICD TYPE and ADDR. |
| `ack`, `code` | Reply ACK/ERR and ADDR/ERRCODE bytes. For errors, `ack=255` and `code` is the ICD error code. |
| `length` | Reply payload length in bytes. |
| `text` | Human-readable interpretation of the reply. |
| `payload_hex` | Raw reply payload as lowercase hexadecimal. |

ICD error codes are `0x00` invalid TC, `0x01` data error, `0x02` memory
error, and `0x03` unknown operation. Malformed local command syntax has a
separate `{"ok":false,"error":"invalid command or arguments"}` response.

## RS422 binary ICD

The UART carries binary frames. Plain text typed into a serial terminal is
not a flight command. The PC test client lives at
`service/dal_obc_fsw/tool/pc_client/bee_dal_repl.py` and requires `pyserial`.

```sh
python3 service/dal_obc_fsw/tool/pc_client/bee_dal_repl.py /dev/ttyUSB0
```

TC and TM frames use these fields:

| Position | TC | TM |
| --- | --- | --- |
| 0 | Start byte `0xA5` | Start byte `0xA5` |
| 1 | TYPE | ACK/ERR: echoed TYPE, `0xFE` for debug ACK, or `0xFF` for error |
| 2 | ADDR | Echoed ADDR or error code |
| 3–4 | Payload LENGTH, U16 little-endian | Payload LENGTH, U16 little-endian |
| 5 onward | Payload | Payload |
| Final 2 bytes | CRC16, little-endian | CRC16, little-endian |

The CRC is CRC-16/CCITT with polynomial `0x1021` and initial value `0xFFFF`,
calculated over **payload bytes only**. A zero-length payload therefore has
CRC `0xFFFF`. The protocol payload limit is 32768 bytes. Invalid CRC frames
are discarded. The decoder resynchronizes after a 2000 ms inter-byte timeout
(checked in 200 ms ticks, so a stalled frame is dropped after 2.0–2.4 s).

The debug shell ACK (`0xFE/0x01`) payload starts with U8 process status
(`0` exited, `1` signaled, `3` execution error), U8 exit code or signal,
U8 truncation flag, and U32 output length, followed by the captured output.

### OBC housekeeping payload (`0x04/0x02`)

This command has an empty TC payload. Its 64-byte TM payload is little-endian:

| Offset | Type | Field | Units or validity |
| ---: | --- | --- | --- |
| 0 | U16 | Validity mask | Bit 0 onboard temperature; bits 1–4 NTC1–4; bit 5 BME temperature/pressure/humidity; bit 6 gas; bit 7 PAC U6; bit 8 PAC U2 |
| 2 | U32 | Snapshot time | Unix seconds |
| 6 | U32 | Boot count | Boots |
| 10 | I16 | Onboard temperature | °C × 100 |
| 12 | I16[4] | NTC1–4 temperatures | °C × 100 |
| 20 | I16 | BME temperature | °C × 100 |
| 22 | U32 | BME pressure | Pa |
| 26 | U16 | BME humidity | %RH × 100 |
| 28 | U32 | BME gas resistance | Ω |
| 32 | U16[8] | U6 channels 1–4, then U2 channels 1–4 voltage | mV |
| 48 | I16[8] | U6 channels 1–4, then U2 channels 1–4 current | mA |

For example, validity mask `0x0061` means onboard temperature, BME, and gas
are valid; all NTC and PAC channels are invalid. The console displays zero
for invalid fields and adds `(invalid)` where supported. Verify the mask
before using a numeric value.

## File transfer sequence

For a local inspection of an HK file:

```text
flight> dat_lst 0xFF
flight> dat_sel 0 0 1024
flight> dat_get 0
flight> dat_get 1
flight> dat_des
```

The binary RS422 peer uses the same command sequence and parses `dat_sel` to
obtain file size, CRC32, and chunk count. It then requests every chunk and
checks the result against the advertised CRC32. Daily CSV files may still be
growing during transfer; retry if the size or CRC changes.

## Service checks

```sh
make -C /home/root/deploy status
dal_flight_console ping
dal_flight_console tm_obc_get
cat /run/dal/dal_obc_fsw/obc_data.json
journalctl -u dal_obc_fsw.service -b --no-pager
```

Xem [hướng dẫn log flight software](flight-software-logs.md) để theo dõi log
trực tiếp, lọc lệnh/phản hồi và chỉnh chế độ debug của service trên board.

The flight software cannot make an invalid upstream PAC or NTC reading
valid. Check the snapshot flags and the sensor service logs when the
housekeeping validity mask has missing bits.

MIPI capture through the local `--gui-hw mipi-capture` path retains the 5120×3840 BG10 RAW frame and converts it to a full-resolution PNG in `/home/bee/data/images/mipi/`. Both files share a `MIPI{board-1}.{sensor}_H_NNNNNN` stem. The final `Saved` line reports the PNG path; set `DAL_GUI_MIPI_PNG` to override the `raw_png` executable in tests.
