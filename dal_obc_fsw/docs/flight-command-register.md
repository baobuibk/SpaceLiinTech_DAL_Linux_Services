# Flight software command register

This page maps the **current `dal_obc_fsw` console commands** to their RS-422 binary frames. It reflects the command parser and dispatcher in `service/dal_obc_fsw`. The [revision 1.0 ICD transcription](icd.md) records the original protocol document; this register records what the current software accepts and returns. Update this page when a flight command or wire payload changes.

See the [ICD v1.2 payload contracts](icd-v1p2-payload-contracts.md) for all native lengths and DAL extension assignments. Native ACKs contain uptime; job IDs are returned only by job extensions.

Native empty GETs also include `uvc_cfg get` (01/10, 32-byte default camera profile), `uvc_stat` (01/12, six-byte aggregate), `thp_cfg get` (02/03, heater 0 profile), `thp_en get` (02/04, one-byte heater 0 status), and `pwr_en get` (01/03, U16 global status). Native power writes use `pwr_en on|off <global_mask> [force]` with a two-byte payload.

Rich jobs: `rba_job` = 01/33, `fld_job` = 01/34, `uvc_job` = 01/35. Selected JSON pages use 01/36 (`fld_cfg get <page>`) and 02/12 (`pc_cfg get <page>`). These are [DAL extensions](icd-v1p2-payload-contracts.md#dal-extensions-outside-icd-command-addresses), separate from the PDF commands.

## How to use this register

On the OBC, `dal_flight_console` sends one text command over the root-only Unix socket at `/run/dal/dal_obc_fsw/console.sock`:

```sh
dal_flight_console ping
dal_flight_console --json tm_obc_get
dal_flight_console dat_sel 0 0 1024
```

On RS-422, send a **binary TC frame**, not the command text. The UART is 921600 baud, 8N1. Integer fields are little-endian. The current decoder accepts up to 32768 payload bytes and has a 2000 ms inter-byte timeout.

| Byte offset | TC field | Size |
| ---: | --- | ---: |
| 0 | START = `0xA5` | 1 |
| 1 | TYPE | 1 |
| 2 | ADDRESS | 1 |
| 3 | LENGTH, little-endian | 2 |
| 5 | Payload | LENGTH |
| 5 + LENGTH | CRC-16, little-endian | 2 |

The current implementation calculates CRC-16 over **payload bytes only** (polynomial `0x1021`, initial value `0xFFFF`, no reflection or final XOR). Thus the `ping` TC is `A5 00 00 00 00 FF FF`. On a normal successful TM, byte 1 echoes TYPE and byte 2 echoes ADDRESS. On an error, byte 1 is `0xFF` and byte 2 is the error code: `0x00` invalid TC, `0x01` data error, `0x02` memory error, or `0x03` unknown error. The debug commands use a distinct successful TM byte `0xFE`.

**TC payload** and **TM payload** lengths below exclude the five-byte header and two-byte CRC. `U16` and `U32` fields use little-endian order. The console accepts decimal or `0x` hexadecimal numeric arguments.

For commands whose TM payload is listed as **4-byte uptime ACK**, the complete frame is documented in the [TM ACK package table](icd-console-mapping.md#tm-ack-package-pdf-revision-10): header at bytes 0–4, uptime U32 LE at bytes 5–8, CRC-16 at bytes 9–10, total 11 bytes. An empty ACK, a request-ID reply, and an uptime ACK are distinct layouts; use the command's TM payload column below.

## Native ICD v1.2 EXP sensors

| Console command | TYPE/ADDR | TC payload | TM payload |
| --- | --- | --- | --- |
| `sen_cfg get` | `01/05` | 0 bytes | 8 bytes: effective BME + IMU config |
| `sen_cfg set bme <temp_os> <press_os> <hum_os> <filter>` | `01/05` | 8 bytes, BME only | 8-byte verified readback |
| `sen_cfg set imu accel\|gyro <odr> <scale>` | `01/05` | 8 bytes, one IMU pair | 8-byte verified readback |
| `sen_cfg set imu freefall\|wakeup <threshold> <duration>` | `01/05` | 8 bytes, one IMU pair | 8-byte verified readback |
| `sen_stat bme` / `sen_stat imu` | `01/06` | 1 byte: 2 / 3 | 8 / 16 bytes, validity mask + scaled I16 measurements |

[Full console and RS-422 definitions](icd-v1p2-console-mapping.md#native-sensor-command-register).
The older board/group `sen_stat` syntax below remains the separate `01/07` extension.

## Console ↔ RS-422 command register

| Console command | TC TYPE/ADDR | TC payload | Successful TM payload | Notes |
| --- | --- | --- | --- | --- |
| `ping` | `00/00` | 0 bytes | U32 uptime seconds (4 bytes) | Communication check. |
| `sys_info` | `01/00` | 0/1 bytes (self_test = 01) | 96-byte system identity ([payload fields](icd-console-mapping.md#sys_info-0x00)) | Uses background PDU Modbus and EXP RPARAM table 0/12 identity caches; OBC versions are local. |
| `sys_rtc` | `01/01` | 0 bytes | U32 current system epoch (4 bytes) | Reads the Linux system clock; does not modify it. |
| `sys_rtc <epoch\|now>` | `01/01` | U32 Unix epoch UTC (4 bytes) | U32 resulting system epoch (4 bytes) | Sets the system clock. `now` uses OBC time in `dal_flight_console`, but PC time in the RS-422 PC client. |
| `pdu_vout get` | `01/02` | 0 bytes | Four U16 LE fields (8 bytes): setpoint, measured voltage, measured current, flags | Reads the five-second PDU Modbus cache. [Field layout](icd-console-mapping.md#pwr_adj_cfg-0x02). |
| `pdu_vout set <mV>` | `01/02` | U16 target mV + six zero reserved bytes (8 bytes), 800–20000 | Same eight-byte VOUT report | PDU powers TRANS if needed before setting the TPS55287. Matches ICD v1.2 eight-byte SET. |
| `pwr_en <OBC|EXP|PDU> get` / `pwr_en <board> <rail> on|off` | `01/32` | 1-byte board for get; 6-byte board/masks/flags for set | Board U8 plus three U16 LE masks (7 bytes) | One command for all three boards. [Board IDs and rail bits](icd-console-mapping.md#unified-power-enable-command-current-flight-software). |
| `pwr_stat all\|monitor\|enable\|error` | `01/04` | Report selector U8 (1 byte) | 6-byte header + 6 bytes/source + 16-byte PDU trailer, maximum 184 bytes | Aggregate OBC/EXP/PDU readbacks; [layout and error-report limitation](icd-v1p2-console-mapping.md#pwr_stat-0104-native-v12). |
| [`sen_stat <OBC|EXP|PDU> get <group> [source] [cursor n]`](icd-console-mapping.md#sen_stat-0x07) | `01/07` | Board, group, source, cursor U8[4] | 10-byte header plus up to eight 12-byte records | Read sensor values, status, and age; follow `next_cursor` for more records. [Wire layout](icd-console-mapping.md#current-sen_stat-implementation-type-0x01-addr-0x07). |
| [`uvc_cfg <1|2|3|4> get|set ...`](icd-console-mapping.md#current-uvc_cfg-implementation-type-0x01-addr-0x10) | `01/30` GET / `01/10` SET | Get: camera ID U8; set: 32-byte profile | 32-byte profile | Select, configure, save, and read back one OBC USB camera. [Field layout](icd-console-mapping.md#current-uvc_cfg-implementation-type-0x01-addr-0x10). |
| [`uvc_ctl <1|2|3|4> capture <request_id>`](icd-console-mapping.md#current-uvc_ctl-and-uvc_stat-implementation) | `01/11` | Camera/operation/reserved/request ID (8 bytes) | U32 uptime ACK (4 bytes) | Apply the saved camera LED PWM, queue one JPEG under `/home/bee/data/images/usb`, then turn the LED off; poll `uvc_stat` for completion. |
| [`uvc_stat <1|2|3|4> get <request_id>`](icd-console-mapping.md#current-uvc_ctl-and-uvc_stat-implementation) | `01/31` | Camera U8 and request ID U32 LE (5 bytes) | Camera/state/error/request ID (8 bytes) | Read queued, capturing, saved, failed, or unknown state. |
| [`thm_cfg get\|heater\|ir ...`](icd-console-mapping.md#thm_cfg-0x13) | `01/13` | 0 bytes (get) or 32 bytes (set) | 32-byte manual heater and IR limits | Bridged to EXP `thm_cfg`; [payload layout](icd-console-mapping.md#thm_cfg-payload-layout). |
| [`thm_ctl heater\|ir ...`](icd-console-mapping.md#thm_ctl-0x14) | `01/14` | 8 bytes | 4-byte uptime ACK | Manual heater/IR control; [layout](icd-console-mapping.md#current-thm_ctl-implementation). |
| [`thm_stat [get]`](icd-v1p2-console-mapping.md#thm_stat-0115-native-v12) | `01/15` | 0 bytes | 8 bytes (6 channels + 2 reserved) | Thermal status; CRC at byte 13, total frame 15 bytes. |
| [`fld_cfg ...`](icd-console-mapping.md#fld_cfg-current-payload-layout) | `01/16` | 0 bytes (get) or 32 bytes | 32 bytes | Fluidic valve, flow, route, profile and pump-detect configuration; [pump detection](icd-console-mapping.md#fld_cfg-pump-detection). |
| [`fld_ctl valve\|pump ...`](icd-console-mapping.md#fld_ctl-manual-valve-and-pump-control) | `01/17` | 8 bytes | U32 uptime ACK (4 bytes) | Manual valve and pump control. |
| [`fld_stat [get]`](icd-console-mapping.md#fld_stat-actuator-status) | `01/18` | 0 bytes | 16 bytes | Valve and pump actuator status. |
| [`rba_ctl init\|move\|goto\|jog\|home\|stop\|disable ...`](icd-console-mapping.md#current-rba_ctl-implementation) | `01/1A` | 8 bytes | U32 uptime ACK (4 bytes) | Manual motor control. `01/19 rba_cfg` is reserved and returns unknown error `0x03`. |
| [`rba_stat [get]`](icd-console-mapping.md#current-rba_stat-implementation) | `01/1B` | 0 bytes | 16 bytes | Two motors and four optical limits. |
| [`pdu_fault_clear`](icd-console-mapping.md#pdu-fault-clear-011c) | `01/1C` | 0 bytes | U8 `0` | Clear latched PDU eFuse faults through Modbus coil 00003; live faults prevent clearing. DAL extension. |
| [`pdu_heartbeat get\|enable on\|off\|on <ms>\|off <ms>`](icd-console-mapping.md#pdu-heartbeat-011d) | `01/1D` | 0/5 bytes | U8 enable, U32 on_ms, U32 off_ms | PDU heartbeat coil 00010 and holding 40020–40023; DAL extension. |
| [`pdu_bootloader enter`](icd-console-mapping.md#pdu-bootloader-011e) | `01/1E` | 0 bytes | U8 accepted=1 | Write 0xB007 to PDU holding 40060; PDU resets shortly after reply. DAL extension. |
| [`pc_stat [get]`](icd-console-mapping.md#pc_stat-0x00) | `02/00` | 0 bytes | U8 status | PC experiment status bits. |
| [`pc_cfg get\|valve\|flow\|route\|profile\|resume\|datalog\|defaults ...`](icd-console-mapping.md#pc_cfg-current-payload-layout) | `02/01` | 0 bytes (get) or 32 bytes | 32 bytes | Shares the `fld_cfg` handler; sub-operations 8 and 9 (pump detect/result) are rejected with data error `01`. |
| [`pc_ctl activate\|deactivate\|test\|replace\|circulation\|ground\|standby\|stop\|reset_error`](icd-console-mapping.md#pc_ctl-0x02) | `02/02` | 8 bytes (opcode 1–9, argument, six zero bytes) | U32 uptime ACK (4 bytes) | PC experiment control via the EXP `pc_*` CLI. |
| [`thp_cfg heater\|ir get\|set ...`](icd-console-mapping.md#thp_cfg-0x03) | `02/10` selected GET / `02/03` SET | 32 bytes | 32 bytes | Heater PID profile 0–3 or IR profile 0–1, via EXP `thp_wire_cfg`. |
| [`thp_ctl heater <0-3>\|ir <0-1> start\|stop\|status\|resume`](icd-console-mapping.md#thp_ctl-0x04) | `02/04` | 1 byte selector (index, kind bit 2, action bits 3–4) | U8 status: phase, fault, configured, running | Via EXP `thp_wire_ctl`; EXP refusals return data error `01`. |
| [`dat_lst <mark>`](icd-console-mapping.md#dat_lst-0x00) | `03/00` | Exactly 1 byte: U8 Mark filter | U32 HK count, U32 SCI1 count, U32 SCI2 count (12 bytes) | Mark is required: `0xFF` = all files, `0x00` = unmarked, `0x01`–`0xFE` = matching mark. Missing or extra payload bytes are rejected. The image count includes collected EXP thermal frames in `images/exp`. A new list invalidates the current selection. |
| [`dat_sel <type> <index> <chunk_size>`](icd-v1p2-console-mapping.md#selection-metadata-0301) | `03/01` | U8 type, U32 index, U16 chunk size (7 bytes) | File metadata (41 bytes) | Type 0 housekeeping, 1 images, 2 LWL/PC logs; index 0–4294967295; chunk size 1–32768. |
| [`dat_get <chunk>`](icd-console-mapping.md#dat_get-0x02) | `03/02` | U32 zero-based chunk index (4 bytes) | Raw file chunk (up to selected size) | Requires a current `dat_sel`. |
| [`dat_des`](icd-console-mapping.md#dat_des-0x03) | `03/03` | 0 bytes | U32 uptime (4 bytes) | Releases selected file. |
| [`dat_del <type> <index>`](icd-console-mapping.md#dat_del-0x04) | `03/04` | U8 type, U32 index (5 bytes) | Three U32 file counts (12 bytes) | Active day's CSV files cannot be deleted. |
| [`dat_mrk <mark>` / `dat_mark <mark>`](icd-console-mapping.md#dat_mrk-0x05) | `03/05` | U8 mark (1 byte) | U32 uptime (4 bytes) | Requires selection; 0–254 accepted, `0xFF` reserved. PDF name: `dat_mrk`. |
| `tm_get` | `04/00` | 0 bytes | ICD v1.2 payload telemetry (57 bytes) | Requires valid OBC housekeeping. |
| `tm_obc_get` | `04/02` | 0 bytes | Full OBC housekeeping (64 bytes) | DAL extension beyond PDF revision 1.0. Requires valid OBC housekeeping. |
| `exp_ping` | `02/0A` | 0 bytes | U8 status, U16 text length, EXP UART `tlm_json system` text | UART reachability check. |
| `exp_tlm <section>` | `02/0B` | ASCII section name (1–19 bytes) | U8 status, U16 JSON length, reassembled schema-2 JSON | Sections: system, sensors, power, pc, heaters, pump, valves, motors, burn, ir, cameras, data, all. |
| `exp_csp_snapshot <0\|2\|3\|6\|12>` | `02/0F` | U8 RPARAM table ID | UTF-8 JSON cache envelope | Reads cached EXP identity, sensor, heater profile status, inventory, or compact ICD data; inspect `fresh`, `age_ms`, and table 12 `valid_mask`. Table 6 adds `download` with the latest OBC-saved filename and matching boot/object IDs, or `null`. DAL extension beyond PDF revision 1.0. |
| `exp_csp_refresh 0` | `02/0D` | ASCII `refresh 0` | U32 request ticket | Rereads identity table 0 on the CSP worker. Fetch completion with `exp_csp_result <ticket>`. |
| `exp_run <EXP command>` | `02/0C` | ASCII EXP CLI command (1–95 bytes) | U8 status, U16 text length, EXP UART CLI response | The same address covers EXP test GUI control commands; firmware waits for the EXP prompt or a 15 s timeout. |
| `exp_run camera_config\|camera_address_set\|camera_capture\|camera_status\|camera_point ...` | `02/0C` | ASCII EXP camera CLI command | U8 status, U16 text length, EXP text | Configure and capture either MLX9064x camera. `camera_address_set` programs the sensor address on an isolated I2C branch; power-cycle the sensor if `restart_required=1`. `camera_point <0\|1> <x> <y> <delta>` queues a fresh SRAM-only measurement; poll `camera_status` for `CAMERA_POINT` center/max/mean and `CAMERA_STATS` whole-frame values. Delta clips at image edges. This does not publish an image object. |
| `burn_cfg_set <0\|1> <duty_pct\|burn_time_ms\|max_total_current_ma> <value>` | `02/0C` | ASCII command without `exp_run` prefix | U8 status, U16 text length, `OK persisted` or EXP error text | Validated console alias. Ranges are 1–100%, 50–1000 ms, and 1–3000 mA absolute MOTOR 12 V current. Send all three, then read `exp_run burn_status` before arming. |
| `exp_run bme688_cfg_get` | `02/0C` | ASCII EXP command | U8 status, U16 text length, applied EXP BME configuration text | EXP firmware supports runtime temperature/pressure/humidity oversampling and filter settings. |
| `exp_run bme688_cfg_set <temp_os> <press_os> <hum_os> <filter> 0 0 0` | `02/0C` | ASCII EXP command | U8 status, U16 text length, `BME688 QUEUED id=N` text | Poll `exp_run bme688_result N`; gas and heater are unsupported on EXP. |
| [`sen_bme_cfg EXP get\|set\|result`](icd-console-mapping.md#sen_bme_cfg-0x05) | `02/0C` | ASCII EXP BME CLI command | U8 status, U16 text length, EXP text | Read settings; set with `2x 4x 1x 3` style arguments; poll `result <id>`. Printed ICD `01/05` is not implemented. |
| [`sen_imu_cfg EXP get\|set\|reset\|result`](icd-console-mapping.md#sen_imu_cfg-0x06) | `02/0C` | ASCII EXP IMU CLI command | U8 status, U16 text length, EXP text | Configure accel/gyro rate and scale or motion thresholds; poll `result <id>`. Printed ICD `01/06` is not implemented. |
| `debug_handoff yes` | `FF/00` | ASCII `DEBUG_HANDOFF_OK` (16 bytes) | U32 uptime (4 bytes), debug ACK `0xFE` | Hands the RS-422 UART to the debug getty and stops flight software. |
| `shell <Linux command>` | `FF/01` | Raw command text bytes (1–2048) | Shell result (7 + N bytes), debug ACK `0xFE` | Runs `/bin/sh -c`; captures at most 20480 output bytes. Debug extension, outside PDF revision 1.0. |

The ordinary rows use the same dispatcher whether the request came from the console or RS-422. `debug_handoff` and `shell` also use that dispatcher, but return `0xFE` instead of echoing `0xFF` as the successful ACK byte.

EXP gateway replies start with status U8 (0 completed, 1 UART or command error, 2 timeout, 3 UART not configured, 4 output too large), followed by U16 text length and that many UTF-8 bytes. A completed UART exchange can still contain an EXP CLI refusal in the text; inspect the text before treating an actuator request as successful. Compact EXP status comes from the FSW worker's CAN RPARAM table 12 cache when CSP is enabled. EXP firmware and hardware versions come from RPARAM table 0 at startup, after MCU reboot, or after `exp_csp_refresh 0`; no periodic UART `version` request is sent. Periodic `tlm_json system`, `sensors`, and `power` polling for the GUI cache requires `DAL_EXP_UART_POLL=1`.

`sys_rtc` with no argument reads the Linux system clock. The set form sets that
clock and attempts to set `/dev/rtc2`. Its ACK confirms the system-clock
operation; it does **not** confirm the hardware RTC write. Verify the hardware
time separately with `hwclock --show --utc -f /dev/rtc2` after setting it. The
[ICD mapping](icd-console-mapping.md#sys_rtc_cfg-0x01) gives the exact RS-422
get/set frames and field offsets.

### File types and storage folders on the OBC

The current OBC groups files by their **storage folder**, not by their filename
extension. The three counts returned by `dat_lst` are in this order; the same
numeric type is passed to `dat_sel` and `dat_del`:

| Type | Current name | Meaning | Legacy ICD label |
| ---: | --- | --- | --- |
| `0` | **Housekeeping** | Housekeeping files only | HK |
| `1` | **Images** | USB, MIPI, and EXP IR camera images, including raw thermal `.bin` frames | SCI1 |
| `2` | **Logs** | LWL firmware logs and PC experiment logs | SCI2 |

Default storage root on the OBC: **`/home/bee/data`**.

| Type | Folder on OBC | Contents / example filenames |
| ---: | --- | --- |
| `0` HK | `/home/bee/data/logs/hk/` | Housekeeping CSV: `HK0_YYYYMMDD.csv` (OBC), `HK1_YYYYMMDD.csv` (EXP) |
| `2` SCI2 | `/home/bee/data/logs/lwl/` | LWL firmware diagnostic logs; collected EXP objects: `EXP_B00000078_O00000001.bin` |
| `2` SCI2 | `/home/bee/data/logs/pc/` | PC experiment logs; collected EXP objects: `EXP_Bxxxxxxxx_Oxxxxxxxx.bin` |
| `1` Images | `/home/bee/data/images/usb/` | USB camera images, e.g. `USB0_NNNNNNNN.jpg` |
| `1` Images | `/home/bee/data/images/mipi/` | MIPI camera `.raw` frames and converted `.png` images, e.g. `MIPI0.0_H_000001.raw` |
| `1` Images | `/home/bee/data/images/exp/` | EXP IR camera frames, e.g. `EXP_C1_B00000078_O00000028_E2.bin`; `E1` is int16, `E2` is float32 |

These are the directories scanned by the file-transfer backend. A directory may
not exist until a producer creates its first file. An EXP thermal `.bin` belongs
to **Images (type 1)**, while an EXP LWL/PC `.bin` belongs to **SCI2 logs (type 2)**.
`dat_lst` returns counts, not a list of filenames; use `dat_sel` to inspect each
file's metadata. Indexes start at zero within each type and combine all folders
of that type, sorted by relative path.

`DAL_DATA_DIR` overrides the file-transfer root; replace `/home/bee/data` above
with that configured root. The EXP object collector can separately use
`DAL_EXP_DATA_DIR` (falling back to `DAL_DATA_DIR`, then `/home/bee/data`). Keep
these roots aligned if collected EXP files must be visible through `dat_lst`.

The disk basename may be longer than the **20-byte filename field in `dat_sel`**;
that field is truncated for long names. FSW retains the full relative path
internally and `dat_get` reads the selected file using that path. The 20-byte
field is not a filesystem filename limit.

### File metadata returned by `dat_sel`

These offsets are within the **41-byte TM payload**:

| Payload offset | Field | Encoding |
| ---: | --- | --- |
| 0 | Data type | U8 |
| 1 | Data index | U32 |
| 5 | Chunk size | U32 |
| 9 | File basename | 20 bytes, NUL-padded when shorter; no terminator when exactly 20 bytes |
| 29 | File size | U32 |
| 33 | File CRC-32 | U32 ISO-HDLC |
| 37 | Chunk count | U32 |

The v1.2 data-transfer layouts and DALTUI workflow are documented [here](icd-v1p2-console-mapping.md#data-transfer-v12-type-0x03). Empty files have zero chunks.

After `dat_sel`, request chunks `0` through `chunk_count − 1` with `dat_get`, concatenate their raw TM payloads, verify the whole-file CRC-32, then call `dat_des`. Selection state is shared by RS-422 and the local console, so avoid interleaving two file transfers. If the file changes during transfer, reselect and retry.

### Shell result returned by `FF/01`

| Payload offset | Field | Encoding |
| ---: | --- | --- |
| 0 | Exit status | U8: 0 exited, 1 signaled, 3 execution error |
| 1 | Exit code or signal | U8 |
| 2 | Output truncated | U8: 0 or 1 |
| 3 | Captured output length | U32 |
| 7 | Combined stdout/stderr | N bytes |

The shell command executes in a fresh process, so `cd` does not persist to the next request. The reply's debug ACK indicates command dispatch; inspect exit status and exit code for shell success.

### Telemetry layouts

`tm_get` returns the [ICD v1.2 57-byte payload](icd-v1p2-console-mapping.md#tm_get-0400-native-v12), with PDU/EXP/OBC temperatures, EXP NTC/BME/IMU/SLF readings and cached RBA/TA status. The TM totals 64 bytes; CRC16 starts at offset 62. Missing I16 fields use `0x8000`, U16 fields use `0xFFFF`. MLX full-frame average is currently unavailable because no verified average is published by the cache.

`tm_obc_get` returns a 64-byte payload: U16 validity mask, U32 snapshot epoch, U32 boot count, onboard and four NTC temperatures, BME temperature/pressure/humidity/gas resistance, then eight PAC voltages and eight currents. Its [offset and scaling table](flight-software.md#obc-housekeeping-payload-0x040x02) is the current wire definition. Invalid measurements have their mask bit clear.

## Console-only operations

These names are accepted over the local Unix socket or by the interactive client. They have **no dedicated RS-422 TYPE/ADDRESS**:

| Local command or option | Result |
| --- | --- |
| `help` | Lists console commands. `dal_flight_console --json help` returns JSON. |
| `gui_state` | One JSON snapshot with OBC, EXP, PDU, GPIO, device presence, RTC, MIPI route, and camera LED status. |
| `gui_shell <seconds> <command>` | Runs a Linux shell command in a separate socket worker; timeout 1–120 s; returns JSON with `ok`, `exit`, `timeout`, and `text`. |
| `raw <type> <address> [byte ...]` | Console parser for an arbitrary TYPE/ADDRESS/payload; sent through the same dispatcher. It is not a new wire command. |
| `--json` / `json on` / `json off` | Client response formatting. `json on/off` is an interactive-client setting. |
| `exit` / `quit` | Exits only the interactive client. |

The socket accepts one newline-terminated command per connection and closes after its reply. The command line is limited to 4096 bytes. Console text/JSON formatting does not change the RS-422 binary frame.

## Unsupported entries from the PDF

The current dispatcher does not implement the PDF's monitoring and reserved experiment entries (`02/05`–`02/09`), `01/05` and `01/06` as such (use the `02/0C` routes), `01/08`–`01/0F`, `01/19` (`rba_cfg`, reserved), `01/1F` and above, SFU (`TYPE 0x05`), or `tm_cfg` (`04/01`). Those frames return an error: unknown error `0x03` for TYPE `01`, `02` and `05`, otherwise invalid TC `0x00`. The EXP UART gateway extensions are `02/0A`–`02/0C`. Sending a reserved TYPE/ADDRESS with `raw` does not add support for it.
