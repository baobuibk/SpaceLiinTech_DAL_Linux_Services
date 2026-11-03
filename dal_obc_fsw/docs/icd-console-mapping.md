# BEE-DAL payload ICD and console mapping

## Burn wire command mapping (current extension)

The EXP burn wire configuration now consists of `duty_pct` (1–100%), `burn_time_ms` (50–1000 ms), and `max_total_current_ma` (1–3000 mA) for wire 0 or 1. The current ceiling is the absolute MOTOR 12 V rail current. A finished pulse reports `COMPLETED`, which does not confirm wire separation. Existing EXP safety checks still require fresh PAC data and a motor-rail voltage within 8–16 V. Old ten-field FRAM configurations are rejected and must be entered again.

| Console command | RS-422 TC payload | TYPE/ADDR | MCU command |
| --- | --- | --- | --- |
| `burn_cfg_set 0 duty_pct 25` | ASCII `burn_cfg_set 0 duty_pct 25` | `02/0C` | Persist duty |
| `burn_cfg_set 0 burn_time_ms 100` | ASCII `burn_cfg_set 0 burn_time_ms 100` | `02/0C` | Persist burn time |
| `burn_cfg_set 0 max_total_current_ma 1000` | ASCII `burn_cfg_set 0 max_total_current_ma 1000` | `02/0C` | Persist total-current limit |
| `exp_run burn_status` | ASCII `burn_status` | `02/0C` | Read applied settings and state |
| `exp_run burn_arm 0`; `exp_run burn_fire`; `exp_run burn_abort` | ASCII command after `exp_run` | `02/0C` | Arm, fire, abort |

Frame: `A5 02 0C LEN_LO LEN_HI [ASCII bytes] CRC_LO CRC_HI`; CRC-16 covers only the ASCII payload. Success TM uses the same TYPE/ADDR with payload `status U8, text_length U16 LE, EXP reply bytes`. `burn_cfg_set` is a validated flight-console alias for `exp_run burn_cfg_set ...`. Configure and verify all three fields before arming. RPARAM table 9 publishes `burn_duty_pct` (offset 191), `burn_time_ms` (193), and `burn_total_ma` (209); former burn fields are reserved.

This page maps the [revision 1.0 ICD](icd.md) and its unified power payload to the **current flight software console**. The [source PDF](assets/icd/BEE-DAL_ICDr1p0.pdf), titled *IGRVT-50 Payload ICD*, Space LiinTech, rev 1.0, has 32 pages numbered 5–36 and remains the original protocol reference. A dash in the console column means the flight software does not implement that command. The [current flight command register](flight-command-register.md) gives implemented wire payloads. The [printed-page tables](#source-tables-printed-pages-1036) retain the PDF's original addresses and content.

The physical interface and framing remain copied from revision 1.0. The command register adds the current flight software mapping and the unified power payload; later printed-page tables retain the PDF verbatim values and addresses. Update this page and the [ICD page](icd.md) together when the protocol changes.

## Electrical interface

The payload connector is a 9-pin vacuum Sub-D, model FA13603 from Solid Sealing Technology, mating with FA13118. Maximum current is 5 A per contact.

| Pin | Signal | Description |
| ---: | --- | --- |
| 1 | IO | Debug IO |
| 2 | RX− / B | RS-422 inverting receiver input |
| 3 | TX+ / Y | RS-422 driver output |
| 4 | GND | Ground |
| 5 | GND | Ground |
| 6 | RX+ / A | RS-422 receiver input |
| 7 | TX− / Z | RS-422 inverting driver output |
| 8 | VIN | Input voltage |
| 9 | VIN | Input voltage |

| Electrical characteristic | Minimum | Typical | Maximum |
| --- | ---: | ---: | ---: |
| Supply voltage | 11.2 V | 12.0 V | 13.5 V |
| Current consumption | 300 mA | 1000 mA | 1250 mA |
| Power consumption | 3.6 W | 12.0 W | 15.0 W |
| Inrush current | — | — | 1500 mA for at most 5 s |

The PDF's footnotes limit maximum power consumption to 60 seconds through the payload interlock and inrush current to 5 seconds. Its power budget is:

| Payload mode | Average | Peak | Duration/duty cycle |
| --- | ---: | ---: | --- |
| Off / safe state | 0 | — | — |
| Boot / initialization | 3 W | 8 W | 60 s / 100% |
| Standby | 5 W | — | — / 100% |
| Nominal operation | 12 W | 15 W | — / 100% |
| Data transfer | 8 W | 10 W | — / 100% |
| Fault / recovery | TBD | TBD | TBD |
| Ground checkout | TBD | TBD | TBD |

The signal table specifies an Analog Devices LTC2865 transceiver, **3.0–5.5 V** operating voltage, **±15 kV** RS-422 ESD protection, **921,600 bps** typical and **16,000,000 bps** maximum data rate, and protocol text “Native, KISS”. The binary packet format below is the format defined by this ICD.

## RS-422 transport and frame

The UART uses **921600 baud, 8N1, no parity** and transmits each byte least-significant bit first. Multibyte fields are little-endian. The packet timeout is 2000 ms.

### Telecommand (TC)

| Byte offset | Field | Type | Meaning |
| ---: | --- | --- | --- |
| 0 | START | U8 | `0xA5` |
| 1 | TYPE | U8 | Command group |
| 2 | ADDRESS | U8 | Command within group |
| 3 | LENGTH | U16 LE | Payload length, 0–65535 bytes |
| 5 | PAYLOAD | U8[N] | Command-specific bytes |
| 5 + N | CRC-16 | U16 LE | CRC; see scope note below |

For a zero-length payload, the PDF requires CRC-16 to be `0xFFFF`. TYPE values are `0x00` ping, `0x01` system configuration, `0x02` experiment control, `0x03` data transfer, `0x04` telemetry, and `0x05` SFU. No SFU command table is supplied in revision 1.0.

### Telemetry (TM)

The reply has the same START, LENGTH, PAYLOAD, and CRC positions. On success, byte 1 echoes the TC TYPE and byte 2 echoes the TC ADDRESS. On error, byte 1 is `0xFF` and byte 2 is an error code:

| Error code | Meaning |
| --- | --- |
| `0x00` | Invalid TC |
| `0x01` | Data error |
| `0x02` | Memory error |
| `0x03` | Unknown error |

The generic ACK payload is a U32 system uptime in seconds (TM LENGTH = 4; uptime at byte 5; CRC at byte 9). Some commands define another reply layout in the tables below.

### CRC parameters

| Check | Polynomial | Initial value | Input/output reflection | Final XOR |
| --- | --- | --- | --- | --- |
| CRC-16 CCSDS, as named in PDF | `0x1021` | `0xFFFF` | false / false | `0x0000` |
| CRC-32 ISO-HDLC, for files | `0x04C11DB7` | `0xFFFFFFFF` | true / true | `0xFFFFFFFF` |

The PDF gives the CRC parameters and the `0xFFFF` result for an empty payload, but does not explicitly state which frame bytes enter CRC-16. The current flight software calculates CRC-16 over **payload bytes only**, which produces that empty-payload result. Record this scope explicitly in the next ICD revision.

## Command register

In these tables, **TC** and **TM** lengths count payload bytes, excluding the seven bytes of frame overhead. `0/32` means a zero-byte read request or a 32-byte write request. “ACK” means the generic four-byte uptime response. A length or field marked “PDF discrepancy” needs review before changing an MCU parser. The **Console command** column is the current flight software mapping, not a field from the PDF.

### Ping and system configuration

| TYPE/ADDR | ICD name | Console command | TC payload | TM payload | Purpose |
| --- | --- | --- | --- | --- | --- |
| `00/00` | [`ping`](#ping-communication-check) | `ping` | 0 | ACK | Communication check |
| `01/00` | [`sys_info`](#sys_info-0x00) | `sys_info` (get only) | PDF: 0/1; current: 0 only. The PDF's self-test byte is not implemented. | 96 bytes | System information |
| `01/01` | [`sys_rtc_cfg`](#sys_rtc_cfg-0x01) | `sys_rtc` (get); `sys_rtc <epoch\|now>` (set) | 0/4; U32 Unix epoch UTC for set | U32 epoch | Read/set system clock |
| `01/02` | [`pwr_adj_cfg`](#pwr_adj_cfg-0x02) | `pdu_vout get`; `pdu_vout set <mV>` | PDF: 0/8 opaque; flight software: 0/2 U16 mV | Four U16 fields (8 bytes) | PDU VOUT setpoint; current implementation differs from printed PDF length |
| `01/03` | [`pwr_en`](#unified-power-enable-command-current-flight-software) | `pwr_en <board> get|<rail> on|off` | 1/6 bytes: board U8; optional select U16, on U16, flags U8 | 7 bytes | OBC, EXP, or PDU switched outputs |
| `01/04` | [`pwr_stat`](#pwr_stat-current-flight-software-0104) | `pwr_stat <board> get [source|all|mask <mask>]` | Current: board U8 + source mask U16 LE (3 bytes); PDF: one U8 selector | Current: 6-byte header + 12 bytes per source | OBC, EXP, or PDU power status |
| `01/05` | [`sen_bme_cfg`](#sen_bme_cfg-0x05) | `sen_bme_cfg EXP get\|set\|result` via `02/0C` | Printed: 0/8; current route: ASCII | Printed: 8 bytes; current route: EXP status/text | Native `01/05` reserved; EXP console route available |
| `01/06` | [`sen_imu_cfg`](#sen_imu_cfg-0x06) | `sen_imu_cfg EXP get\|set\|reset\|result` via `02/0C` | Printed: 0/8; current route: ASCII | Printed: 8 bytes; current route: EXP status/text | Native `01/06` reserved; EXP console route available |
| `01/07` | [`sen_stat`](#sen_stat-0x07) | `sen_stat <board> get <group> [source] [cursor n]` | Current: 4 bytes; printed: U8 selector | Current: 10 + 12×N bytes | [Current format](#current-sen_stat-implementation-type-0x01-addr-0x07) |
| `01/1C` | [PDU fault clear](#pdu-fault-clear-011c) | `pdu_fault_clear` | 0 bytes | U8 `0` | Clear latched PDU eFuse faults; DAL extension. |
| `01/1D` | [PDU heartbeat](#pdu-heartbeat-011d) | `pdu_heartbeat get\|enable on\|off\|on <ms>\|off <ms>` | 0/5 bytes | 9 bytes | Enable and set LED on/off periods; DAL extension. |
| `01/1E` | [PDU bootloader](#pdu-bootloader-011e) | `pdu_bootloader enter` | 0 bytes | U8 `1` | Request PDU bootloader entry; DAL extension. |
| `01/10` | [`uvc_cfg`](#current-uvc_cfg-implementation-type-0x01-addr-0x10) | `uvc_cfg <camera> get\|set ...` | Printed: 0/32; current: 1/32 | 32 bytes | Configure one of four OBC USB cameras; see current layout. |
| `01/11` | [`uvc_ctl`](#current-uvc_ctl-and-uvc_stat-implementation) | `uvc_ctl <1\|2\|3\|4> capture <request_id>` | 8 bytes | Current: 8-byte queue status | Capture one JPEG; see current layout. |
| `01/12` | [`uvc_stat`](#uvc_stat-response-details) | `uvc_stat <camera> get <request_id>` | Current: 5 bytes | Current: 8 bytes | Poll capture request; see current layout. |
| `01/13` | [`thm_cfg`](#thm_cfg-0x13) | `thm_cfg get\|heater\|ir` | 0/32 | 32 bytes | Manual output limits; [payload layout](#thm_cfg-payload-layout) |
| `01/14` | [`thm_ctl`](#thm_ctl-0x14) | `thm_ctl heater/ir ...` | 8 bytes | 4-byte uptime ACK | [Current layout](#current-thm_ctl-implementation) |
| `01/15` | [`thm_stat`](#thm_stat-0x15) | `thm_stat [get]` | 0 | 6 bytes | [Current layout](#current-thm_stat-implementation) |
| `01/16` | [`fld_cfg`](#fld_cfg-current-payload-layout) | `fld_cfg get\|valve\|flow\|route\|profile\|resume\|pump_type\|pump detect/result` | 0/32 | 32 bytes | Fluidic mapping and profiles; [pump detection](#fld_cfg-pump-detection) |
| `01/17` | [`fld_ctl`](#fld_ctl-manual-valve-and-pump-control) | `fld_ctl valve mask <0–15>`; `fld_ctl pump <u11\|u15> off\|on\|frequency <Hz>\|voltage [channel] <Vpp>` | 8 bytes | 0/4 bytes | Manual valve and pump control; pump reply gives request ID |
| `01/18` | [`fld_stat`](#fld_stat-actuator-status) | `fld_stat [get]` | 0 | 16 bytes | Valve and pump actuator status |
| `01/19` | [`rba_cfg`](#rba_cfg-0x19) | — | 0/32 | 32 bytes | Two-motor/limit configuration; fixed half-step table; OBC handler reserved |
| `01/1A` | [`rba_ctl`](#current-rba_ctl-implementation) | `rba_ctl init|move|goto|jog|home|stop|disable|result ...` | 8 bytes | 0/4/12 bytes | Manual motor control; [current layout](#current-rba_ctl-implementation) |
| `01/1B` | [`rba_stat`](#current-rba_stat-implementation) | `rba_stat [get]` | 0 | 16 bytes | Two motors and four optical limits; [current layout](#current-rba_stat-implementation) |
| `01/20` | `lev_calib` | — | 0/128 or 0/130; PDF discrepancy | 130 bytes | LEV calibration; reserved |
| `01/21` | `lev_cfg` | — | 0/32 | 32 bytes | LEV configuration; reserved |
| `01/22` | `lev_ctl` | — | 8 bytes | ACK | LEV manual control; reserved |
| `01/23` | `lev_stat` | — | 0 | 16 bytes | LEV status; reserved |


The printed PDF assigns `pwr_stat` selector values 0 all, 1 monitor, 2 enable, and 3 error; the current implementation instead uses board plus source mask. The `sen_stat` selector values are 0 all, 1 temperature, 2 BME, 3 IMU, and 4 flow. The printed PDF does not define the inner fields of the opaque configuration, control, or status arrays. Its **`pwr_adj_cfg` entry does not assign bytes or units**; the current flight software layout is specified in the [implementation detail](#pwr_adj_cfg-0x02). Other opaque arrays remain undefined.


### Unified power enable command (current flight software)

The revision 1.0 PDF assigns `pwr_en` to `01/03` and `pwr_stat` to `01/04`. The current flight software retains those addresses. A board ID in the `pwr_en` payload selects OBC (`0x03`), EXP (`0x04`), or PDU (`0x05`); the rail masks use board-specific bit tables below. The printed PDF tables remain unchanged. `pwr_stat` uses the [status format below](#pwr_stat-current-flight-software-0104).

| Frame field | Get TC | Set TC | Successful TM |
| --- | --- | --- | --- |
| START / TYPE / ADDR | `A5 01 03` | same | same |
| LENGTH at bytes 3–4 | `01 00` | `06 00` | `07 00` |
| PAYLOAD from byte 5 | `board` U8 | `board` U8, `select_mask` U16 LE, `on_mask` U16 LE, `flags` U8 | `board` U8, `on_mask` U16 LE, `known_mask` U16 LE, `failed_mask` U16 LE |
| CRC-16 location | bytes 6–7 | bytes 11–12 | bytes 12–13 |

The console syntax is `pwr_en <OBC|EXP|PDU> get`, `pwr_en <board> <rail> on|off [force]`, or `pwr_en <board> set <select_mask> <on_mask> [force]`. Rail names and on/off are case-insensitive. `force` applies only to EXP off. `get` reads all control states on one board. A named set uses a single selected bit; the mask form can update several rails.

The CRC covers payload bytes only. `select_mask` must be nonzero for set; `on_mask` may only contain selected bits. Invalid board IDs or masks return data error `01`; lengths other than 1 or 6 return invalid TC `00`. A partial set returns a seven-byte TM with failed bits in `failed_mask`.

The TM `on_mask` describes the **reported control state**, and `known_mask` marks the bits whose control state was read. An unknown bit in `on_mask` has no meaning. For OBC, the state comes from GPIO brightness readback; for EXP read requests, from cached CAN RPARAM table 2 `rail_enabled` feedback; for PDU read requests, from cached Modbus enable coils. EXP and PDU set requests still use device readback. This reply does not verify voltage at a load. `failed_mask` is zero on a successful get.

#### OBC rails (board 0x03)

| Bit | Console output name | Board control | Example |
| ---: | --- | --- | --- |
| 0 | `AI_power` | `pow_onoff_ai` | `pwr_en OBC AI_power off` |
| 1 | `FPGA_power` | `pow_onoff_fpga` | `pwr_en OBC FPGA_power on` |
| 2 | `USB1_power` | `usb_1_pwr_en` | `pwr_en OBC USB1_power on` |
| 3 | `USB2_power` | `usb_2_pwr_en` | `pwr_en OBC USB2_power on` |
| 4 | `USB3_power` | `usb_3_pwr_en` | `pwr_en OBC USB3_power on` |
| 5 | `USB4_power` | `usb_4_pwr_en` | `pwr_en OBC USB4_power on` |
| 6 | `MIPI1_power` | `pow_mipicam_1` | `pwr_en OBC MIPI1_power off` |
| 7 | `MIPI2_power` | `pow_mipicam_2` | `pwr_en OBC MIPI2_power off` |

Valid bits: `0x00FF`. `flags=0` is required. Turning a MIPI camera off also disables that camera's I²C enable. Turning camera power on does not select its sensor or enable its I²C path; use the camera routing controls for that.

#### EXP rails (board 0x04)

| Bit | Console output name | EXP domain | MCU CLI commands |
| ---: | --- | --- | --- |
| 0 | `EXP12V` | `12V_EXP`, heaters and IR LEDs | `power_exp12v_on/off/get` |
| 1 | `SLN_HD` | `5V_SLN_HD`, pumps and valves | `power_sln_hd_on/off/get` |
| 2 | `MOTOR` | Motor supply, motors and burn wires | `power_motor_on/off/get` |

Valid bits: `0x0007`. `flags` bit 0 permits **force off only** when every selected bit is being turned off; for example, `pwr_en EXP SLN_HD off force`. Normal off may be refused while another EXP consumer owns a domain. Force off clears its claims and holds it off until an explicit on request. The command uses the EXP UART CLI and reports each domain's requested state, not physical voltage.
For a set, FSW resolves `failed_mask` from the MCU's subsequent
`power_all_get` readback. A lost command reply does not mark a rail failed if
the MCU reports the requested state; an unknown or mismatched state does.
When all three bits are selected for the same state, FSW sends the MCU's
`power_all_on` or `power_all_off` once, then reads back all three states.

#### PDU rails (board 0x05)

| Bit | Console output name | PDU control | Modbus coil |
| ---: | --- | --- | --- |
| 0 | `VOUT` | TPS55287 VOUT enable | `00001` |
| 1 | `TRANS` | TRANS eFuse enable | `00002` |

Valid bits: `0x0003`; `flags=0` is required. VOUT enable requires a valid setpoint from [`pwr_adj_cfg`](#pwr_adj_cfg-0x02). Enabling VOUT can turn TRANS on first. Disabling TRANS also turns VOUT off. A set selecting both bits with VOUT on and TRANS off is rejected with data error `01`. When both are selected, the write order is TRANS on, VOUT, then TRANS off. Use `pwr_en PDU get` to read both controls.

Example set payloads (the full frame also needs the header and payload CRC): OBC AI on `03 01 00 01 00 00`, EXP motor off `04 04 00 00 00 00`, PDU VOUT on `05 01 00 01 00 00`. The first byte identifies the board; mask bits are local to that board.

#### PDU fault clear (01/1C)

`pdu_fault_clear` takes a zero-length TC payload. FSW writes `1` to PDU
Modbus coil `00003` and reads that coil back. On success, TM echoes TYPE
`01`, ADDR `1C`, with one U8 byte `0` (latch clear). A nonzero TC length
returns invalid-TC `00`; a failed Modbus operation or a live PC4/PC0
fault returns error `03`. This is a DAL extension to the printed ICD.

#### PDU heartbeat (01/1D)

Get uses an empty TC payload. Set uses U8 action plus U32 value in little
endian: action `1` enables (`1`) or disables (`0`) the LED, action `2` sets
on time, and action `3` sets off time. Periods are 1–60000 ms. TM always
returns U8 enabled, U32 on_ms, U32 off_ms (9 bytes, little endian). FSW
maps the enable to Modbus coil `00010` and the periods to holding registers
`40020`–`40023` (32-bit values, high word first). Invalid payloads return
error `01` or `00`; Modbus/readback failures return `03`.

#### PDU bootloader (01/1E)

The empty TC requests entry. FSW writes key `0xB007` to holding register
`40060` and returns one U8 `1` once the PDU acknowledges. The PDU waits
100 ms, turns off VOUT and TRANS, then resets into its bootloader. A Modbus
failure returns error `03`; a nonempty TC returns `00`.

#### pwr_stat (current flight software, 01/04)

The single read-only command covers all three boards. Console forms are `pwr_stat <OBC|EXP|PDU> get` for every source, `pwr_stat <board> get <source_name>` for one source, and `pwr_stat <board> get mask <mask>` for several sources. Names and board IDs are case-insensitive in the console; RS-422 uses numeric IDs. The three-byte TC payload is `board` U8 followed by `source_mask` U16 LE. Board IDs are `0x03` OBC, `0x04` EXP, and `0x05` PDU. Mask zero selects all board sources. A nonzero mask selects exactly its set bits. Invalid board IDs or bits return data error `01`; a TC length other than 3 returns invalid TC `00`.

| TM payload offset | Field | Type | Meaning |
| ---: | --- | --- | --- |
| 0 | Board | U8 | Echoes the selected board |
| 1 | Record count | U8 | Number of returned sources |
| 2 | Snapshot time | U32 LE | Unix seconds; `0` if no measurement snapshot is available |
| 6 | Source records | 12 bytes each | In ascending source-bit order |

Each record is `source_bit` U8, `flags` U8, `sample_age_ms` U16 LE, `voltage_mv` U32 LE, and `current_ma` I32 LE. Flag bit 0 means on/off state known; bit 1 means on (meaningful only when bit 0 is set); bits 2 and 3 mean voltage and current valid; bit 4 means the observation passed its freshness limit. `sample_age_ms=0xFFFF` means age unavailable. Ignore voltage and current fields when their validity bits are clear. The TM payload is `6 + 12 × record_count` bytes; one request returns one frame. A single source returns 18 payload bytes, and all 16 OBC sources return 198 payload bytes. CRC-16 covers the payload only, as elsewhere in the current flight software.

| Board | Source bits and console names | Measurement source |
| --- | --- | --- |
| OBC (`03`) | 0 `5V_SAMA7`, 1 `5V_FPGA`, 2 `3V3`, 3 `5V_AI`, 4 `PMIC_3V3`, 5 `PMIC_DDR_1V35`, 6 `PMIC_CORE_1V15`, 7 `PMIC_CPU_1V15` | OBC PAC U6/U2 housekeeping, mV and signed mA |
| OBC (`03`) | 8 `AI_power`, 9 `FPGA_power`, 10–13 `USB1_power`–`USB4_power`, 14–15 `MIPI1_power`–`MIPI2_power` | GPIO control readback; voltage/current unavailable |
| EXP (`04`) | 0 `12V_MOTOR`, 1 `12V_EXP`, 2 `5V_EXP`, 3 `5V_SLN_HD` | EXP PAC housekeeping over CAN, mV and signed mA |
| EXP (`04`) | 4 `EXP12V`, 5 `SLN_HD`, 6 `MOTOR` | Cached CAN RPARAM table 2 `rail_enabled` feedback; voltage/current unavailable |
| PDU (`05`) | 0 `VOUT`, 1 `TRANS`, 2 `INPUT_12V`, 3 `INPUT_5V` | Cached Modbus poll; VOUT/TRANS voltage and current, input voltage only |

OBC PAC values require a valid sample no older than 30 seconds. EXP PAC values require an online, valid snapshot with the PAC sensor group valid and no older than 30 seconds, including the sensor's own age. EXP rail feedback requires a successful CAN poll no older than three seconds. PDU values come from a background Modbus poll no older than 15 seconds. An unavailable source still has a record with clear validity flags. The snapshot time may differ from a switch observation time; use each record's age for freshness.

### Experiment control

| TYPE/ADDR | ICD name | Console command | TC payload | TM payload | Purpose |
| --- | --- | --- | --- | --- | --- |
| `02/00` | [`pc_stat`](#pc_stat-0x00) | `dal_flight_console pc_stat [get]` | 0 | U8 status | PC experiment status; [current bits](#pc_stat-0x00) |
| `02/01` | [`pc_cfg`](#pc_cfg-current-payload-layout) | `pc_cfg get|valve|flow|route|profile|resume|datalog|defaults ...` | 0/32 | 32 bytes | PC experiment profiles, routes, and policy; [current layout](#pc_cfg-current-payload-layout) |
| `02/02` | [`pc_ctl`](#pc_ctl-0x02) | `pc_ctl activate|deactivate|test|replace|circulation|ground|standby|stop|reset_error` | 8 bytes | 0-byte ACK | PC experiment control; [current payload](#pc_ctl-0x02) |
| `02/03` | [thp_cfg](#thp_cfg-0x03) | `dal_flight_console thp_cfg heater|ir get|set <profile> [parameters]` ([full syntax](#thp_cfg-0x03)) | 32 | 32 bytes | Heater/IR thermal profile get or set |
| `02/04` | [`thp_ctl`](#thp_ctl-0x04) | `dal_flight_console thp_ctl heater <0-3> start|stop|status|resume`; `dal_flight_console thp_ctl ir <0-1> start|stop|status` | 1 | U8 status | [Control format](#thp_ctl-0x04) |
| `02/0D` | EXP CSP request | `exp_csp_refresh 0` | ASCII `refresh 0` | U32 ticket | Rereads identity table 0 once; fetch completion with `exp_csp_result <ticket>` at `02/0E`. |
| `02/0F` | EXP cached RPARAM snapshot | `exp_csp_snapshot <0\|2\|3\|6\|12>` | U8 table ID | UTF-8 JSON | Local extension: returns cached identity, sensor, heater profile status, inventory, or ICD compact status without CAN I/O. Table 0 freshness requires a matching current table 12 boot ID; other tables expire after three seconds. |
| `02/05` | `mon_cfg` | — | 0/32 | 32 bytes | Monitoring configuration |
| `02/06` | `mon_en` | — | 0/1 | U8 enable state | Monitoring enable |
| `02/07` | `rsv_op1` | — | 32 bytes | ACK | Reserved operation 1 |
| `02/08` | `rsv_op2` | — | 32 bytes | 32 bytes | Reserved operation 2 |
| `02/09` | `rsv_op3` | — | 0/32 | 32 bytes | Reserved operation 3 |

The PDF gives `rsv_op1` the description “LEV subsystem manual control” and `rsv_op2`/`rsv_op3` “Monitoring configure” in their detailed tables. Their summary names remain reserved operations.

### File transfer

| TYPE/ADDR | ICD name | Console command | TC payload | TM payload | Purpose |
| --- | --- | --- | --- | --- | --- |
| `03/00` | `dat_lst` | `dat_lst [mark]` | U8 mark filter | Three U32 file counts | Count files by type |
| `03/01` | `dat_sel` | `dat_sel <type> <index> <chunk_size>` | U8 type, U16 index, U16 chunk size | 41-byte metadata | Select a file |
| `03/02` | `dat_get` | `dat_get <chunk>` | U32 chunk index | N raw chunk bytes | Download selected chunk |
| `03/03` | `dat_des` | `dat_des` | 0 | ACK | Deselect file |
| `03/04` | `dat_del` | `dat_del <type> <index>` | U8 type, U32 index | Three U32 file counts | Delete file |
| `03/05` | `dat_mrk` | `dat_mark <mark>` | U8 mark | ACK | Mark selected file |

File types are 0 HK, 1 SCI1, and 2 SCI2. The `dat_lst` filter is `0xFF` for all files, `0x00` for unmarked files, or `0x01`–`0xFE` for a particular mark. The PDF limits chunk size to **32768 bytes**, despite the U16 field's wider range. Chunk indexes are zero-based U32 values.

#### Current three-category storage layout

The current OBC implementation keeps the revision 1.0 frame sizes and command addresses, but assigns the three `dat_lst` counts and `dat_sel` data-type values as follows. This is a flight-software extension of the printed HK/SCI1/SCI2 labels.

| Type | Count / selector | Source folder under `/home/bee/data` | Filename pattern (20-byte maximum) |
| ---: | --- | --- | --- |
| `0` | Logs | `logs/hk/` | `HK0_YYYYMMDD.csv` (OBC), `HK1_YYYYMMDD.csv` (EXP) |
| `0` | Logs | `logs/lwl/` | `LWL_NNNNNNNN.bin` |
| `0` | Logs | `logs/pc/` | `PC_NNNNNNNN.bin` |
| `1` | Images | `images/usb/` | `USB0_NNNNNNNN.jpg` through `USB3_NNNNNNNN.jpg` |
| `1` | Images | `images/mipi/` | `MIPIc.s_H_NNNNNN.raw` and `.png` (current MIPI capture); `.jpg` may be added by another producer; `c=0..1`, `s=0..3`, `R=H` or `L` |
| `2` | Data | `data/lev/` | `LEV_NNNNNNNN.bin` |
| `2` | Data | `data/other/` | `OTHER_NNNNNNNN.bin` |

The `dat_sel` index is zero-based within its type, sorted by relative folder and filename at `dat_lst` time. Its 20-byte metadata field contains the basename; the OBC retains the folder internally. A 20-byte name has no trailing NUL. `dat_get` uses the selected path. Marks are keyed by relative path so matching basenames in separate folders remain distinct. `dat_del` uses the printed five-byte TC (`U8 type`, `U32 index`).

The current GUI MIPI capture retains a 5120×3840 BG10 `.raw` frame and saves a full-resolution `.png` with the same basename. Both appear in the image index. The PNG is made with `/home/root/tools/bin/raw_png` using `grbg` Bayer order; FSW discards that converter’s low-resolution intermediate. If conversion fails, the RAW remains and capture reports an error. JPEG and low-resolution files enter this index when their producers save them using a supported basename. The legacy EXP data-ready handoff routes `TypeName` prefixes (`LWL`, `PC`, `LEV`) into those folders and other prefixes to `data/other/`. The current EXP MCU instead exposes PSRAM object descriptors through `data_list` and `data_read`; `hk_service` does not yet ingest that interface, so current EXP LWL/PC objects will not appear until that bridge is updated. Existing files in former locations are not automatically migrated.

#### `dat_sel` response layout

Offsets here are from the **start of the TM frame**, as printed in the PDF.

| Offset | Field | Type/size in PDF |
| ---: | --- | --- |
| 5 | Data type | U8, 1 byte |
| 6 | Data index | U32, 4 bytes |
| 10 | Chunk size | Labeled U16 but size shown as 4 bytes; PDF discrepancy |
| 14 | File name | 20-byte string |
| 34 | File size | U32, 4 bytes |
| 38 | File CRC-32 | U32, 4 bytes |
| 42 | Chunk count | U32, 4 bytes |
| 46 | Frame CRC-16 | U16, 2 bytes |

The declared TM payload length is 41 bytes, which matches the offsets through byte 45 if chunk size occupies **four** bytes. Chunk count is the ceiling of file size divided by chunk size. The source does not specify string termination or padding rules for the 20-byte name.

#### Transfer sequence

1. Call `dat_lst`, optionally filtering by mark, and choose a file type and index.
2. Call `dat_sel` with a chunk size of at most 32768 bytes; record name, size, CRC-32, and chunk count.
3. Call `dat_get` for chunk indexes 0 through count − 1 and concatenate the returned bytes.
4. Verify the complete file against the CRC-32 from `dat_sel`. On mismatch, select and transfer again.
5. Call `dat_des` when complete.

The PDF estimates HK files at 10 KB each (binary), SCI1 at 1–2 MB each (JPG), and SCI2 at 100 KB each (binary).

### Telemetry

| TYPE/ADDR | ICD name | Console command | TC payload | TM payload | Purpose |
| --- | --- | --- | --- | --- | --- |
| `04/00` | `tm_get` | `tm_get` | 0 | PDF says 29 bytes; field table extends to byte 61 | Payload telemetry |
| `04/01` | `tm_cfg` | — | 0/8 | 8 bytes | Telemetry configuration |

#### `tm_get` field table, as printed

Offsets are from the **start of the TM frame**. The source does not define scaling or validity rules for these measurements.

| Offset | Field | Type | Bytes | Notes |
| ---: | --- | --- | ---: | --- |
| 5 | System time | U32 | 4 | Unix epoch |
| 9 | System status | I8 | 1 | — |
| 10 | Boot count | U32 | 4 | — |
| 14 | Onboard temperatures | I16[3] | 6 | PDU, EXP, OBC |
| 20 | NTC temperatures | I16[4] | 8 | NTC 0–3 |
| 28 | MLX average temperature | I16 | 2 | — |
| 30 | BME temperature | I16 | 2 | — |
| 32 | BME pressure | U16 | 2 | — |
| 34 | BME humidity | U16 | 2 | — |
| 36 | IMU data | I16[7] | 14 | Temperature, ax/ay/az, gx/gy/gz |
| 50 | SLF1 data | I16[2] | 4 | Temperature, flow |
| 54 | SLF2 data | I16[2] | 4 | Temperature, flow |
| 58 | RBA status | U16 | 2 | — |
| 60 | TA status | U16 | 2 | — |
| 62 | Frame CRC-16 | U16 | 2 | — |

## Detailed command payload layouts

The tables above give every revision 1.0 command. This section records the byte positions and types in their detailed TC/TM tables (PDF pages 12–35). Offsets are measured from the first `0xA5` byte, not from the payload. Every command has TYPE at byte 1, ADDRESS at byte 2, little-endian LENGTH at bytes 3–4, payload starting at byte 5, and CRC-16 immediately after the payload. Thus a payload of length *N* puts CRC at byte `5+N`; total frame length is `7+N`. The detailed tables' `TC Payload` and `TM Payload` labels do **not** define the meaning of their individual bytes.

`0/n` means TC LENGTH 0 reads the configuration and TC LENGTH *n* writes *n* bytes. The PDF prints one TM layout per command and does not give separate get and set reply schemas. `U8[n]` means *n* uninterpreted bytes; `U16` and `U32` follow the ICD's little-endian rule. For zero-length requests CRC is `0xFFFF`. For errors, the generic TM ERR frame uses byte 1 = `0xFF` and byte 2 = the error code; the PDF does not define a command-specific error payload.

### Ping and system configuration (TYPE `0x00`/`0x01`, PDF pages 12–25)

| TYPE/ADDR | TC payload at byte 5 | TC CRC offset | TM payload at byte 5 | TM CRC offset |
| --- | --- | ---: | --- | ---: |
| `00/00` `ping` | none | 5 | Generic ACK: U32 uptime (seconds) | 9 |
| `01/00` `sys_info` | 0/1 U8; write `0x01` for self-test | 5/6 | U8[96] system information | 101 |
| `01/01` [`sys_rtc_cfg`](#sys_rtc_cfg-0x01) | 0/4 U32 Unix epoch UTC | 5/9 | U32 current epoch | 9 |
| `01/02` [`pwr_adj_cfg`](#pwr_adj_cfg-0x02) | 0/8 U8[8] | 5/13 | U8[8] “power config status” | 13 |
| `01/03` `pwr_en` | 0/2 U16 power enable data | 5/7 | U16 power enable status | 7 |
| `01/04` `pwr_stat` | U8 report selector (one byte) | 6 | Variable-length payload labeled U16 | `5+N` |
| `01/05` [`sen_bme_cfg`](#sen_bme_cfg-0x05) | 0/8 U8[8] | 5/13 | U8[8] BME configuration | 13 |
| `01/06` [`sen_imu_cfg`](#sen_imu_cfg-0x06) | 0/8 U8[8] | 5/13 | U8[8] IMU configuration | 13 |
| `01/07` [`sen_stat`](#sen_stat-0x07) | Printed: U8 selector; current: board/group/source/cursor U8[4] | Printed: 6; current: 9 | Current: 10 + 12×N bytes | `15+12×N` |
| `01/10` [`uvc_cfg`](#current-uvc_cfg-implementation-type-0x01-addr-0x10) | Printed: 0/32; current: 1/32 U8[32] | 6/37 | [U8[32] camera configuration](#current-uvc_cfg-implementation-type-0x01-addr-0x10) | 37 |
| `01/11` [`uvc_ctl`](#current-uvc_ctl-and-uvc_stat-implementation) | U8[8] capture request | 13 | [Current: U8[8] job state](#current-uvc_ctl-and-uvc_stat-implementation); printed: generic ACK | Current: 13; printed: 9 |
| `01/12` [`uvc_stat`](#uvc_stat-response-details) | Printed: none; current: camera U8 + request ID U32 LE | Printed: 5; current: 10 | [Current: U8[8] job status](#uvc_stat-response-details); printed: 6-byte opaque status | Current: 13; printed: 11 |
| `01/13` [`thm_cfg`](#thm_cfg-0x13) | 0/32 U8[32] | 5/37 | U8[32] manual heater and IR limits | 37 |
| `01/14` [`thm_ctl`](#thm_ctl-0x14) | U8[8] manual control | 13 | Valve ACK: 0 bytes; pump ACK: request ID U32 LE | 9/13 |
| `01/15` [`thm_stat`](#thm_stat-0x15) | none | 5 | 6-byte thermal status, labeled U16 | 11 |
| `01/16` [`fld_cfg`](#fld_cfg-current-payload-layout) | 0/32 U8[32] | 5/37 | U8[32] paged configuration, set echo, or [pump result](#fld_cfg-pump-detection) | 37 |
| `01/17` [`fld_ctl`](#fld_ctl-manual-valve-and-pump-control) | U8[8] manual control | 13 | Valve ACK: 0 bytes; pump ACK: request ID U32 LE | 9/13 |
| `01/18` [`fld_stat`](#fld_stat-actuator-status) | none | 5 | 16-byte actuator status | 21 |
| `01/19` [`rba_cfg`](#rba_cfg-0x19) | 0/32 U8[32] | 5/37 | U8[32] RBA configuration; fields reserved | 37 |
| `01/1A` [`rba_ctl`](#current-rba_ctl-implementation) | U8[8] manual control | 13 | Stop/disable: 0 bytes; motion: request ID U32 LE; result: 12 bytes | 7/11/19 |
| `01/1B` [`rba_stat`](#current-rba_stat-implementation) | none | 5 | 16-byte motor/limit status | 23 |
| `01/20` `lev_calib` | 0/130 U8[130] in length row; footnote says 128-byte write | 5/135 per length row | U8[130] LEV calibration | 135 |
| `01/21` `lev_cfg` | 0/32 U8[32] | 5/37 | U8[32] LEV configuration | 37 |
| `01/22` `lev_ctl` | U8[8] manual control | 13 | Valve ACK: 0 bytes; pump ACK: request ID U32 LE | 9/13 |
| `01/23` `lev_stat` | none | 5 | 16-byte LEV status, labeled U16 | 21 |

The `lev_calib` detail prints ADDRESS `0x21` in both TC and TM despite the summary's `0x20`, colliding with `lev_cfg`. The `pwr_stat` and `sen_stat` TC LENGTH cells print 2 despite their single U8 selector and CRC at byte 6. The `sen_stat` detail also calls itself “Get system power status,” inconsistent with its sensor report selectors. `sys_rtc_cfg` prints CRC offset 9 even for the zero-length form; the generic frame gives offset 5 for that form. These PDF conflicts are retained rather than silently corrected.

### Experiment control (TYPE `0x02`, PDF pages 25–30)

| TYPE/ADDR | TC payload at byte 5 | TC CRC offset | TM payload at byte 5 | TM CRC offset |
| --- | --- | ---: | --- | ---: |
| `02/00` [`pc_stat`](#pc_stat-0x00) | none | 5 | U8 PC status; current bit layout | 6 |
| `02/01` [`pc_cfg`](#pc_cfg-0x01) | 0/32 U8[32] | 5/37 | [U8[32] PC experiment configuration](#pc_cfg-current-payload-layout) | 37 |
| `02/02` [`pc_ctl`](#pc_ctl-0x02) | U8[8] PC experiment control | 13 | Zero-byte ACK; [payload layout](#pc_ctl-0x02) | 9 |
| `02/03` [thp_cfg](#thp_cfg-0x03) | 32 U8[32] | 37 | U8[32] selected thermal profile | 37 |
| `02/04` [`thp_ctl`](#thp_ctl-0x04) | 1 U8 | 6 | U8 thermal profile status | 6 |
| `02/05` `mon_cfg` | 0/32 U8[32] | 5/37 | U8[32] monitoring configuration | 37 |
| `02/06` `mon_en` | 0/1 U8 | 5/6 | U8 monitoring enable state | 6 |
| `02/07` `rsv_op1` | U8[32]; detail says LEV manual control | 37 | Generic ACK | 9 |
| `02/08` `rsv_op2` | U8[32]; detail says monitoring configure | 37 | U8[32] | 37 |
| `02/09` `rsv_op3` | 0/32 U8[32]; detail says monitoring configure | 5/37 | U8[32] | 37 |

The printed `thp_en` and `mon_en` rows give the write-form TC CRC offset without the zero-length read offset; the generic frame puts it at byte 5. The reserved-operation names are from the summary; the subsystem descriptions are from the detailed tables.

### Data transfer (TYPE `0x03`, PDF pages 30–33)

| TYPE/ADDR | TC payload layout and frame offsets | TM payload layout and frame offsets |
| --- | --- | --- |
| `03/00` `dat_lst` | U8 mark filter at 5; CRC at 6 | U32 HK count at 5, U32 SCI1 count at 9, U32 SCI2 count at 13; LENGTH 12, CRC at 17 |
| `03/01` `dat_sel` | U8 data type at 5 (0 HK, 1 SCI1, 2 SCI2); U16 data index at 6 (0–65535); U16 requested chunk size at 8 (1–65535 in field, restricted to 32768 bytes by note); LENGTH 5, CRC at 10 | 41-byte metadata detailed above; CRC at 46 |
| `03/02` `dat_get` | U32 chunk index at 5 (0–`2^32−1`); LENGTH 4, CRC at 9 | N raw chunk bytes starting at 5; CRC at `5+N` |
| `03/03` `dat_des` | none; CRC `0xFFFF` at 5 | Generic ACK; CRC at 9 |
| `03/04` `dat_del` | U8 data type at 5, U32 data index at 6; LENGTH 5, CRC at 10 | Same three U32 counts as `dat_lst`; LENGTH 12, CRC at 17 |
| `03/05` `dat_mrk` | U8 mark value at 5; LENGTH 1, CRC at 6 | Generic ACK; CRC at 9 |

The PDF's transfer flow is `dat_lst` → `dat_sel` → repeated `dat_get` with increasing chunk index → compare the completed file CRC-32 → retry selection/transfer if mismatched → `dat_des` when complete. File type 0 is HK (approximately 10 KB per file, binary), type 1 is SCI1 (approximately 1–2 MB per file, JPG), and type 2 is SCI2 (approximately 100 KB per file, binary). The PDF gives no binary schema for HK or SCI2 files.

### Telemetry (TYPE `0x04`, PDF pages 33–35)

| TYPE/ADDR | TC payload at byte 5 | TC CRC offset | TM payload at byte 5 | TM CRC offset |
| --- | --- | ---: | --- | ---: |
| `04/00` `tm_get` | none | 5, `0xFFFF` | Field table above; printed LENGTH 29 conflicts with listed 57 bytes | 62 as printed |
| `04/01` `tm_cfg` | 0/8 U8[8] | 5/13 | U8[8] telemetry configuration | 13 |

Revision 1.0 lists TYPE `0x05` as SFU but gives no SFU command addresses, payloads, replies, or transfer procedure.

## Source discrepancies to resolve in the next ICD revision

These are transcription issues in the PDF, **not protocol changes made by this page**:

| PDF location | What conflicts |
| --- | --- |
| TMTC summary and `lev_calib` detail | Summary assigns `lev_calib` address `0x20`; detailed TC and TM rows print `0x21`, the same address as `lev_cfg`. |
| `lev_calib` TC | LENGTH and payload columns say 0/130 bytes; footnote says a 128-byte set payload. |
| `pwr_stat` and `sen_stat` TC | LENGTH is printed as 2, but each defined selector is one U8 at offset 5 and CRC starts at offset 6. |
| `sen_stat` detail heading | Description says “Get system power status,” while its selectors and reply are sensor data. |
| Zero-length form of `sys_rtc_cfg`, printed `thp_en`, and `mon_en` | The detailed CRC offset prints the write-form location (byte 9 or 6); the generic frame puts CRC at byte 5 when TC LENGTH is zero. |
| `pwr_adj_cfg` and other opaque configurations | The printed PDF specifies payload lengths and “power config status” but no byte meanings. The current [flight software layout](#pwr_adj_cfg-0x02) defines a two-byte VOUT setpoint; other opaque commands still need definitions. |
| `dat_sel` TM | Chunk size says U16 but occupies four bytes in the size/offset columns. The metadata LENGTH of 41 matches a four-byte field. |
| `tm_get` TM | LENGTH is printed as 29, but listed fields occupy bytes 5–61, implying a 57-byte payload before the CRC at 62. |
| CRC-16 scope | The PDF specifies parameters and the empty-payload result but does not explicitly say whether frame header bytes enter the CRC. Current flight software uses payload bytes only. |
| Several status TM tables | A status field is labeled U16 while its size is 6 or 16 bytes; interpret as an opaque array pending a defined schema. |
| Revision 1.0 command summary | The PDF itself says its summary table needs updating. Current flight software adds commands beyond this PDF; use [flight software commands](flight-software.md) to track those separately. |

When revising this page, update the command register and any affected detailed layout together, then record the agreed resolution of these discrepancies. Keep the fixed `START/LENGTH/CRC` frame separate from command-table revisions.

## Revision 1.0 commands without a flight handler

These 13 revision 1.0 addresses have no handler in the current dispatcher.
The original TYPE/ADDRESS values are retained. Sending them with `raw` reaches
the dispatcher but does not implement the operation. The complete
[source-based coverage table](flight-software-architecture.md#revision-10-icd-coverage)
also lists the 30 routed commands.

| ICD group | Missing commands |
| --- | --- |
| System configuration (`0x01`) | [`01/05 sen_bme_cfg`](#sen_bme_cfg-0x05), [`01/06 sen_imu_cfg`](#sen_imu_cfg-0x06); the current EXP configuration route is `02/0C` |
| RBA (`0x01`) | [01/19 rba_cfg](#rba_cfg-0x19) |
| LEV (`0x01`) | `01/20 lev_calib`, `01/21 lev_cfg`, `01/22 lev_ctl`, `01/23 lev_stat`. The PDF's detailed `lev_calib` table conflicts on its original address. |
| Experiment (`0x02`) | `02/05 mon_cfg`, `02/06 mon_en`, `02/07 rsv_op1`, `02/08 rsv_op2`, `02/09 rsv_op3` |
| Telemetry (`0x04`) | `04/01 tm_cfg` |

The `sys_info` command supports the zero-length information read, not the proposed one-byte self-test. `dat_del` uses the printed U8 type plus U32 index (five-byte payload). The [current command register](flight-command-register.md) defines what to send to the running software.

## Current flight commands absent from the revision 1.0 ICD

These operations are supported by the current flight software but have **no row in the PDF's command summary**:

| Console command | Current RS-422 counterpart | Purpose |
| --- | --- | --- |
| `tm_obc_get` | `04/02` | 64-byte OBC housekeeping telemetry extension |
| `exp_ping` | `02/0A` | EXP UART reachability via `tlm_json system` |
| `exp_tlm <section>` | `02/0B` | EXP UART schema-2 telemetry with chunk reassembly |
| `exp_run <EXP command>` | `02/0C` | EXP UART text CLI command and response |
| `debug_handoff yes` | `FF/00` with ASCII `DEBUG_HANDOFF_OK` | Debug UART handoff; special successful ACK `0xFE` |
| `shell <Linux command>` | `FF/01` with command text bytes | Debug shell extension; special successful ACK `0xFE` |

## Console operations without a dedicated RS-422 command

These socket/client operations have **no direct binary RS-422 TYPE/ADDRESS**:

| Console operation | Scope |
| --- | --- |
| `help` | Lists local console commands. |
| `gui_state` | Returns a JSON snapshot for the terminal GUI. |
| `gui_shell <seconds> <command>` | Runs a local shell command with a timeout and returns JSON. The RS-422 `FF/01` shell is a separate handler with a different request and response format. |
| `raw <type> <address> [byte ...]` | Builds an arbitrary TC for the same dispatcher; it is a local parser feature, not an ICD address. |
| `--json`, interactive `json on` / `json off` | Local client response formatting. |
| Interactive `exit` / `quit` | Closes the console client. |

The new `02/0A`–`02/0C` extensions provide native RS-422 routes to the EXP UART text CLI. Running another Linux tool through `shell` or `gui_shell` still wraps a command in the shell extension or local socket operation; it does not add a dedicated PDU, camera, or power-control address.

## Source tables: printed pages 10–36

The tables below reproduce every table on printed pages 11–36, plus page 10 where the command summary starts. Values, offsets, footnotes, and conflicts follow the revision 1.0 PDF. Line wrapping is normalized for Markdown; the [source PDF](assets/icd/BEE-DAL_ICDr1p0.pdf) remains authoritative. The page 36 flowchart is described in the transfer sequence above.

### Printed page 10 (PDF page 6)

TMTC Summary
Table 8: BEE-DAL TMTC Summary


| TYPE | ADDRESS | Name | Description | Response |
| --- | --- | --- | --- | --- |
| 0x00 | 0x00 | [ping](#ping-communication-check) | Communication check | ACK |
| 0x01 | 0x00 | [sys_info](#sys_info-0x00) | System information | info data |
| 0x01 | 0x01 | [sys_rtc_cfg](#sys_rtc_cfg-0x01) | Configure system clock | current epoch |
| 0x01 | 0x02 | [pwr_adj_cfg](#pwr_adj_cfg-0x02) | Configure adjustable power | power config status |
| 0x01 | 0x03 | pwr_en | Enable/Disable power lines | power en status |
| 0x01 | 0x04 | pwr_stat | Get system power statuses | power status |
| 0x01 | 0x05 | [sen_bme_cfg](#sen_bme_cfg-0x05) | Sensor BME configuration | BME config |
| 0x01 | 0x06 | [sen_imu_cfg](#sen_imu_cfg-0x06) | Sensor IMU configuration | IMU config |
| 0x01 | 0x07 | [sen_stat](#sen_stat-0x07) | Sensor status | Sensor status |
| 0x01 | 0x10 | [uvc_cfg](#current-uvc_cfg-implementation-type-0x01-addr-0x10) | UVC configuration | [32-byte payload details](#current-uvc_cfg-implementation-type-0x01-addr-0x10) |
| 0x01 | 0x11 | [uvc_ctl](#current-uvc_ctl-and-uvc_stat-implementation) | Capture one JPEG | [Queue status layout](#current-uvc_ctl-and-uvc_stat-implementation) |
| 0x01 | 0x12 | [uvc_stat](#uvc_stat-response-details) | Capture request status | [Response states and errors](#uvc_stat-response-details) |
| 0x01 | 0x13 | [thm_cfg](#thm_cfg-0x13) | Manual heater and IR limits | [Payload layout](#thm_cfg-payload-layout) |
| 0x01 | 0x14 | [thm_ctl](#thm_ctl-0x14) | Thermal control | [Current layout](#current-thm_ctl-implementation) |
| 0x01 | 0x15 | [thm_stat](#thm_stat-0x15) | Thermal status | [Current layout](#current-thm_stat-implementation) |
| 0x01 | 0x16 | [fld_cfg](#fld_cfg-current-payload-layout) | Fluidic mapping, profiles, [pump detection](#fld_cfg-pump-detection) | 32-byte page, set echo, or pump result |
| 0x01 | 0x17 | [fld_ctl](#fld_ctl-manual-valve-and-pump-control) | Manual valve and pump control | Valve ACK or pump request ID |
| 0x01 | 0x18 | [fld_stat](#fld_stat-actuator-status) | Valve and pump actuator status | 16-byte actuator status |
| 0x01 | 0x19 | rba_cfg | RBA configuration | RBA config |
| 0x01 | 0x1A | [rba_ctl](#current-rba_ctl-implementation) | RBA manual control | 0/4/12-byte reply |
| 0x01 | 0x1B | [rba_stat](#current-rba_stat-implementation) | RBA status | 16-byte motor/limit status |
| 0x01 | 0x20 | lev_calib | LEV calibration | LEV calib data |
| 0x01 | 0x21 | lev_cfg | LEV configuration | LEV config |
| 0x01 | 0x22 | lev_ctl | LEV control | ACK |
| 0x01 | 0x23 | lev_stat | LEV status | LEV status |
| 0x02 | 0x00 | [pc_stat](#pc_stat-0x00) | PC experiment status | 1-byte PC status |
| 0x02 | 0x01 | [pc_cfg](#pc_cfg-0x01) | PC experiment configuration | PC config |
| 0x02 | 0x02 | [pc_ctl](#pc_ctl-0x02) | PC experiment control | 0-byte ACK |
| 0x02 | 0x03 | [thp_cfg](#thp_cfg-0x03) | Thermal profile configuration | THP config |
| 0x02 | 0x04 | [thp_ctl](#thp_ctl-0x04) | Thermal profile control | One-byte status |
| 0x02 | 0x05 | mon_cfg | Monitoring configuration | MON config |
| 0x02 | 0x06 | mon_en | Monitoring enable | ACK |
| 0x02 | 0x07 | rsv_op1 | Reserved operation #1 | ACK |
| 0x02 | 0x08 | rsv_op2 | Reserved operation #2 | RSV #2 response |
| 0x02 | 0x09 | rsv_op3 | Reserved operation #3 | RSV #3 response |
| 0x03 | 0x00 | dat_lst | Get number of data files | count of data files by type |
| 0x03 | 0x01 | dat_sel | Select file to download | file metadata |
| 0x03 | 0x02 | dat_get | Download (chunks) data | (chunks) data |
| 0x03 | 0x03 | dat_des | Release current file | ACK |
| 0x03 | 0x04 | dat_del | Delete file data | count of data files |

### Printed page 11 (PDF page 7)

| TYPE | ADDRESS | Name | Description | Response |
| --- | --- | --- | --- | --- |
| 0x03 | 0x05 | dat_mrk | Mark data file | ACK |
| 0x04 | 0x00 | tm_get | Payload telemetry get | TM data |
| 0x04 | 0x01 | tm_cfg | Payload TM configuration | TM config |

NOTE: This summary table supposes to be updated.

### Printed page 12 (PDF page 8)

TMTC Tables

#### Ping — Communication check

PING (TYPE = 0x00)

Communication check.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x00 |
| ADDR | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | 0xFFFF |

**TM**

TM ACK package

System Conﬁguration (TYPE = 0x01)

#### sys_info (0x00)

Get system information.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 0/1 (footnote 1) |
| TC Payload | U8 | 0/1 | 5 | - |
| CRC-16 | U16 | 2 | 5/6 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 1: set command, write 0x1 to TC Payload will perform a system self-test


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 96 |
| TM Payload | U8 | 96 | 5 | - |
| CRC-16 | U16 | 2 | 101 | - |

The printed ICD fixes the TM payload at 96 bytes but does not define its internal
fields. The following is the **current flight software format** (schema 1);
offsets are relative to the TM payload, which starts at frame byte 5.

| Payload offset | Size (B) | Type | Field | Meaning |
| ---: | ---: | --- | --- | --- |
| 0 | 1 | U8 | Schema version | `0x01` |
| 1 | 1 | U8 | Board count | `0x03` |
| 2 | 1 | U8 | Online mask | Bit 0 PDU, bit 1 EXP, bit 2 OBC; set means online |
| 3 | 1 | U8 | Reserved | `0x00` |
| 4–7 | 4 | U32 LE | Sample time | OBC Unix time in seconds when the command runs |
| 8–11 | 4 | U8[4] | Reserved | Zero |
| 12–39 | 28 | Board record | PDU | Board ID `0x01` |
| 40–67 | 28 | Board record | EXP | Board ID `0x02` |
| 68–95 | 28 | Board record | OBC | Board ID `0x03` |

Each board record has this layout. Add the relative offset to the record start
(12 for PDU, 40 for EXP, 68 for OBC) to get the payload offset.

| Record offset | Size (B) | Type | Field | Meaning |
| ---: | ---: | --- | --- | --- |
| 0 | 1 | U8 | Board ID | 1 PDU, 2 EXP, 3 OBC |
| 1 | 1 | U8 | Online | `0x01` if the direct read succeeded; `0x00` if it failed or timed out. OBC is local and always online while this command runs. |
| 2–4 | 3 | U8[3] | Hardware revision | Major, minor, patch. EXP uses RPARAM table 0; PDU and OBC use configured values. |
| 5–7 | 3 | U8[3] | Firmware version | Major, minor, patch; `FF FF FF` when unavailable. |
| 8–27 | 20 | ASCII[20] | Build ID | NUL-terminated and zero-padded. PDU uses eight hexadecimal digits from its Modbus build code; EXP uses RPARAM table 12; OBC is empty. |

PDU firmware version/build come from Modbus holding registers 40100–40109;
EXP firmware/hardware versions come from RPARAM table 0 and its build ID from
table 12. OBC firmware and PDU/OBC hardware revisions are configuration constants.
Only the zero-length get TC is implemented; the printed one-byte self-test TC
is not implemented.

For the supplied payload, header `01 03 06 00 40 c8 7e 67 00 00 00 00`
means schema 1, three boards, EXP and OBC online, and OBC sample time
`0x677EC840` (2025-01-08 18:47:28 UTC). The PDU record has online `00`,
hardware `1.0.0`, firmware `FF FF FF`, and an empty build ID. EXP has online
`01`, hardware/firmware `1.0.0`, and build `4dc7bbb-dirty`. OBC has online
`01`, hardware `1.0.0`, firmware `0.1.0`, and an empty build ID.

### Printed page 13 (PDF page 9)

#### sys_rtc_cfg (0x01)

Conﬁgure system clock


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 0/4 (footnote 1) |
| Unix epoch (UTC) | U32 | 0/4 | 5 | - |
| CRC-16 | U16 | 2 | 9 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 4: set command with Unix epoch (4B)


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 4 |
| System epoch | U32 | 4 | 5 | - |
| CRC-16 | U16 | 2 | 9 | - |

**Current console and RS-422 format**

| Console location | Command | TC operation |
| --- | --- | --- |
| OBC `dal_flight_console` | `sys_rtc` | Get: zero-byte payload; reads the OBC Linux system clock. |
| OBC `dal_flight_console` | `sys_rtc <epoch>` | Set: U32 Unix epoch in UTC; decimal or `0x` hexadecimal. |
| OBC `dal_flight_console` | `sys_rtc now` | Set using the OBC's current system time; it does not synchronize from an external clock. |
| PC RS-422 client | `sys_rtc` | Get: zero-byte payload. |
| PC RS-422 client | `sys_rtc <epoch>` or `sys_rtc now` | Set using the given epoch or the PC's current Unix time. |

The local OBC console calls the same command handler without transmitting over
RS-422. A PC client sends the following TC frame. All multi-byte integers are
little-endian. The CRC is non-reflected CRC-16/CCITT (polynomial `0x1021`,
initial `0xFFFF`, no final XOR), calculated over **payload bytes only** and sent
least-significant byte first.

| Frame byte offset | Size (B) | Field | Get value | Set value |
| ---: | ---: | --- | --- | --- |
| 0 | 1 | START | `A5` | `A5` |
| 1 | 1 | TYPE | `01` (system configuration) | `01` |
| 2 | 1 | ADDR | `01` (`sys_rtc_cfg`) | `01` |
| 3–4 | 2 | LENGTH, U16 LE | `00 00` (no TC payload) | `04 00` (four TC payload bytes) |
| 5–8 | 0 or 4 | Unix epoch, U32 LE | Absent | Seconds since 1970-01-01 00:00:00 UTC, range 0–4294967295 |
| 5–6 (get) or 9–10 (set) | 2 | CRC-16 LE | `FF FF` (CRC of empty payload) | CRC of the four epoch bytes |

The complete get TC is `A5 01 01 00 00 FF FF` (7 bytes). For set epoch
`1700000000` (`0x6553F100`), the complete TC is
`A5 01 01 04 00 00 F1 53 65 4D 37` (11 bytes): the epoch bytes are
`00 F1 53 65`, and their payload CRC is `0x374D`, sent as `4D 37`.

Both operations return the same successful TM structure:

| Frame byte offset | Size (B) | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | START | `A5` |
| 1 | 1 | ACK/ERR | `01` echoes the TC TYPE on success. |
| 2 | 1 | ADDR/ERRCODE | `01` echoes the TC ADDR on success. |
| 3–4 | 2 | LENGTH, U16 LE | `04 00`: four TM payload bytes. |
| 5–8 | 4 | System epoch, U32 LE | Linux system time in UTC seconds when the reply is generated. For get, this is the current time; for set, it is read immediately after setting the time. |
| 9–10 | 2 | CRC-16 LE | CRC of the four TM epoch bytes, least-significant byte first. |

The get form does not read or modify the hardware RTC. The set form updates
the Linux clock, then attempts to write `/dev/rtc2`; a successful TM does not
prove that the hardware RTC write succeeded. Only TC lengths 0 and 4 are
accepted. On an invalid length, the error TM has ACK/ERR `FF`, error code
`00`, length `00 00`, and CRC `FF FF`; if setting the Linux clock fails,
the error code is `03`.

#### pwr_adj_cfg (0x02)

Conﬁgure adjustable power


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x02 |
| LENGTH | U16 | 2 | 3 | 0/8 (footnote 1) |
| TC Payload | U8 | 0/8 | 5 | - |
| CRC-16 | U16 | 2 | 5/13 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 8: set command with 8B TC payload


**TM**

### Printed page 14 (PDF page 10)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x02 |
| LENGTH | U16 | 2 | 3 | 8 |
| TM Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

**Current flight software mapping (supersedes the printed TC length for this command)**

The console commands are `pdu_vout get` and `pdu_vout set <mV>`
(`pwr_adj_cfg get/set` is an alias on the OBC console). The PC RS-422
client uses `pdu_vout get/set`. The set value is an unsigned integer from
800 to 20000 mV. Both forms use TYPE `01`, ADDR `02`; all multi-byte
payload fields are little-endian. This mapping has a **two-byte** set TC,
whereas the printed revision 1.0 table above says eight bytes. An eight-byte
set TC is rejected by current flight software.

| Operation | TC LENGTH | TC payload at frame byte 5 | CRC location | Complete example |
| --- | ---: | --- | --- | --- |
| Get | `00 00` | None | Bytes 5–6 | `A5 01 02 00 00 FF FF` |
| Set | `02 00` | U16 target VOUT in mV, range 800–20000 | Bytes 7–8 | 12000 mV: `A5 01 02 02 00 E0 2E 11 C8` |

The CRC is non-reflected CRC-16/CCITT, polynomial `0x1021`, initial
`0xFFFF`, no final XOR, over the **TC payload only**; it is transmitted
low byte first. `E0 2E` is 12000 mV in little-endian form, and `11 C8`
is its CRC. The zero-length request has CRC `FF FF`.

A successful get or set returns the printed eight-byte TM, now defined as:

| TM payload offset (frame byte) | Size (B) | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 (5) | 2 | U16 setpoint mV | PDU commanded VOUT setpoint; `FFFF` if none has been set since reset. |
| 2 (7) | 2 | U16 measured VOUT mV | Latest PDU PAC1934 voltage sample; `FFFF` if unavailable. |
| 4 (9) | 2 | U16 measured VOUT mA | Latest PDU PAC1934 current sample; `FFFF` if unavailable. |
| 6 (11) | 2 | U16 flags | Bit 0 TRANS commanded on; bit 1 VOUT enabled; bit 2 setpoint valid; bit 3 voltage valid; bit 4 current valid; bits 5–15 reserved zero. |

The complete successful TM is `A5 01 02 08 00 <8 TM payload bytes>
<2 CRC bytes>`; its CRC follows the payload at frame bytes 13–14 and is
calculated over those eight bytes only. The measured values are samples and
may not yet have settled to a newly commanded voltage.

Flight software reads the PDU over Modbus for both operations. Before a
set, it verifies the PDU product code and writes holding register `40001`
with Modbus function 06. PDU firmware powers TRANS if needed, probes the
TPS55287 buck-boost IC, and applies the VOUT setpoint; the TC has no TRANS
control field and sends no separate VOUT enable request. A set can therefore
turn TRANS on while leaving VOUT enable in its prior state. Flight software rejects
other TC lengths with error code `00`, out-of-range setpoints with `01`,
and PDU communication/readback failures with `03`. A write may have applied
even if the subsequent readback fails and produces error `03`.

#### pwr_en (0x03)

Enable/Disable power lines


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x03 |
| LENGTH | U16 | 2 | 3 | 0/2 (footnote 1) |
| TC Payload | U16 | 0/2 | 5 | - |
| CRC-16 | U16 | 2 | 5/7 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 2: set command with 2B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x03 |
| LENGTH | U16 | 2 | 3 | 2 |
| TM Payload | U16 | 2 | 5 | - |
| CRC-16 | U16 | 2 | 7 | - |

#### pwr_stat (0x04)

Get system power status


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x04 |
| LENGTH | U16 | 2 | 3 | 2 |
| Power report type (footnote 1) | U8 | 1 | 5 | - |

### Printed page 15 (PDF page 11)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| CRC-16 | U16 | 2 | 6 | - |

**Footnote 1**
Power report type:

- 0: All statuses
- 1: Monitor status
- 2: Enable status
- 3: Error status


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x04 |
| LENGTH | U16 | 2 | 3 | N |
| TM Payload | U16 | N | 5 | - |
| CRC-16 | U16 | 2 | 5+N | - |

#### sen_bme_cfg (0x05)

Conﬁgure BME sensor

**Current EXP console command:** `sen_bme_cfg EXP get`,
`sen_bme_cfg EXP set <temp_os> <press_os> <hum_os> <filter>`, or
`sen_bme_cfg EXP result <id>`. Use oversampling names `skip`, `1x`, `2x`,
`4x`, `8x`, `16x`; use filter coefficients `off`, `1`, `3`, `7`, `15`,
`31`, `63`, `127`. Example: `sen_bme_cfg EXP set 2x 4x 1x 3`.
A set returns `QUEUED id=N`; poll `sen_bme_cfg EXP result N` until `DONE`
or `FAILED`. The command is encoded through [`02/0C exp_run`](#current-exp-console-route-for-sen_bme_cfg).
The printed `01/05` TC/TM tables below remain the original ICD format;
OBC flight software has no native `01/05` handler or runtime OBC BME setter.
The EXP firmware source also contains the BME guide at
`service/ref/https-github.com-baobuibk-SpaceLiin_BEE-DAL_EXP_V1.0_FW/docs/testing/19_bme688.md`.



**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x05 |
| LENGTH | U16 | 2 | 3 | 0/8 (footnote 1) |
| TC Payload | U8 | 0/8 | 5 | - |
| CRC-16 | U16 | 2 | 5/13 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 8: set command with 8B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x05 |
| LENGTH | U16 | 2 | 3 | 8 |
| TM Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

### Current EXP console route for sen_bme_cfg

The OBC console translates the commands below into `TYPE=0x02, ADDR=0x0C`
(`exp_run`). The TC payload is the ASCII command, without a NUL terminator.
The RS-422 frame is `A5 02 0C <LENGTH U16 LE> <ASCII bytes> <CRC-16 U16 LE>`;
CRC covers the payload bytes, as for other current flight commands.
This route does **not** use the printed `01/05` eight-byte TC/TM layout.

| OBC console command | `02/0C` TC payload (ASCII) | EXP behavior |
| --- | --- | --- |
| `sen_bme_cfg EXP get` | `bme688_cfg_get` | Reads last successfully applied settings without I²C access |
| `sen_bme_cfg EXP set 2x 4x 1x 3` | `bme688_cfg_set 2 3 1 2 0 0 0` | Queues new settings; returns request ID |
| `sen_bme_cfg EXP result 17` | `bme688_result 17` | Reads completion status for request 17 |

The successful `02/0C` TM payload is `status U8` (offset 0; zero means
successful UART command), `text_length U16 LE` (offsets 1–2), then the EXP
CLI UTF-8 text (`text_length` bytes from offset 3). An EXP `QUEUED` reply
means the configuration was accepted; it does not mean it has been applied.
Poll `result` for `DONE`, `FAILED`, `DISABLED`, or `NOT_FOUND`.
A transport failure returns a nonzero status in the same TM payload. A malformed
TC receives the generic RS-422 error frame.

The EXP firmware configuration fields are listed below for interpreting the
CLI values. The native `01/05` eight-byte payload has **no implemented
parser**; these offsets describe the PC experiment configuration structure, not a
currently transmitted `01/05` payload.

| Config offset | Field | Type | Supported EXP values |
| ---: | --- | --- | --- |
| 0 | Temperature oversampling | U8 | 0=skip, 1=1x, 2=2x, 3=4x, 4=8x, 5=16x |
| 1 | Pressure oversampling | U8 | Same codes |
| 2 | Humidity oversampling | U8 | Same codes |
| 3 | IIR filter | U8 | 0=off, 1=coefficient 1, 2=3, 3=7, 4=15, 5=31, 6=63, 7=127 |
| 4 | Flags | U8 | Must be zero; gas path unsupported |
| 5–6 | Heater temperature | U16 LE, °C | Must be zero |
| 7 | Heater duration | U8, 10 ms units | Must be zero |

Default EXP values are `02 03 01 02 00 00 00 00` (temperature 2x,
pressure 4x, humidity 1x, filter coefficient 3). Configuration is volatile.

### Printed page 16 (PDF page 12)

#### sen_imu_cfg (0x06)

Conﬁgure IMU sensor

**Current EXP console command:** `sen_imu_cfg EXP get`, `reset`,
`set accel <odr> <scale>`, `set gyro <odr> <scale>`,
`set freefall <threshold> <duration>`, `set wakeup <threshold> <duration>`,
or `result <id>`. For example, `sen_imu_cfg EXP set accel 208Hz 8g`.
A set or reset queues an EXP request; poll the returned ID with
`sen_imu_cfg EXP result <id>`. The command uses the
[`02/0C` route](#current-exp-console-route-for-sen_imu_cfg).
The printed `01/06` tables below are retained from the PDF; there is no
native OBC `01/06` parser. The EXP firmware source also contains the IMU guide
at `service/ref/https-github.com-baobuibk-SpaceLiin_BEE-DAL_EXP_V1.0_FW/docs/testing/15_lsm6dsox.md`.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x06 |
| LENGTH | U16 | 2 | 3 | 0/8 (footnote 1) |
| TC Payload | U8 | 0/8 | 5 | - |
| CRC-16 | U16 | 2 | 5/13 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 8: set command with 8B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x06 |
| LENGTH | U16 | 2 | 3 | 8 |
| TM Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

### Current EXP console route for sen_imu_cfg

The OBC console maps `sen_imu_cfg` to `TYPE=0x02, ADDR=0x0C` (`exp_run`).
The TC frame is `A5 02 0C <LENGTH U16 LE> <ASCII bytes> <CRC-16 U16 LE>`.
The ASCII payload has no NUL terminator and CRC covers the payload bytes.
This route does **not** use the printed `01/06` eight-byte payload.

| OBC console command | `02/0C` TC payload (ASCII) | Meaning |
| --- | --- | --- |
| `sen_imu_cfg EXP get` | `lsm6d_conf_get` | Read cached desired/effective configuration and status |
| `sen_imu_cfg EXP reset` | `lsm6d_init` | Queue reset to defaults |
| `sen_imu_cfg EXP set accel 208Hz 8g` | `lsm6d_conf_accel 5 3` | Queue accelerometer rate and scale |
| `sen_imu_cfg EXP set gyro 104Hz 2000dps` | `lsm6d_conf_gyro 4 4` | Queue gyroscope rate and scale |
| `sen_imu_cfg EXP set freefall 3 20` | `lsm6d_conf_ff 3 20` | Queue free-fall threshold and duration register IDs |
| `sen_imu_cfg EXP set wakeup 20 2` | `lsm6d_conf_wu 20 2` | Queue wake-up threshold and duration register IDs |
| `sen_imu_cfg EXP result 18` | `lsm6d_result 18` | Read request completion |

The success TM payload contains `status U8` at offset 0 (zero for a
successful UART command), `text_length U16 LE` at offsets 1–2, and that
many UTF-8 EXP CLI bytes starting at offset 3. Set/reset initially returns
`LSM6DSOX QUEUED id=N`; poll `result` until `DONE` or a terminal error.
A nonzero gateway status indicates a UART/gateway error. EXP command
refusals appear in the returned CLI text; check the queued request result
for final success. Malformed TC receives a generic RS-422 error frame.

| ODR text | Code | Rate |
| --- | ---: | --- |
| `off` | 0 | Power down |
| `12.5Hz`, `26Hz`, `52Hz`, `104Hz`, `208Hz`, `416Hz`, `833Hz` | 1–7 | Corresponding output rate |

| Accelerometer scale text | Code | Gyroscope scale text | Code |
| --- | ---: | --- | ---: |
| `2g` | 0 | `125dps` | 0 |
| `16g` | 1 | `250dps` | 1 |
| `4g` | 2 | `500dps` | 2 |
| `8g` | 3 | `1000dps` | 3 |
| — | — | `2000dps` | 4 |

Free-fall threshold is 0–7 and duration is 0–63 accelerometer ODR periods.
Wake-up threshold is 0–63 and duration is 0–3 register units. These
settings require a nonzero accelerometer ODR. The EXP firmware is polling
only: interrupt routing is disabled, so configuring these thresholds does
not enable hardware event capture. Default accel/gyro settings are both
ODR code 5 (208 Hz), scale code 3 (8 g / 1000 dps). Configuration is
volatile. The PDF defines the native `01/06` configuration as opaque
U8[8]; it does not specify field meanings for those eight bytes.

#### sen_stat (0x07)

Get sensor status. The printed heading says “Get system power status”; its
selectors describe sensors. The printed TC/TM tables below are retained as
revision 1.0 source data. See the [implemented payload](#current-sen_stat-implementation-type-0x01-addr-0x07).


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x07 |
| LENGTH | U16 | 2 | 3 | 2 |
| Sensor report type (footnote 1) | U8 | 1 | 5 | - |
| CRC-16 | U16 | 2 | 6 | - |

**Footnote 1**
Sensor report Type:

- 0: All sensor data
- 1: Temperature sensor data
- 2: BME sensor data
- 3: IMU sensor data
- 4: Flow sensor data

### Printed page 17 (PDF page 13)

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x07 |
| LENGTH | U16 | 2 | 3 | N |
| TM Payload | I16 | N | 5 | - |
| CRC-16 | U16 | 2 | 5+N | - |

### Current sen_stat implementation (TYPE `0x01`, ADDR `0x07`)

`sen_stat` is now implemented in OBC flight software. The console form is
`sen_stat <OBC|EXP|PDU> get <group> [source] [cursor n]`. Groups are `all`,
`temperature`, `bme`, `imu`, `flow`, `power`, `camera`, and `limits`.
`source` defaults to `all`; `cursor` defaults to zero. For example:

```sh
dal_flight_console sen_stat OBC get bme
dal_flight_console sen_stat EXP get flow flow2
dal_flight_console sen_stat EXP get all cursor 8
dal_flight_console sen_stat PDU get temperature
```

The current **TC payload is four bytes**, so its CRC starts at frame byte 9.
This supersedes the printed one-byte selector with the inconsistent printed
LENGTH value 2. Multibyte fields and CRC remain little endian; CRC covers
payload bytes only in current flight software.

| Payload offset | Field | Type | Values |
| ---: | --- | --- | --- |
| 0 | Board | U8 | `0x03` OBC, `0x04` EXP, `0x05` PDU |
| 1 | Group | U8 | 0 all, 1 temperature, 2 BME, 3 IMU, 4 flow, 5 power monitor, 6 camera, 7 limits |
| 2 | Source | U8 | 0 all in group; board-local source ID below |
| 3 | Cursor | U8 | 0 first page; otherwise the previous TM `next_cursor` |

The **TM payload** has a 10-byte header and zero to eight 12-byte records
(`LENGTH = 10 + 12 × record_count`, maximum 106 bytes). Thus the largest
complete RS-422 frame is 113 bytes, including its seven frame-overhead bytes.

| Payload offset | Field | Type | Meaning |
| ---: | --- | --- | --- |
| 0 | Board | U8 | Echo of request |
| 1 | Group | U8 | Echo of request |
| 2 | Source | U8 | Echo of request |
| 3 | Board status | U8 | 0 online, 1 offline |
| 4 | Record count | U8 | 0–8; offline board returns zero |
| 5 | Next cursor | U8 | `0xFF` complete; otherwise request again using this value |
| 6–9 | Snapshot epoch | U32 LE | OBC Unix seconds when the source was read |
| 10 onward | Records | 12 bytes each | Sorted by source ID and channel ID |

Each record is independently typed and has its own validity and age:

| Record offset | Field | Type | Meaning |
| ---: | --- | --- | --- |
| 0 | Source ID | U8 | Board-local source below |
| 1 | Channel ID | U8 | Channel within the source |
| 2 | Flags | U8 | bit 0 valid, bit 1 stale, bit 3 disabled, bit 4 error, bit 5 unsupported |
| 3 | Unit | U8 | 0 none, 1 °C×100, 2 Pa, 3 %RH×100, 4 ohm, 5 mg, 6 °/s×1000, 7 µL/min×100, 8 Boolean, 9 count |
| 4–7 | Age | U32 LE | Milliseconds; `0xFFFFFFFF` unknown |
| 8–11 | Value | I32 LE | `INT32_MIN` when invalid; inspect flags before use |

| Board | Source ID and available channels |
| --- | --- |
| OBC | 1 board temperature (0); 2 NTC1–4 (1–4); 3 BME temperature/pressure/humidity/gas (1–4); 4 PAC U6 health (0); 5 PAC U2 health (0); 6–7 MIPI path status (0, currently unsupported) |
| EXP | 1 board temperature (0); 2 NTC1–4 (1–4); 3 BME temperature/pressure/humidity (1–3; gas channel 4 unsupported); 4 IMU temperature (1), acceleration X/Y/Z (2–4), gyro X/Y/Z (5–7); 5–6 flow1/flow2 flow and temperature (1–2); 7 PAC health (0); 8–9 thermal camera status (0, currently unsupported); 10 optical limits (1–4, currently unsupported) |
| PDU | 1 board temperature (0); 2 PAC health (0) |

PAC rail voltage/current remain in [`pwr_stat`](#pwr_stat-current-flight-software-0104).
Camera image pixels and optical-limit readings are not manufactured from power
or device-presence data; their `sen_stat` records carry the unsupported flag
until structured status is available to OBC flight software. Invalid or stale
measurement records remain in the reply with their validity bit clear.
For `all`, OBC takes two pages, EXP four, and PDU one. Pages read current
snapshots independently, so they are not one atomic cross-page capture.

#### uvc_cfg (0x10)

Conﬁgure UVC subsystem

The following table transcribes revision 1.0. Its 32 payload bytes are opaque in the PDF. The [current flight software layout](#current-uvc_cfg-implementation-type-0x01-addr-0x10) below defines those bytes and adds a camera ID to the get request.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x10 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 32: set command with 32B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x10 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

### Current uvc_cfg implementation (TYPE `0x01`, ADDR `0x10`)

The OBC selects USB camera 1–4 through its USB switch, powers it, and uses Linux V4L2 controls. The 32-byte configuration is an OBC profile saved per camera in `/var/lib/dal/uvc/cameraN.bin` and reapplied when that camera is selected for capture. The camera itself is not programmed with a persistent configuration. The selected camera must be connected and support each requested control; otherwise the command returns a data error (`FF/01`). Only MJPEG capture is supported by the current `usb_cam` tool. `uvc_cfg` does not capture an image.

Console syntax: `uvc_cfg <1|2|3|4> get` or `uvc_cfg <1|2|3|4> set <setting> <value> [setting value ...]`. Supported setting names are `format` (`MJPG`), `resolution` (`WIDTHxHEIGHT`), `fps` (positive integer), `exposure` (`auto|manual`), `exposure_time` (U32), `gain` (U16), `autofocus` (`on|off`), `focus` (U16), `white_balance` (`auto|manual`), `white_balance_temp` (U16), `brightness` (I16), and `led_pwm` (0–100 percent). Exposure time uses V4L2's camera-reported unit; the command does not convert it to milliseconds. Examples: `uvc_cfg 2 get`; `uvc_cfg 2 set format MJPG resolution 1920x1080 fps 30`; `uvc_cfg 2 set exposure manual exposure_time 200 gain 40`.

Get TC: standard frame with LENGTH `1`, payload byte 0 = camera ID 1–4, CRC at bytes 6–7. Set TC: LENGTH `32`, the payload below at byte 5, CRC at bytes 37–38. Success TM: LENGTH `32`, payload below at byte 5, CRC at bytes 37–38. All multibyte values are little-endian. The reply reports the saved profile with requested controls read back from V4L2. A camera with no saved profile returns a zero field mask. A get selects and powers the camera, then reapplies its saved profile before reading it back.

| Payload offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | Camera ID | 1–4 |
| 1 | 1 | Schema | `1` |
| 2 | 2 | Field mask | Bits 0 format, 1 width, 2 height, 3 frame rate, 4 exposure mode, 5 exposure time, 6 gain, 7 autofocus, 8 focus position, 9 auto white balance, 10 white-balance temperature, 11 brightness, 12 CAMERA_LED PWM; bits 13–15 zero. Only masked fields are applied. |
| 4 | 4 | Pixel format | FourCC ASCII `MJPG` |
| 8 | 2 | Width | Pixels, U16 |
| 10 | 2 | Height | Pixels, U16 |
| 12 | 2 | Frame-rate numerator | U16, positive |
| 14 | 2 | Frame-rate denominator | U16, positive; console `fps N` sends N/1 |
| 16 | 1 | Exposure mode | 0 auto; 1 manual |
| 17 | 4 | Exposure time | U32, V4L2 `exposure_absolute` units |
| 21 | 2 | Gain | U16, camera control units |
| 23 | 1 | Autofocus | 0 off; 1 on |
| 24 | 2 | Focus position | U16, camera control units |
| 26 | 1 | Auto white balance | 0 off; 1 on |
| 27 | 2 | White-balance temperature | U16, camera control units |
| 29 | 2 | Brightness | I16, camera control units |
| 31 | 1 | CAMERA_LED duty | 0–100 percent when mask bit 12 is set; zero otherwise |

The field mask makes a set request a partial update. For example, `uvc_cfg 2 set resolution 1920x1080 fps 30` selects bits 1, 2, and 3 (`0x000E`) and leaves the other saved controls as they were. The reply mask identifies all fields in the resulting saved profile, not just the fields in this request. `resolution` always sets width and height together; `fps 30` stores numerator `30` and denominator `1`. Set payload bytes not selected by the mask are ignored. The OBC validates the camera ID, schema, mask bits, and PWM duty before invoking the USB camera tool. The tool rejects invalid values or unsupported camera controls; the OBC returns a standard data error (`FF/01`) instead of a 32-byte success payload. A separate `uvc_ctl` command performs image capture. `uvc_cfg 2 set led_pwm 40` updates camera 2’s saved LED duty; it does not light the LED immediately.

| Operation | TC payload | TM success payload | Full frame length |
| --- | --- | --- | --- |
| `uvc_cfg 2 get` | `02` | 32-byte camera 2 profile | TC 8 bytes; TM 39 bytes |
| `uvc_cfg 2 set resolution 1920x1080 fps 30` | 32 bytes: ID `02`, schema `01`, mask `0E 00`, width `80 07`, height `38 04`, frame rate `1E 00 01 00` at their table offsets | 32-byte resulting camera 2 profile | TC 39 bytes; TM 39 bytes |

The frame starts with `A5 01 10 LENGTH_LE16` for TC and `A5 01 10 LENGTH_LE16` for a successful TM. CRC-16 is little endian and covers the payload bytes only, as specified in [CRC parameters](#crc-parameters).

Setting an automatic mode clears the corresponding manual value from the saved profile. The helper reads back format and requested V4L2 controls, but frame-rate numerator and denominator currently reflect the requested profile rather than a verified `--get-parm` value. Camera support and ranges vary; `v4l2-ctl --list-ctrls-menus` on the selected `/dev/video*` device shows its actual controls. The current response has no per-control capability bitmap: a zero mask means no profile has been saved, not that the camera has no controls.

#### uvc_ctl (0x11)

UVC subsystem manual control


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x11 |

### Printed page 18 (PDF page 14)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| LENGTH | U16 | 2 | 3 | 8 |
| TC Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

**TM**

TM ACK package


#### uvc_stat (0x12)

Get UVC subsystem status


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x12 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x12 |
| LENGTH | U16 | 2 | 3 | 6 |
| UVC status | U16 | 6 | 5 | - |
| CRC-16 | U16 | 2 | 11 | - |

### Current uvc_ctl and uvc_stat implementation

**Flight console format:** `dal_flight_console uvc_ctl <camera:1–4> capture <request_id:1–4294967295>`. For example, `dal_flight_console uvc_ctl 2 capture 1042`. The camera number selects `/dev/video*` through the saved camera mapping; the request ID names the output JPEG and is reused when polling `uvc_stat`.

`uvc_ctl <1|2|3|4> capture <request_id>` queues one JPEG capture. The OBC selects the camera, reapplies its saved [`uvc_cfg`](#current-uvc_cfg-implementation-type-0x01-addr-0x10) profile, and saves to `/home/bee/data/images/usb/USB{camera-1}_{request_id:08}.jpg` (for example, `/home/bee/data/images/usb/USB1_00001042.jpg`). Before invoking the camera tool, the OBC applies that camera’s saved `led_pwm` duty to CAMERA_LED; after capture finishes or fails it turns the LED off. A missing profile or duty 0 does not enable the LED. `request_id` is a nonzero U32, unique per camera; an existing image or active/recent duplicate request is rejected rather than overwritten. Only one capture can be active at a time. The immediate reply means queued, **not saved**. Use `uvc_stat <1|2|3|4> get <request_id>` to poll completion. The OBC maintains eight recent job results per camera in memory; completed images remain discoverable after restart by checking the file. Older failed jobs become unknown after eviction or restart. The JPEG files are not currently indexed by the `dat_*` CSV transfer commands.

`uvc_ctl` TC has TYPE `0x01`, ADDR `0x11`, LENGTH `8`, payload at frame byte 5, and CRC at byte 13:

| Payload offset | Size | Field | Value |
| ---: | ---: | --- | --- |
| 0 | 1 | Camera ID | 1–4 |
| 1 | 1 | Operation | 1 = capture one JPEG |
| 2 | 2 | Reserved | Zero |
| 4 | 4 | Request ID | U32 little-endian, nonzero |

`uvc_stat` TC has TYPE `0x01`, ADDR `0x12`, LENGTH `5`, camera ID U8 at payload byte 0, request ID U32 LE at payload bytes 1–4, and CRC at frame byte 10. Both commands return the same eight-byte TM payload (CRC at frame byte 13):

| Payload offset | Size | Field | Value |
| ---: | ---: | --- | --- |
| 0 | 1 | Camera ID | Echoed request |
| 1 | 1 | State | 0 unknown; 1 queued; 2 capturing; 3 saved; 4 failed |
| 2 | 1 | Error | 0 none; 1 camera tool/capture failed; 2 tool succeeded but output file missing or empty |
| 3 | 1 | Reserved | Zero |
| 4 | 4 | Request ID | Echoed U32 LE |

#### uvc_stat response details

Call `uvc_stat <camera:1–4> get <request_id>` with the same nonzero request ID used for `uvc_ctl`. This reads the OBC capture job state; it does not probe camera health or start another capture. An invalid camera ID or zero request ID returns the standard data-error TM (`ACK/ERR=0xFF`, error code `0x01`). A valid request always returns the eight-byte success payload above, even when the job is unknown or failed. The successful TM frame is `A5 01 12 08 00` + eight payload bytes + CRC-16 LE, 15 bytes total. CRC-16 covers the payload only.

| State byte | Console text | Meaning | Next action |
| ---: | --- | --- | --- |
| `0` | `unknown` | No retained result for this camera/ID and no nonempty JPEG at its expected path. This can mean the ID was never queued, an old result was evicted, or a failed result was lost on reboot. | Check the ID and camera; retry capture with a new ID if needed. |
| `1` | `queued` | `uvc_ctl` accepted the request and the worker has not yet started capture. Its immediate reply is this state. | Poll again. |
| `2` | `capturing` | The worker has started and is launching or waiting for the USB camera tool. | Poll again. |
| `3` | `saved` | The worker verified a nonempty JPEG at `/home/bee/data/images/usb/USB{camera-1}_{request_id:08}.jpg`; the console appends that path. A retained result is not rechecked on every poll, so a file deleted afterward may still show `saved`. After reboot or history eviction, the OBC recovers `saved` only when the file still exists and is nonempty. | Read the file from that path. |
| `4` | `failed` | The worker completed without a verified nonempty output file. The error byte gives the reason. | Inspect camera/tool logs and retry with a new ID. |

| Error byte | Meaning |
| ---: | --- |
| `0` | No recorded error. `unknown` also uses zero because no failure record exists. |
| `1` | The USB camera tool could not launch, returned a nonzero exit status, or capture failed. |
| `2` | The tool returned success, but the expected file was absent, empty, or not a regular file. |

Example for camera 2 and request ID 1042 (`0x00000412`): `02 03 00 00 12 04 00 00` means saved; `02 04 01 00 12 04 00 00` means capture failed. Bytes 4–7 echo the ID and byte 3 is reserved zero. The payload does not contain the image or its path. The OBC holds eight recent results per camera in RAM; once a failed result is evicted or the OBC restarts, its detailed error is no longer available. The image root can be overridden with `DAL_UVC_DATA_DIR`; the deployed default is `/home/bee/data/images/usb`. The profile directory can be overridden with `DAL_UVC_PROFILE_DIR` for testing.

This extends the printed revision 1.0 tables: the PDF gives `uvc_ctl` an opaque eight-byte TC and generic ACK, and `uvc_stat` a zero-byte TC and opaque six-byte TM. The current format adds a request selector to status and an explicit job-state reply. The flight software can override the image root with `DAL_UVC_DATA_DIR` for testing; the deployed default is `/home/bee/data/images/usb`. The profile directory can be overridden with `DAL_UVC_PROFILE_DIR` for testing.

#### thm_cfg (0x13)

Conﬁgure thermal subsystem

`thm_cfg` configures manual limits for four EXP heaters and two EXP IR LEDs. Camera and NTC associations, PID gains, and feedback thresholds belong to thermal profiles. The EXP settings are held in RAM; reboot restores defaults. The heater manual duty ceiling is separate from the existing persisted PID/heater BSP ceiling. A zero slew value means unrestricted changes; an off command cuts output immediately. The maximum heater on time limits each manual request; PID runtime is governed by the profile. IR duty is bounded by the EXP hard limit of 25%; the IR max-on field (up to 100 ms) limits continuous operation at configured maximum duty. Below maximum duty, manual IR remains on until changed or stopped. Camera feedback pulses remain bounded by their profile.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x13 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command

### Printed page 19 (PDF page 15)

- 32: set command with 32B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x13 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

##### thm_cfg payload layout

TC `get` has no payload. TC `set` contains 32 bytes. Byte 1 selects records to change; unselected bytes are ignored. The TM response to either operation contains the complete 32-byte configuration and has byte 1 set to `0x3F`. All multibyte integers are little endian. TC framing is `A5 01 13 LENGTH_LE16 PAYLOAD CRC16_LE`; TM framing is `A5 01 13 20 00 PAYLOAD CRC16_LE`. Errors use the standard error TM frame.

| Payload offset | Bytes | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | Schema | `1` |
| 1 | 1 | Update mask / reply mask | Bits 0–3 select heaters 0–3; bits 4–5 select IR LEDs 0–1; bits 6–7 zero. Reply is `0x3F`. |
| 2–17 | 4 × 4 | Heater records | Per heater: max manual duty U8 percent (0–100), slew U8 percentage points/s (0–100; 0 disables ramp), max manual on time U16 in 10 ms units (1–60000). |
| 18–31 | 2 × 7 | IR records | Per IR LED: max duty U8 percent (0–25), maximum time at maximum duty U8 ms (1–100), PWM frequency U32 Hz (100–100000), min off time U8 in 100 ms units (0–255). |

Console commands:

```text
thm_cfg get
thm_cfg heater 0 set max_duty 60 slew 5 max_on_ms 30000
thm_cfg ir 0 set max_duty 10 max_on_ms 20 pwm_hz 100000 min_off_ms 500
```

Heater `max_on_ms` must be a multiple of 10; IR `min_off_ms` must be a multiple of 100. EXP also exposes `thm_cfg` (read) and `thm_cfg <64 hex digits>` (set) over its UART CLI, which OBC uses internally. A set changes selected records and returns all records. A failed EXP UART transaction or refused setting returns an RS422 error. IR fields are applied through the IR owner queue; a service failure can leave earlier fields applied, so read back with `thm_cfg get` before retrying.

#### thm_ctl (0x14)

Thermal subsystem manual control


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x14 |
| LENGTH | U16 | 2 | 3 | 8 |
| TC Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

**TM**

TM ACK package


##### Current thm_ctl implementation

The printed revision 1.0 table fixes an eight-byte TC and generic ACK but leaves the payload opaque. The flight software uses the layout below without changing frame lengths. Console commands:

```text
thm_ctl heater <0–3> set <duty_pct> <duration_ms>
thm_ctl heater <0–3> off
thm_ctl ir <0–1> set <duty_pct>
thm_ctl ir <0–1> off
thm_ctl ir <0–1> feedback_start
```

| TC payload offset | Bytes | Field | Values |
| ---: | ---: | --- | --- |
| 0 | 1 | Output type | `0` heater, `1` IR LED |
| 1 | 1 | Channel | Heater `0–3`, IR LED `0–1` |
| 2 | 1 | Action | `0` off, `1` set manual duty, `2` start camera feedback (IR only) |
| 3 | 1 | Duty | `0` for off/feedback start; nonzero percent for set; IR hard maximum 25% |
| 4–7 | 4 | Duration | U32 LE milliseconds for heater set (1–600000); zero for IR and off |

TC framing: `A5 01 14 08 00 [8-byte payload] [CRC16 LE]`. For example, `thm_ctl ir 0 feedback_start` sends `01 00 02 00 00 00 00 00`. The successful TM is the **printed generic ACK**: `A5 01 14 04 00 [uptime U32 LE] [CRC16 LE]`. EXP refusal or UART failure produces a standard error TM. Heater set/off is processed synchronously by the EXP temperature service; the MCU refuses both actions while the corresponding profile owns the heater (including profile error state). Stop the profile before manual control; a refusal returns error code `0x01`. IR set/feedback start is queued by the IR owner. An ACK for a queued IR action confirms acceptance, not that PWM is already on. Use `thm_stat` for applied PWM and `02/0C exp_run ir_result <id>` or `ir_feedback_status <channel>` for detailed completion/feedback diagnostics. The generic ACK does not carry the EXP request ID; use the EXP gateway for ticket-aware operation. Manual IR can run below configured maximum duty until changed or stopped. At maximum duty, EXP applies the configured max-on timer and saturates off; camera feedback and manual control are mutually exclusive.

#### thm_stat (0x15)

Get thermal subsystem status


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x15 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | - |

**TM**

### Printed page 20 (PDF page 16)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x15 |
| LENGTH | U16 | 2 | 3 | 6 |
| THM status | U16 | 6 | 5 | - |
| CRC-16 | U16 | 2 | 11 | - |

##### Current thm_stat implementation

The printed revision 1.0 TC and TM lengths are retained. `thm_stat` or `thm_stat get` sends a zero-byte TC: `A5 01 15 00 00 [CRC16 LE]`. The successful TM is `A5 01 15 06 00 [six-byte payload] [CRC16 LE]`. There is one byte per physical PWM output:

| TM payload byte | Output | Bit 7 | Bits 0–6 |
| ---: | --- | --- | --- |
| 0 | Heater 0 | `1` PWM running; `0` stopped | Applied duty 0–100% |
| 1 | Heater 1 | Same | Same |
| 2 | Heater 2 | Same | Same |
| 3 | Heater 3 | Same | Same |
| 4 | IR LED 0 | Same | Same |
| 5 | IR LED 1 | Same | Same |

The running bit reflects physical PWM activity: a camera feedback sequence capturing a frame has IR duty 0 and running bit 0 until its LED pulse starts. A stopped output is `0x00`; an output running at 40% is `0xA8` (`0x80 | 40`). This reply intentionally contains no owner, request ID, saturation reason, temperature, or camera feedback phase. EXP CLI `ir_result`, `ir_feedback_status`, `heater_result`, and `temp_status` provide those diagnostics through `02/0C exp_run`. OBC returns the latest background `thm_wire_stat all` sample; it returns a standard RS422 error if no sample is available or the sample is older than 15 seconds.

#### ﬂd_cfg (0x16)

Conﬁgure ﬂuidic subsystem


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x16 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 32: set command with 32B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x16 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

##### fld_cfg current payload layout

`fld_cfg` sends configuration to the EXP MCU through its existing `pc_*`, `pump_type_set`, `pump_detect`, `pump4_detect`, and `pump_result` CLI commands. It configures the four logical valves on populated U35/J7, flow input, safe and operation routes, and the active protein crystallization profile. It also exposes U11 pump type selection and the MCU pump-driver detection checks. TYPE `0x01`, ADDR `0x16`; all multi-byte integers are little endian. Each ordinary set changes one item. A failed UART transaction produces a standard error TM. For ordinary settings, an MCU refusal also produces a standard error TM; pump detection reports its MCU outcome in the 32-byte TM result field described below. The fixed 32-byte TM is an echo of an accepted ordinary set, a page of `cfg_json_pc` data on a get, or a pump-detection result.

| TC offset | Bytes | Get meaning | Set meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | `0` | `1` |
| 1 | 1 | Page index, zero based | Group: `1` valve map; `2` valve pulse; `3` flow channel; `4` flow log interval; `5` route; `6` profile field; `7` boot resume; `8` U11 pump driver type; [`9` pump detection](#fld_cfg-pump-detection) |
| 2 | 1 | Zero | Route kind: `0` standby, `1` test, `2` replace, `3` circulation, `4` ground; or profile target: `0` global, `1` test, `2` replace, `3` circulation |
| 3 | 1 | Zero | Route case `0` A / `1` B, or ground preset `0` protein_a, `1` protein_b, `2` precip_respol, `3` precip_rsvb; for profile, field ID below |
| 4–7 | 4 | Zero | Group value U32: pulse/interval in ms, channel `0–1`, resume `0–1`, pump type `0` lowdriver/`1` highdriver, or profile value. For route use byte 4 as V1–V4 open mask. For valve map use bytes 2–5 as four distinct U35 pair indices `0–3`. |
| 8–31 | 24 | Zero | Zero; reserved |

For a get, zero-length TC requests page 0; a 32-byte TC with byte 0 = `0` and byte 1 = page requests another page. The 32-byte TM has total JSON length U16 at offsets 0–1, byte offset U16 at 2–3, and up to 28 bytes of UTF-8 `cfg_json_pc` JSON at 4–31. Unused trailing bytes are zero. Reassemble pages in byte-offset order; request pages `0` through `ceil(total/28)-1`. Each page is sampled anew, so configuration changes during paging can change the JSON; repeat the read if consistency matters. Set TM echoes the accepted 32-byte TC except for pump detection, whose 32-byte result is described below. The console displays each page's offset, total, and text.

| Profile field ID | Name | Targets | Wire value |
| ---: | --- | --- | --- |
| 1 | `duration` | test, replace | U32 ms |
| 2 | `minflow` | test, replace, circulation | U32 milli-µL/min |
| 3 | `flow_action` | test, replace, circulation | `0` none, `1` warn, `2` stop |
| 4 | `pump_freq` | test, replace, circulation | U32 Hz, constrained by selected U11 driver |
| 5 | `pump_volt` | test, replace, circulation | U32 Vpp, constrained by selected U11 driver |
| 6 | `volume` | test, replace, circulation | U32 milli-µL |
| 7–9 | `pump_on`, `pump_off`, `cycles` | circulation | U32 ms, ms, count; zero cycles means continuous |
| 10 | `pump_off_settle` | global | U32 ms |
| 11 | `run_mode` | global | `0` time, `1` volume |
| 12 | `max_pump_on` | global | U32 ms |

Route mask bit 0 is V1, through bit 3 for V4; `1` means open. The flow channel selects sensor 0 or 1. The MCU profile also stores flow-check enable/delay, failure sample count, pump-enable flags and a profile name. These are exposed by [`pc_cfg`](#pc_cfg-current-payload-layout), while the older `fld_cfg` field range remains 1–12. Operation and driver settings are validated by the MCU; U11 is the pump used by the current crystallization profiles.

Console examples:

```text
fld_cfg get
fld_cfg get 1
fld_cfg valve map 0 1 2 3
fld_cfg valve pulse 30
fld_cfg flow channel 1
fld_cfg flow interval 1000
fld_cfg route standby 0 0 0 0
fld_cfg route test a 1 0 0 0
fld_cfg route ground protein_a 1 0 0 0
fld_cfg profile test duration 15000
fld_cfg profile test minflow 0.5
fld_cfg profile circulation cycles 3
fld_cfg profile global run_mode volume
fld_cfg resume 1
fld_cfg pump_type highdriver
fld_cfg pump detect u11
fld_cfg pump result 42
fld_cfg pump detect u15
```

The console maps these to `pc_valve_conf_channel`, `pc_valve_conf_pulse`, `pc_flow_conf_channel`, `pc_flow_conf_interval`, `pc_standby_conf_dir`, `pc_{test,replace,circulation}_conf_dir`, `pc_onground_conf`, `pc_profile_set`, `pc_resume_cfg`, `pump_type_set`, `pump_detect`, `pump4_detect`, and `pump_result` on EXP. `cfg_json_pc` supplies get data. Operation commands `pc_activate`, `pc_test`, `pc_replace`, `pc_circulation`, `pc_standby`, `pc_stop`, `pc_status`, and manual valve commands remain separate from `fld_cfg`.


##### fld_ctl manual valve and pump control

`fld_ctl` uses TYPE `0x01`, ADDR `0x17`. The TC payload is exactly 8 bytes; multi-byte values are unsigned little endian. It controls manual DAL V1–V4 directions and the U11/U15 pump drivers. Profile execution is outside this command.

| Byte | Field | Values |
|---:|---|---|
| 0 | Device | `0` valves; `1` U11; `2` U15 |
| 1 | Action | Valves: `0` set all; pumps: `0` off, `1` on, `2` frequency, `3` voltage |
| 2 | Channel | U15 voltage output `0–3`; zero otherwise |
| 3 | Valve mask | For valves, bits 0–3 are V1–V4, `1` = open/position B, `0` = closed/position A; zero for pumps |
| 4–7 | Value | Frequency in Hz or voltage in Vpp; zero for other actions |

Console forms are `fld_ctl valve mask <0–15>`, `fld_ctl pump u11 on|off|frequency <Hz>|voltage <Vpp>`, and `fld_ctl pump u15 on|off|frequency <Hz>|voltage <channel 0–3> <Vpp>`. U11 frequency is 8–1992 Hz and voltage 0–150 Vpp; U15 shared frequency is 50–800 Hz and per-channel voltage 0–250 Vpp. The console currently exposes the full valve mask only; a one-valve command would need a reliable read of the other three live directions.

| Flight console command | TC device/action | EXP MCU command |
|---|---|---|
| `fld_ctl valve mask <0–15>` | `0/0` | `pc_onground_control <v1> <v2> <v3> <v4>` |
| `fld_ctl pump u11 off\|on` | `1/0\|1` | `pump_disable` / `pump_enable` |
| `fld_ctl pump u11 frequency <Hz>` | `1/2` | `pump_set_freq <Hz>` |
| `fld_ctl pump u11 voltage <Vpp>` | `1/3` | `pump_set_volt <Vpp>` |
| `fld_ctl pump u15 off\|on` | `2/0\|1` | `pump4_disable` / `pump4_enable` |
| `fld_ctl pump u15 frequency <Hz>` | `2/2` | `pump4_set_freq <Hz>` |
| `fld_ctl pump u15 voltage <channel> <Vpp>` | `2/3` | `pump4_set_volt <channel> <Vpp>` |


The OBC sends `pc_onground_control <v1> <v2> <v3> <v4>`, `pump_enable/disable/set_freq/set_volt`, or `pump4_enable/disable/set_freq/set_volt` to EXP. The MCU requires the PC system activated and in Standby or ONGROUND for manual valve control. A valve command returns a zero-payload ACK after the MCU accepts it. A pump command returns a four-byte U32 little-endian MCU request ID in the ACK payload. Pump ACK means queued, not completed: poll `fld_cfg pump result <request_id>` until `done` or a failure state, then read `fld_stat` for the applied actuator settings. Malformed input, MCU refusal, and UART failure return a standard error TM. The valve command still does not provide a completion ticket; check its commanded and valid masks in `fld_stat`.


##### fld_stat actuator status

`dal_flight_console fld_stat` (or `fld_stat get`) requests TYPE `0x01`, ADDR `0x18` with a zero-byte TC payload. The 16-byte TM reports the current EXP actuator state sampled by one MCU `fld_wire_stat` command from the valve and pump service caches. It does not report the crystallization experiment state, profile, flow, or experiment errors. Multi-byte integers are unsigned little endian.

| Byte | Field | Meaning |
|---:|---|---|
| 0 | Valve commanded mask | Bits 0–3: V1–V4 commanded open/position B; zero means closed/position A |
| 1 | Valve valid mask | Bits 0–3: commanded position is known for V1–V4 |
| 2 | Actuator flags | Bit 0 rail on; 1 U11 initialized; 2 U11 enabled; 3 U15 initialized; 4 U15 enabled; 5 valve busy; 6 valve driver ready; 7 pump busy |
| 3 | U11 driver type | `0` unconfigured, `1` Lowdriver, `2` Highdriver |
| 4–5 | U11 frequency | Hz, U16 LE |
| 6 | U11 voltage | Vpp |
| 7–8 | U15 shared frequency | Hz, U16 LE |
| 9–12 | U15 channel voltages | Channels 0–3, one Vpp byte each |
| 13–15 | Reserved | Zero |

Valve position feedback is not fitted: mask bytes 0–1 represent the commanded state and its validity, not verified physical position. The MCU returns one compact cached snapshot without I²C reads. Its valve and pump service copies are read sequentially, so it is not a simultaneous hardware measurement. A pump request can still be queued when this status is sampled; use the request ID from `fld_ctl` and poll `fld_cfg pump result` to confirm completion. A missing or malformed EXP status response produces a standard error TM rather than a partial 16-byte status.

##### fld_cfg pump detection

`fld_cfg pump detect u11` calls EXP `pump_detect`; `fld_cfg pump detect u15` calls EXP `pump4_detect`. Detection is a driver initialization and communication check, not proof that a physical piezo pump is connected or moving fluid. U11 uses the previously configured `pump_type` (`lowdriver` or `highdriver`); an unconfigured type returns `not_ready`. U11 Highdriver and U15 Highdriver4 check device ID `0xB2` at their selected I²C addresses. U11 Lowdriver checks initialization register readback; it does not independently identify the fitted chip. A wrong U11 type may fail but is not conclusively distinguished from an I²C failure. Detection is refused while the selected driver is enabled and can also be refused by the power or experiment ownership interlocks.

The OBC-to-EXP UART command normally queues the check. The first RS-422 reply returns a MCU request ID; use `fld_cfg pump result <request_id>` to poll `pump_result <request_id>` until `done` or a failure state appears. Do not treat `queued` or `accepted` as detection success; `done` means the driver check completed successfully for a detect request. The MCU retains pump results for approximately 60 seconds. This two-step exchange avoids waiting for initialization within one RS-422 transaction.

Both operations use the existing TYPE `0x01`, ADDR `0x16`, 32-byte TC and 32-byte success TM. The TC and TM payloads are:

| Payload offset | Size | Detect TC | Result TC | TM response |
| ---: | ---: | --- | --- | --- |
| 0 | 1 | `1` | `1` | `1` schema |
| 1 | 1 | `9` | `9` | `9` pump detection group |
| 2 | 1 | `0` | `1` | Echoed operation |
| 3 | 1 | Target: `0` U11, `1` U15 | `0xFF` (ticket is not target-specific) | Echoed target selector |
| 4–7 | 4 | Zero | Nonzero MCU request ID U32 LE | MCU request ID U32 LE; zero for immediate completion or a rejected submission |
| 8 | 1 | Zero | Zero | Result code below |
| 9–12 | 4 | Zero | Zero | MCU driver error U32 LE if supplied by `pump_result` |
| 13–31 | 19 | Zero | Zero | Zero, reserved |

| Result code | Console state | Meaning |
| ---: | --- | --- |
| `0` | `queued` | MCU accepted a ticket; poll `fld_cfg pump result <id>`. |
| `1` | `accepted` | MCU started processing; poll again. |
| `2` | `done` | Driver initialization and type-specific readback completed. |
| `3` | `refused` | MCU rejected the operation or an interlock prevented it. |
| `4` | `io_error` | I²C or driver communication/check failed; inspect `driver_error`. |
| `5` | `power_lost` | Required SLN/HD rail or ownership was lost. |
| `6` | `cancelled` | MCU cancelled the request. |
| `7` | `busy` | Pump service could not accept the request now. |
| `8` | `interlocked` | Pump service ownership interlock blocked the request. |
| `9` | `not_ready` | Pump service or U11 pump type is not configured. |
| `10` | `invalid` | MCU rejected the request parameters. |
| `11` | `unknown` | Result ID is expired or unavailable. |

The TM remains a normal `01/16` ACK frame even when its result code reports a failed detection; callers must inspect byte 8. The local console reports failed states as errors. An EXP UART transport failure produces a standard RS-422 error frame. The result query uses `0xFF` in target byte 3 because the MCU `pump_result` response does not identify which driver created the ticket; retain the target from the detection request in ground software.

#### ﬂd_ctl (0x17)

Fluidic subsystem manual control


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x17 |
| LENGTH | U16 | 2 | 3 | 8 |
| TC Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

### Printed page 21 (PDF page 17)

**TM**

TM ACK package


#### ﬂd_stat (0x18)

Get fluidic actuator status.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x18 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x18 |
| LENGTH | U16 | 2 | 3 | 16 |
| FLD status | U16 | 16 | 5 | - |
| CRC-16 | U16 | 2 | 21 | - |

#### rba_cfg (0x19)

Configure the two robot-arm motors and their four optical limit inputs. The EXP firmware always drives both motors with its built-in 20-phase half-step table. There is no phase-table or profile selector in `rba_cfg`. The OBC `01/19` handler and binary payload fields remain **reserved**; the formats below are the printed ICD envelope, not an implemented console-to-RS-422 mapping.

Current EXP CLI commands available for a future OBC handler are `motor5_cfg_get/set`, `motor mode`, `motor limits get/set`, `limit_cfg_get/set`, and `motor config save/restore/defaults`. Motor parameters are `max_speed_sps` (1–200), `accel_sps2` (1–1000), `drive_pct` (1–80), `hold_pct` (0–80), `hold_timeout_ms` (1–10000), `home_speed_sps` (1–maximum speed), and `reverse` (0/1). The limit inputs are optical 0–3; each motor has distinct min and max inputs, and home aliases one endpoint. Each input has `active_level`, `debounce_ms` (0–100), and `invalid_timeout_ms` (0–100). The motor rail must be powered for motor operations. Custom phase and sequence writes are rejected by the current EXP firmware.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x19 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 32: set command with 32B TC payload


**TM**

### Printed page 22 (PDF page 18)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x19 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

#### rba_ctl (0x1A)

RBA subsystem manual control. See the [implemented console and binary layout](#current-rba_ctl-implementation).


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x1A |
| LENGTH | U16 | 2 | 3 | 8 |
| TC Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

**TM**

TM ACK package

##### Current rba_ctl implementation

**Flight console commands:** `rba_ctl init`; `rba_ctl move <0|1> <signed_steps> [speed_sps]`; `rba_ctl goto <0|1> <signed_position> [speed_sps]`; `rba_ctl jog <0|1> <cw|ccw> [speed_sps]`; `rba_ctl home <0|1>`; `rba_ctl stop <0|1|all>`; `rba_ctl stop_hold <0|1>`; `rba_ctl disable`; `rba_ctl result <request_id>`. Explicit speed is 1–200 steps/s. Omit it to use the configured motor speed. `move` requires nonzero steps other than INT32_MIN. `goto` requires a valid position reference. `jog` continues until stopped. `home` uses the configured home endpoint to determine direction; its optional speed is not exposed by this command. The EXP motor rail and driver must be ready before motion.

FSW translates `init` to EXP `motor5_init` and `disable` to `motor5_disable`. `stop_hold` keeps the current winding phase energized at `hold_pct` (or `drive_pct` if `hold_pct` is zero) until the next move, jog, HOME, ordinary stop, STOP ALL, power loss, or fault. It marks the motor `HOLDING` and invalidates the position reference. A successful motion reply contains a request ID; query `rba_ctl result <request_id>` to learn whether it completed.

The 8-byte TC payload at frame offsets 5–12 is:

| Payload byte | Field | Encoding |
| ---: | --- | --- |
| 0 | Operation | 1 init, 2 move, 3 goto, 4 jog, 5 home, 6 stop, 7 disable, 8 result, 9 stop_hold |
| 1 | Motor | 0 or 1 for motion; 0, 1, or 2 (`all`) for stop; 0 or 1 for stop_hold; 2 for init, disable, result |
| 2 | Speed | U8, 1–200 steps/s for move/goto/jog; 0 uses configured speed; zero for all other operations |
| 3 | Reserved | Zero |
| 4–7 | Argument | LE S32 steps for move, LE S32 target for goto, +1 (`01 00 00 00`) CW or −1 (`FF FF FF FF`) CCW for jog, LE U32 nonzero request ID for result; zero otherwise |

On accepted `init`, `move`, `goto`, `jog`, or `home`, the TM payload is the EXP request ID as **U32 LE** (4 bytes). This acknowledges submission or initial acceptance, not completion. `stop`, `stop_hold`, and `disable` return a zero-byte ACK. `result` returns the following 12-byte TM payload:

| Payload bytes | Field | Encoding |
| ---: | --- | --- |
| 0–3 | Request ID | U32 LE, same ID as requested |
| 4 | Result code | 0 queued, 1 accepted, 2 done, 3 invalid, 4 busy, 5 not ready, 6 refused, 7 not homed, 8 limit reached, 9 fault, 10 power lost, 11 cancelled, 12 storage error, 13 not found, 14 limit invalid, 15 power off, 16 SPI error, 17 wrong device |
| 5–7 | Reserved | Zero |
| 8–11 | Position | S32 LE, EXP-reported motor position in steps |

OBC maps the request to the current EXP `motor`/`motor5_*` CLI. UART timeout, malformed EXP response, and rejected submission return a standard error TM. `result` can report a terminal error in its result code. The printed ICD specifies a generic ACK only; these variable-length reply details describe the current flight software extension.


#### rba_stat (0x1B)

Get robot-arm motor and optical-limit status. See the [current 16-byte payload](#current-rba_stat-implementation).


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x1B |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x1B |
| LENGTH | U16 | 2 | 3 | 16 |

### Printed page 23 (PDF page 19)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| RBA status | U16 | 16 | 5 | - |
| CRC-16 | U16 | 2 | 21 | - |

##### Current rba_stat implementation

**Flight console:** `dal_flight_console rba_stat` or `dal_flight_console rba_stat get`. Both issue a zero-byte TC at `TYPE=0x01`, `ADDR=0x1B`. The 16-byte TM payload is one EXP motor-service snapshot:

| TM payload byte | Field | Encoding |
| ---: | --- | --- |
| 0 | Driver/limit flags | Bit 0 initialized; 1 asleep; 2 faulted; 3 diagnostic mode; 4 burn-wire reservation; 5 optical inputs powered; bits 6–7 zero |
| 1 | Limit active mask | Bits 0–3 correspond to optical inputs 0–3; a bit is set only when that input is valid and active; bits 4–7 zero |
| 2 | Limit valid mask | Bits 0–3 correspond to optical inputs 0–3 and indicate a current, valid sample; bits 4–7 zero |
| 3 | Motor flags | Bit 0 motor 0 position reference valid; 1 motor 1 reference valid; 2 motor 0 direction negative; 3 motor 1 direction negative; bits 4–7 zero |
| 4 | Motor 0 state | 0 stopped, 1 moving, 2 continuous jog, 3 homing, 4 holding, 5 error |
| 5 | Motor 1 state | Same values as motor 0 |
| 6–9 | Motor 0 position | S32 LE, steps; meaningful as an absolute reference only when byte 3 bit 0 is set |
| 10–13 | Motor 1 position | S32 LE, steps; meaningful as an absolute reference only when byte 3 bit 1 is set |
| 14 | Motor 0 applied speed | U8, steps/s, 0–200 |
| 15 | Motor 1 applied speed | U8, steps/s, 0–200 |

If a limit-valid bit is zero, its position is **invalid**, regardless of the active bit. `rba_stat` uses a fresh table 12 sample or requests `rba_wire_stat` once over EXP UART when the cache is unavailable. OBC validates the 16 bytes and returns a standard error TM on UART timeout or malformed EXP status. Use `rba_ctl result <request_id>` for the completion of a particular motion; `rba_stat` reports current actuator state.


#### lev_calib (0x20)

Calib LEV subsystem


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x21 |
| LENGTH | U16 | 2 | 3 | 0/130 (footnote 1) |
| TC Payload | U8 | 0/130 | 5 | - |
| CRC-16 | U16 | 2 | 5/135 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 128: set command with 128B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x21 |
| LENGTH | U16 | 2 | 3 | 130 |
| TM Payload | U8 | 130 | 5 | - |
| CRC-16 | U16 | 2 | 135 | - |

#### lev_cfg (0x21)

Conﬁgure LEV subsystem


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x21 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 32: set command with 32B TC payload

### Printed page 24 (PDF page 20)

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x21 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

#### lev_ctl (0x22)

LEV subsystem manual control


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x22 |
| LENGTH | U16 | 2 | 3 | 8 |
| TC Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

**TM**

TM ACK package


#### lev_stat (0x23)

Get LEV subsystem status


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x01 |
| ADDR | U8 | 1 | 2 | 0x23 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x01 |

### Printed page 25 (PDF page 21)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x23 |
| LENGTH | U16 | 2 | 3 | 16 |
| LEV status | U16 | 16 | 5 | - |
| CRC-16 | U16 | 2 | 21 | - |

Experiment Control (TYPE = 0x02)

#### pc_stat (0x00)

Get protein-crystallization experiment status. The printed ICD called this `exp_stat`; TYPE `0x02`, ADDR `0x00`, zero-byte TC and one-byte TM remain unchanged. The EXP `pc_wire_stat` command and OBC `pc_stat` handler implement the following byte layout.

**Console command:**

```text
dal_flight_console pc_stat
dal_flight_console pc_stat get
```

Both forms request the same status. OBC returns the cached compact status byte from CAN RPARAM table 12 when CSP is enabled; the legacy UART cache is used only with `DAL_EXP_CSP_ENABLE=0`. For detailed live fields, use `dal_flight_console exp_run pc_status`.

| TM status bit | Meaning |
| ---: | --- |
| 0–2 | MCU state enum: 0 Standby, 1 Test, 2 Replace, 3 Circulation, 4 Stopping, 5 Error, 6 ONGROUND; 7 reserved |
| 3 | PC activated |
| 4 | Last error is nonzero; the specific error is not encoded |
| 5 | Active case B (otherwise A; interpret only while a case is active) |
| 6 | Circulation pump-on phase; interpret only in Circulation |
| 7 | Pump-off settle pending (`pending_settle_ms > 0`) |

The `pc_status` CLI currently reports activation, logging, auto-resume, log interval, state, error name, measured flow, settle time, active case and case selection, run mode, volume progress, pump-on time and limit, remaining time, and ONGROUND valve/pump information. A one-byte TM cannot carry these detailed values. The MCU state/error enums are defined in `pc_exp_types.h`; the running flag is derivable from states 1–4. EXP `pc_wire_stat` returns `PC_STAT` followed by two hexadecimal digits from one `pc_exp_get_status()` snapshot. OBC validates the byte and returns a standard error TM for UART timeout, malformed EXP output, invalid state 7, or case/circulation flags outside their applicable states. The detailed text `pc_status` remains available through `exp_run`.



**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x02 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 1 |
| PC status | U8 | 1 | 5 | - |
| CRC-16 | U16 | 2 | 6 | - |

#### pc_cfg (0x01)

Configure protein-crystallization experiments on EXP. The printed ICD called this entry `exp_cfg`; its TYPE `0x02`, ADDR `0x01`, and 0/32-byte TC and 32-byte TM envelopes are unchanged. The current OBC handler is described in the [payload layout](#pc_cfg-current-payload-layout). The older [`fld_cfg`](#fld_cfg-current-payload-layout) at `01/16` remains available for its existing settings.

**Configuration inventory from EXP firmware**

| Group | Parameters to cover | Persistent source and current access |
| --- | --- | --- |
| I/O mapping | Four unique valve channels V1–V4 (0–3), valve pulse width (1–100 ms), flow sensor channel (console 0/1), flow/temperature log interval (>0 ms) | `DOMAIN_PCEXP_IO_CONFIG`; `pc_valve_conf_channel`, `pc_valve_conf_pulse`, `pc_flow_conf_channel`, `pc_flow_conf_interval` |
| Safe and ground routes | Standby V1–V4 direction; four named ground-fill presets (`protein_a`, `protein_b`, `precip_respol`, `precip_rsvb`), each a four-valve route | `DOMAIN_PCEXP_IO_CONFIG`; `pc_standby_conf_dir`, `pc_onground_conf` |
| Global profile | Profile name (24-byte stored field), run mode (`time`/`volume`), pump-off settle time, maximum pump-on time | `DOMAIN_PCEXP_PROFILE`; `pc_profile_set global` now accepts `name` as well as numeric/mode fields; changing another field assigns `PC_CUSTOM` |
| Test and Replace, each | Case A and B valve routes; duration; pump enable; pump frequency and voltage; flow-check enable, minimum flow, grace delay, consecutive low-flow failure samples, low-flow action (`none`/`warn`/`stop`); target volume | `DOMAIN_PCEXP_PROFILE`; route commands and `pc_profile_set` expose these fields |
| Circulation | Case A and B valve routes; pump-on/off time, cycle count (0 = indefinite), pump enable/frequency/voltage; flow-check enable, minimum flow, grace delay and low-flow action; target volume and maximum pump-on time | `DOMAIN_PCEXP_PROFILE`; route commands and `pc_profile_set` expose these fields; maximum pump-on time follows the global limit |
| Restart and logging policy | PC auto-resume on the next boot; PC data logging on the next run | `DOMAIN_SYS_STATE`; `pc_resume_cfg` controls auto-resume and `pc_datalog_enable/disable` controls logging |

`DOMAIN_PCEXP_STATE` stores runtime restart data such as current state, elapsed time, active case and circulation phase. It is not a profile-setting record and should be returned by status or persistence inspection, not edited through `pc_cfg`. Profile magic/version and reserved legacy three-way fields are internal storage metadata. DAL uses only four two-way valves; three-way positions are fixed to zero. Pump driver type and detection belong to the pump interface, currently exposed by `fld_cfg`; `pc_cfg` should refer to the selected pump type when validating profile frequency/voltage. Use `cfg_json_pc` and `persist_dump pcexp` to inspect live and saved settings.

The profile and I/O records are restored from FRAM at boot; missing or invalid records fall back to defaults. Each set request changes one setting; a paged get reads the current `cfg_json_pc` configuration.

##### pc_cfg current payload layout

**Flight console:** `pc_cfg get [page]`; `pc_cfg valve map <v1> <v2> <v3> <v4>`; `pc_cfg valve pulse <ms>`; `pc_cfg flow channel <0|1>`; `pc_cfg flow interval <ms>`; `pc_cfg route <standby|test|replace|circulation|ground> [case|preset] <v1> <v2> <v3> <v4>`; `pc_cfg profile <global|test|replace|circulation> <field> <value>`; `pc_cfg resume <0|1>`; `pc_cfg datalog on|off`; `pc_cfg defaults`. Example: `dal_flight_console pc_cfg profile test flow_check 1`.

TC `TYPE=0x02`, `ADDR=0x01`, length 0 for get page 0, or 32 bytes as follows. All multi-byte numbers are little endian. Set TM echoes the accepted 32-byte TC; get TM is 32 bytes of paged JSON. A rejected EXP command or UART failure gives a standard error TM.

| Payload bytes | Get | Set |
| ---: | --- | --- |
| 0 | `0` | `1` |
| 1 | Page index, zero based | Group: 1 valve map, 2 valve pulse, 3 flow channel, 4 flow log interval, 5 route, 6 profile, 7 boot resume, 10 data logging, 11 load default profile |
| 2 | Zero | Route kind 0 standby/1 test/2 replace/3 circulation/4 ground, or profile target 0 global/1 test/2 replace/3 circulation; zero for other groups |
| 3 | Zero | Route case/preset, or profile field ID below; zero for other groups |
| 4–7 | Zero | U32 value; route mask in byte 4; valve map in bytes 2–5; profile name uses bytes 4–26 and a zero terminator |
| 8–31 | Zero | Zero except the profile name field, which uses bytes 8–26 |

Get TM bytes 0–1 are total JSON length U16, bytes 2–3 are byte offset U16, and bytes 4–31 carry up to 28 JSON bytes. Request pages in order until the total is read. The JSON may change between page requests. Set TM echoes the request. Group 10 value is `0` logging off or `1` on; group 11 has zero value. Groups 8–9 from `fld_cfg` (pump type/detection) are not part of `pc_cfg`.

Profile field IDs 1–12 are the same as [`fld_cfg`](#fld_cfg-current-payload-layout): duration, minflow, flow_action, pump_freq, pump_volt, volume, pump_on, pump_off, cycles, pump_off_settle, run_mode, max_pump_on. Additional IDs are:

| ID | Field | Target | Wire value |
| ---: | --- | --- | --- |
| 13 | `flow_check` | Test, Replace, Circulation | U32, 0 off / 1 on |
| 14 | `flow_delay` | Test, Replace, Circulation | U32 ms |
| 15 | `fail_samples` | Test, Replace | U32, 1–100 consecutive failures |
| 16 | `pump_enable` | Test, Replace, Circulation | U32, 0 off / 1 on |
| 17 | `name` | Global | ASCII 1–23 characters in bytes 4–26, zero terminated; bytes 27–31 zero |

`pc_cfg` uses the existing EXP `pc_*` commands. The EXP `pc_profile_set` CLI now supports IDs 13–17. The MCU marks profile/I/O changes for persistence; data-logging changes commit `DOMAIN_SYS_STATE` immediately, while auto-resume takes effect on the next boot. `defaults` reloads the built-in profile, not the I/O mapping or restart state.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command

### Printed page 26 (PDF page 22)

- 32: set command with 32B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x02 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

#### pc_ctl (0x02)

Control the protein-crystallization experiment. The printed ICD called this `exp_ctl`; TYPE `0x02`, ADDR `0x02`, and its 8-byte TC/generic ACK envelope are retained. The OBC flight console and RS-422 handler implement the operations below using the existing EXP MCU CLI.

| Flight console option | Existing EXP CLI | Meaning and precondition |
| --- | --- | --- |
| `pc_ctl activate` | `pc_activate` | Select pump type first; persist activation, power solenoid rail, reserve pump/valve services, and enter Standby |
| `pc_ctl deactivate` | `pc_deactivate` | Only from Standby; persist deactivation and release pump/solenoid rails |
| `pc_ctl test [a|b|both]` | `pc_test [a|b|both]` | Start configured Test profile; activated PC required |
| `pc_ctl replace [a|b|both]` | `pc_replace [a|b|both]` | Start configured Replace profile; activated PC required |
| `pc_ctl circulation [a|b|both]` | `pc_circulation [a|b|both]` | Start configured Circulation profile; activated PC required |
| `pc_ctl ground <protein_a|protein_b|precip_respol|precip_rsvb>` | `pc_onground <preset>` | Select a configured ground-fill route; activated PC in Standby or ONGROUND required |
| `pc_ctl standby` | `pc_standby` | Request safe Standby from any state; also leaves ONGROUND |
| `pc_ctl stop` | `pc_stop` | Interrupt current operation and return to Standby |
| `pc_ctl reset_error` | `pc_reset_error` | Clear ERROR and return to Standby |

Case `both` is the MCU default. `pc_onground standby` is an alias of `pc_standby`. Configuration (`pc_profile_set`, route settings, logging/resume policy) belongs to [`pc_cfg`](#pc_cfg-0x01); manual V1–V4 valve output belongs to [`fld_ctl`](#fld_ctl-manual-valve-and-pump-control); `pc_status` is a read operation rather than a control option. The MCU also has diagnostic `pc_fake_data`, but its current handler compares operation names incorrectly, so it should not be exposed through `pc_ctl` until fixed.

**RS-422 TC:** TYPE `0x02`, ADDR `0x02`, LENGTH `8`. The eight payload bytes are:

| Payload byte | Field | Values |
| ---: | --- | --- |
| 0 | Operation | `1` activate; `2` deactivate; `3` test; `4` replace; `5` circulation; `6` ground route; `7` standby; `8` stop; `9` reset error |
| 1 | Selection | For Test/Replace/Circulation: `0` both (default), `1` Case A, `2` Case B. For ground: `0` protein_a, `1` protein_b, `2` precip_respol, `3` precip_rsvb. For other operations: `0` |
| 2–7 | Reserved | All zero; OBC rejects nonzero bytes |

**TM:** a successful request returns a zero-byte ACK with ACK/ERR `0x02`, address `0x02`, LENGTH `0`, and CRC-16. Invalid TC values, UART failure, or an explicit EXP error return a standard error TM. `activate`/`deactivate` report the result of the MCU synchronous checks; an already activated/deactivated PC is also acknowledged. Other commands print `sent` after `pc_exp_send_cmd()`, which does not report queue acceptance or state-machine completion. Thus their ACK confirms only the EXP CLI submission. Use `dal_flight_console pc_stat` after the request to confirm the state, or `dal_flight_console exp_run pc_status` for details. `stop`, `standby`, and `reset_error` remain available while PC is deactivated.



**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x02 |
| LENGTH | U16 | 2 | 3 | 8 |
| TC Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

**TM**

TM ACK package


#### thp_cfg (0x03)

Configure or read one heater/NTC PID profile or IR LED/thermal-camera PID profile. The MCU command `thp_wire_cfg` reads the live configuration; heater changes are persisted, while IR profile changes are RAM-only and lost on reboot. Stop a profile before changing it. This command does not start the controller.

**Console commands**

```text
dal_flight_console thp_cfg heater get <profile:0-3>
dal_flight_console thp_cfg heater set <profile:0-3> <main_ntc:0-3> <sec_ntc:0-3> <target_cC> <delta_cC> <cutoff_cC> <timeout_ms> <resume:0|1> <slew_up> <slew_down> <kp> <ki> <kd>
dal_flight_console thp_cfg ir get <led:0-1>
dal_flight_console thp_cfg ir set <led:0-1> <camera:0|1> <x> <y> <width> <height> <target_cC> <cutoff_cC> <rise_cC> <sample_ms> <timeout_ms> <max_duty_pct> <slew_pct_per_s> <kp> <ki> <kd>
```

All multibyte integers and IEEE-754 binary32 PID gains are little-endian. Temperatures are centi-°C. Slew rates are duty percentage points per second. Get requests require bytes 3–31 to be zero. Successful TM returns the selected live record using the same layout; an MCU timeout or refusal produces standard error TM.

| Payload offset | Field | Heater (`kind=0`) | IR LED (`kind=1`) |
| ---: | --- | --- | --- |
| 0 | Action U8 | 0 get; 1 set | Same |
| 1 | Kind U8 | 0 | 1 |
| 2 | Index U8 | Heater/profile 0–3 | LED 0–1 |
| 3–4 | U8 fields | Main NTC; secondary NTC | Camera; ROI x |
| 5–7 | U8/I16 fields | Target I16 at 5–6; delta starts at 7 | ROI y; width; height |
| 8–13 | Temperature and timeout | Delta ends at 8; cutoff I16 at 9–10; timeout U16 at 11–12; resume U8 at 13 | Target I16 at 8–9; cutoff I16 at 10–11; max rise I16 at 12–13 |
| 14–15 | U8/U16 | Slew up U8; slew down U8 | Sample period U16, 250–10000 ms |
| 16–19 | U16/F32/U8 | Kp F32 | Sample timeout U16 at 16–17; max duty U8 at 18 (1–25%); slew U8 at 19 (1–100) |
| 20–23 | F32 | Ki | Kp |
| 24–27 | F32 | Kd | Ki |
| 28–31 | Reserved/F32 | Zero | Kd |

The heater timeout is 1–60000 ms; slew up/down are each 1–100. The MCU requires cutoff above setpoint, valid NTC indexes, and positive sensor delta. The IR ROI must fit 32×24; cutoff must exceed target, rise must be positive, and timeout must exceed the sample period. Its duty ceiling must also respect the channel's configured limit. PID gains must be finite and nonnegative.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x03 |
| LENGTH | U16 | 2 | 3 | 32 |
| TC Payload | U8 | 32 | 5 | See payload layout above |
| CRC-16 | U16 | 2 | 37 | - |

Both get and set use the 32-byte TC payload; byte 0 selects the action and bytes 1–2 select the profile.

### Printed page 27 (PDF page 23)

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x02 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x03 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

#### thp_ctl (0x04)

Thermal profile control for the four heater/NTC PID profiles and two IR LED/thermal-camera PID profiles. The printed revision 1.0 ICD names this address `thp_en` and specifies 0/1-byte TC and one-byte TM without defining the payload. The flight software implements the `thp_ctl` layout below using the MCU `thp_wire_ctl` command.

**Console commands**

```text
dal_flight_console thp_ctl heater <0-3> start|stop|status|resume
dal_flight_console thp_ctl ir <0-1> start|stop|status
```

`start` and `resume` require a configured profile. Heater `resume` revalidates an ERROR profile and resets its PID; it does not apply to IR. `stop` releases profile control. A successful start acknowledges the accepted request, not that the target temperature has been reached. Use `status` again to observe the subsequent state or fault. The MCU counterparts are `temp_auto_ena`, `temp_auto_stop`, `temp_profile_resume`, `temp_status`, `ir_temp_start`, `ir_temp_stop`, and `ir_temp_status`.

**TC: one-byte payload for every action**

| Bit | Meaning |
| --- | --- |
| 0–1 | Profile index: heater 0–3 or IR LED 0–1 |
| 2 | Kind: 0 heater, 1 IR LED |
| 3–4 | Action: 0 status, 1 start, 2 stop, 3 resume (heater only) |
| 5–7 | Reserved; zero |

The zero-length get in the printed ICD cannot select one of six profiles and is replaced by the one-byte `status` selector. Invalid kind/index/action combinations return standard error TM.

**TM: one-byte payload after a successful action**

| Bit | Meaning |
| --- | --- |
| 0–2 | Phase: heater 0 disabled, 1 SOAK, 2 ERROR; IR 0 IDLE, 1 STARTING, 2 WAIT_SAMPLE, 3 RUNNING, 4 FAULT |
| 3–5 | Fault reason: heater 0 none, 1 config, 2 sensor, 3 over-temperature, 4 NTC disagreement, 5 power, 6 restored, 7 manual timeout; IR 0 none, 1 config, 2 owner, 3 camera, 4 stale sample, 5 hot scene, 6 excessive rise, 7 IR service |
| 6 | Profile configured |
| 7 | Controller running: heater SOAK, or IR STARTING/WAIT_SAMPLE/RUNNING |

The one-byte TM reports a snapshot immediately after the requested action. For an IR `start`, it may therefore show STARTING; later camera or IR faults require another `status` request. A timeout, refused action, or malformed MCU reply produces standard error TM rather than this byte.

**Frame fields**

| Frame | START | TYPE/ACK | ADDR | LENGTH | Payload offset | CRC-16 offset |
| --- | --- | --- | --- | ---: | ---: | ---: |
| TC | `0xA5` | `0x02` | `0x04` | 1 | 5 | 6 |
| Success TM | `0xA5` | `0x02` | `0x04` | 1 | 5 | 6 |

#### mon_cfg (0x05)

Monitoring conﬁgure


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |

### Printed page 28 (PDF page 24)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| ADDR | U8 | 1 | 2 | 0x05 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 32: set command with 32B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x02 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x05 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

#### mon_en (0x06)

Monitoring enable


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x06 |
| LENGTH | U16 | 2 | 3 | 0/1 (footnote 1) |
| TC Payload | U8 | 0/1 | 5 | - |
| CRC-16 | U16 | 2 | 6 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 1: set command with 1B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x02 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x06 |
| LENGTH | U16 | 2 | 3 | 1 |
| TM Payload | U8 | 1 | 5 | - |
| CRC-16 | U16 | 2 | 6 | - |

### Printed page 29 (PDF page 25)

#### rsv_op1 (0x07)

LEV subsystem manual control


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x07 |
| LENGTH | U16 | 2 | 3 | 32 |
| TC Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

**TM**

TM ACK package


#### rsv_op2 (0x08)

Monitoring conﬁgure


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x08 |
| LENGTH | U16 | 2 | 3 | 32 |
| TC Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x02 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x08 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

#### rsv_op3 (0x09)

Monitoring conﬁgure


**TC**

### Printed page 30 (PDF page 26)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x02 |
| ADDR | U8 | 1 | 2 | 0x09 |
| LENGTH | U16 | 2 | 3 | 0/32 (footnote 1) |
| TC Payload | U8 | 0/32 | 5 | - |
| CRC-16 | U16 | 2 | 5/37 | - |

**Footnote 1**
TC LENGTH:

- 0: get command
- 32: set command with 32B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x02 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x09 |
| LENGTH | U16 | 2 | 3 | 32 |
| TM Payload | U8 | 32 | 5 | - |
| CRC-16 | U16 | 2 | 37 | - |

Data Transfer (TYPE = 0x03)

#### dat_lst (0x00)

List data ﬁles by type, with/without mark ﬁlter.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x03 |
| ADDR | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 1 |
| Mark filter (footnote 1) | U8 | 1 | 5 | 0xFF |
| CRC-16 | U16 | 2 | 6 | - |

**Footnote 1**
Mark ﬁlter:

- 0xFF: no ﬁlter (list all ﬁles)
- 0x00: unmarked ﬁles only
- 0x01-0xFE: mark value


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x03 |

### Printed page 31 (PDF page 27)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 12 |
| HK file count | U32 | 4 | 5 | - |
| SCI1 file count | U32 | 4 | 9 | - |
| SCI2 file count | U32 | 4 | 13 | - |
| CRC-16 | U16 | 2 | 17 | - |

#### dat_sel (0x01)

Select data ﬁle to process.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x03 |
| ADDR | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 5 |
| Data Type (footnote 1) | U8 | 1 | 5 | 0-2 |
| Data Index | U16 | 2 | 6 | 0 – 65535 |
| Chunk size (bytes) | U16 | 2 | 8 | 1 – 65535 |
| CRC-16 | U16 | 2 | 10 | - |

**Footnote 1**
Data Type:

- 0: HK
- 1: SCI1
- 2: SCI2

Chunk size is limit to 32768B.


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x03 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 41 |
| Data Type | U8 | 1 | 5 | - |
| Data Index | U32 | 4 | 6 | - |
| Chunk size (bytes) | U16 | 4 | 10 | - |
| File name | STR(20) | 20 | 14 | - |
| File size (bytes) | U32 | 4 | 34 | - |
| File CRC-32 (footnote 1) | U32 | 4 | 38 | - |
| Chunk count (footnote 2) | U32 | 4 | 42 | - |
| CRC-16 | U16 | 2 | 46 | - |

**Footnote 1**
File CRC-32: CRC-32 ISO-HDLC
**Footnote 2**
Chunk count = ROUNDUP(File size / Chunk size, 0)

### Printed page 32 (PDF page 28)

#### dat_get (0x02)

Transfer data chunk of selected data ﬁle.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x03 |
| ADDR | U8 | 1 | 2 | 0x02 |
| LENGTH | U16 | 2 | 3 | 4 |
| Chunk Index | U32 | 4 | 5 | 0–(2³²−1) |
| CRC-16 | U16 | 2 | 9 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x03 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x02 |
| LENGTH | U16 | 2 | 3 | N |
| Data Chunk | U8 | N | 5 | - |
| CRC-16 | U16 | 2 | 5+N | - |

#### dat_des (0x03)

Deselect current selected data ﬁle.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x03 |
| ADDR | U8 | 1 | 2 | 0x03 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | 0xFFFF |

**TM**

TM ACK package


#### dat_del (0x04)

Delele data ﬁle at index, by type.


**TC**

### Printed page 33 (PDF page 29)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x03 |
| ADDR | U8 | 1 | 2 | 0x04 |
| LENGTH | U16 | 2 | 3 | 5 |
| Data Type | U8 | 1 | 5 | - |
| Data Index | U32 | 4 | 6 | - |
| CRC-16 | U16 | 2 | 10 | - |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x03 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x04 |
| LENGTH | U16 | 2 | 3 | 12 |
| HK Count | U32 | 4 | 5 | - |
| SCI1 Count | U32 | 4 | 9 | - |
| SCI2 Count | U32 | 4 | 13 | - |
| CRC-16 | U16 | 2 | 17 | - |

#### dat_mrk (0x05)

Mark/label current selected data ﬁle.


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x03 |
| ADDR | U8 | 1 | 2 | 0x05 |
| LENGTH | U16 | 2 | 3 | 1 |
| Mark Value | U8 | 1 | 5 | - |
| CRC-16 | U16 | 2 | 6 | - |

**TM**

TM ACK package


#### Telemetry (TYPE = 0x04)

#### tm_get (0x00)

**TC**

### Printed page 34 (PDF page 30)

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x04 |
| ADDR | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 0 |
| CRC-16 | U16 | 2 | 5 | 0xFFFF |

**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x04 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x00 |
| LENGTH | U16 | 2 | 3 | 29 |
| System time (Linux epoch) | U32 | 4 | 5 | - |
| System status | I8 | 1 | 9 | - |
| Bootcount | U32 | 4 | 10 | - |
| Onboard Temperature | I16 | 6 | 14 | [pdu, exp, obc] |
| NTC Temperature | I16 | 8 | 20 | NTC[0,1,2,3] |
| MLX Avg Temp | I16 | 2 | 28 | — |
| BME Temperature | I16 | 2 | 30 | - |
| BME Pressure | U16 | 2 | 32 | - |
| BME Humidity | U16 | 2 | 34 | - |
| IMU Sensor Data | I16 | 14 | 36 | [t,ax,ay,az,gx,gy,gz] |
| SLF1 Sensor Data | I16 | 4 | 50 | [temp, flow] |
| SLF2 Sensor Data | I16 | 4 | 54 | [temp, flow] |
| RBA Status | U16 | 2 | 58 | - |
| TA Status | U16 | 2 | 60 | - |
| CRC-16 | U16 | 2 | 62 | - |

#### tm_cfg (0x01)

Telemetry conﬁgure


**TC**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| TYPE | U8 | 1 | 1 | 0x04 |
| ADDR | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 0/8 (footnote 1) |
| TC Payload | U8 | 0/8 | 5 | - |
| CRC-16 | U16 | 2 | 5/13 | - |

**Footnote 1**
TC LENGTH:

### Printed page 35 (PDF page 31)

- 0: get command
- 8: set command with 8B TC payload


**TM**

| Field | Data type | Size (B) | Byte offset | Printed value |
| --- | --- | --- | --- | --- |
| START | U8 | 1 | 0 | 0xA5 |
| ACK/ERR | U8 | 1 | 1 | 0x04 |
| ADDR/ERRCODE | U8 | 1 | 2 | 0x01 |
| LENGTH | U16 | 2 | 3 | 8 |
| TM Payload | U8 | 8 | 5 | - |
| CRC-16 | U16 | 2 | 13 | - |

### Printed page 36 (PDF page 32)

Data transfer
Data Description


| Data type | Size | Format |
| --- | --- | --- |
| HK | 10 KB per file | Binary |
| SCI1 | 1-2 MB per file | Image, .JPG |
| SCI2 | 100 KB per file | Binary |

Example of payload data transfer as follows.
