# ICD v1.2 payload contracts

The [revision 1.2 PDF](assets/icd/BEE-DAL_ICDr1p2_EE-TMTC.pdf) defines the native command addresses, payload lengths, primitive types, report selectors and ACK package. These are mandatory. Where it specifies only an opaque byte array, the DAL field assignments below complete that definition **inside the prescribed length**. They are project definitions, not additional fields printed in the PDF.

The [PDF transcription](icd1.2.md) remains the source reference. The older copied implementation sections in [console mapping](icd-v1p2-console-mapping.md) describe historical payloads; the contracts on this page supersede conflicting lengths and addresses there. Runtime device failures still return an error frame; an implemented handler does not guarantee attached hardware is online.

## Framing and acknowledgements

`A5 TYPE ADDR LENGTH_U16_LE PAYLOAD CRC16_U16_LE`; total size is `LENGTH + 7`. TM success echoes TC TYPE and ADDR. Error TM uses `FF ERRCODE 00 00 FF FF` after the `A5` start byte. DAL applies CRC-16/CCSDS to the payload only (initial `FFFF`, polynomial `1021`, no reflection, no final XOR); empty-payload CRC is `FFFF`. The PDF gives the CRC parameters but does not explicitly specify its byte coverage; payload-only coverage is the DAL definition.

Every native **TM ACK package** has exactly four payload bytes: `U32 LE system uptime in seconds`. A queue ticket, an empty reply, a capture result or a motor position is not this ACK. Acceptance acknowledges dispatch, not completion of a physical operation. Query the subsystem status or a documented DAL job extension for completion.

## Native command register

Lengths below are **payload bytes**, excluding the five-byte header and two-byte CRC. “Unavailable” means there is no hardware handler, not a different successful payload format.

| TYPE/ADDR | Command | TC bytes | Successful TM bytes | Definition / support |
| --- | --- | ---: | ---: | --- |
| 00/00 | ping | 0 | 4 | Uptime ACK |
| 01/00 | sys_info | 0 / 1 | 96 | [Identity and self-test](#identity-and-self-test) |
| 01/01 | sys_rtc_cfg | 0 / 4 | 4 | UTC Unix epoch U32; console alias `sys_rtc` |
| 01/02 | pwr_adj_cfg | 0 / 8 | 8 | [Adjustable power](#adjustable-power) |
| 01/03 | pwr_en | 0 / 2 | 2 | [Power selection](#power-selection) |
| 01/04 | pwr_stat | 1 | N ≤ 200 | Selectors 0–3; [power records and PDU trailer](icd-v1p2-console-mapping.md#pwr_stat-0104-native-v12) |
| 01/05 | sen_cfg | 0 / 8 | 8 | [Both-sensor configuration](icd-v1p2-console-mapping.md#sen_cfg-0105-frames) |
| 01/06 | sen_stat | 1 | 8–58 | [All five report selectors](#sensor-reports) |
| 01/10 | uvc_cfg | 0 / 32 | 32 | [Camera configuration](#camera-configuration-and-status) |
| 01/11 | uvc_ctl | 8 | 4 | Camera capture request; uptime ACK |
| 01/12 | uvc_stat | 0 | 6 | [Aggregate camera status](#camera-configuration-and-status) |
| 01/13 | thm_cfg | 0 / 32 | 32 | [Thermal limit records](icd-v1p2-console-mapping.md#thm_cfg-payload-layout) |
| 01/14 | thm_ctl | 8 | 4 | [Manual thermal control](icd-v1p2-console-mapping.md#current-thm_ctl-implementation); uptime ACK |
| 01/15 | thm_stat | 0 | 8 | [Thermal status](icd-v1p2-console-mapping.md#thm_stat-0115-native-v12) |
| 01/16 | fld_cfg | 0 / 32 | 32 | [Fluidic configuration](#fluidic-and-experiment-configuration) |
| 01/17 | fld_ctl | 8 | 4 | [Valve/pump request](icd-v1p2-console-mapping.md#fld_ctl-manual-valve-and-pump-control); uptime ACK |
| 01/18 | fld_stat | 0 | 16 | [Fluidic status](icd-v1p2-console-mapping.md#fld_stat-actuator-status) |
| 01/19 | rba_cfg | 0 / 32 | 32 | Unavailable — no OBC handler |
| 01/1A | rba_ctl | 8 | 4 | [Motor requests](#motor-control) |
| 01/1B | rba_stat | 0 | 16 | [Motor/limit status](icd-v1p2-console-mapping.md#current-rba_stat-implementation) |
| 01/20 | lev_calib | 0 / 130 | 130 | Unavailable — no OBC handler |
| 01/21 | lev_cfg | 0 / 32 | 32 | Unavailable — no OBC handler |
| 01/22 | lev_ctl | 8 | 4 | Unavailable — no OBC handler |
| 01/23 | lev_stat | 0 | 16 | Unavailable — no OBC handler |
| 02/00 | exp_stat | 0 | 1 | [PC experiment state](icd-v1p2-console-mapping.md#pc_stat-0x00); alias `pc_stat` |
| 02/01 | exp_cfg | 0 / 32 | 32 | [Experiment configuration](#fluidic-and-experiment-configuration); alias `pc_cfg` |
| 02/02 | exp_ctl | 8 | 4 | [Experiment operation](icd-v1p2-console-mapping.md#pc_ctl-0x02); alias `pc_ctl`; uptime ACK |
| 02/03 | thp_cfg | 0 / 32 | 32 | [Thermal profiles](#thermal-profiles) |
| 02/04 | thp_en | 0 / 1 | 1 | [Thermal profile enable/status](#thermal-profiles); detailed PDF TM table takes precedence over summary “ACK” wording |
| 02/05 | mon_cfg | 0 / 32 | 32 | Unavailable — no OBC handler |
| 02/06 | mon_en | 0 / 1 | 1 | Unavailable — no OBC handler |
| 02/07 | rsv_op1 | 32 | 4 | Unavailable — reserved operation |
| 02/08 | rsv_op2 | 32 | 32 | Unavailable — reserved operation |
| 02/09 | rsv_op3 | 0 / 32 | 32 | Unavailable — reserved operation |
| 03/00 | dat_lst | 1 | 12 | [File transfer](icd-v1p2-console-mapping.md#data-transfer-v12-type-0x03) |
| 03/01 | dat_sel | 7 | 41 | Same; type U8, index U32, requested chunk U16 |
| 03/02 | dat_get | 4 | N | Same; chunk index U32 |
| 03/03 | dat_des | 0 | 4 | Release selected file; uptime ACK |
| 03/04 | dat_del | 5 | 12 | Type U8, index U32; three U32 counts |
| 03/05 | dat_mrk | 1 | 4 | Mark U8; uptime ACK |
| 04/00 | tm_get | 0 | 57 | [Fixed telemetry layout](icd-v1p2-console-mapping.md#tm_get-0400-native-v12) |
| 04/01 | tm_cfg | 0 / 8 | 8 | Unavailable — no OBC handler |

The 11 unavailable commands return `ERR 03`; they do not return fabricated configuration, status or success ACKs. `lev_cfg` is among these. Hardware support for these commands is still a separate TODO.

## Identity and self-test

GET is empty. SET is exactly `01` (one byte) and performs the same non-actuating identity/connectivity checks against OBC, EXP and PDU, returning their availability in the 96-byte report. This checks identity communication, not motor actuation, sensor calibration or a destructive hardware test.

The existing report has a 12-byte header and three 28-byte records. Header: schema U8 at 0, record count U8 at 1, online mask U8 at 2 (PDU bit 0, EXP bit 1, OBC bit 2), diagnostic U8 at 3, epoch U32 at 4, four reserved zero bytes at 8. Diagnostic is zero for GET; for self-test it is `80 OR online_mask`. A record is board ID U8, online U8, hardware version U8[3], firmware version U8[3], build identifier char[20]. IDs are PDU=1, EXP=2, OBC=3. Unavailable version bytes are `FF`. [Record interpretation](icd-v1p2-console-mapping.md#sys_info-0x00).

Console: `sys_info` or `sys_info self_test`.

## Adjustable power

GET has no payload. SET has exactly eight bytes:

| Payload offset | Type | Meaning |
| --- | --- | --- |
| 0 | U16 | VOUT target, mV, 800–20000 |
| 2 | U8[6] | Reserved; must be zero |

TM remains four U16 words: setpoint mV, measured voltage mV, measured current mA, flags. Flags: bit 0 TRANS on, 1 VOUT on, 2 setpoint valid, 3 measured voltage valid, 4 measured current valid, 5 VOUT state valid. Invalid values use `FFFF`; read flags before using a value. TPS measurements can be unavailable while TRANS is off. Console `pdu_vout get|set <mV>` and `pwr_adj_cfg get|set <mV>` both use this native 0/8-byte contract.

## Power selection

The PDF leaves the meaning of its U16 payload unspecified. DAL defines a masked write so changing one rail never implicitly changes all other rails.

TC SET is one U16: bits 0–12 select rails, bit 13 is the requested state (1 on, 0 off), bit 14 forces EXP off, bit 15 is reserved zero. At least one rail must be selected. Force is allowed only for an OFF request selecting exclusively EXP rails. A single request applies the same state to every selected rail. Mixed on/off changes require separate requests.

| Global bits | Rails, in ascending order |
| --- | --- |
| 0–7 | OBC AI, FPGA, USB1, USB2, USB3, USB4, MIPI1, MIPI2 |
| 8–10 | EXP 12V_EXP, SLN_HD, MOTOR |
| 11–12 | PDU VOUT, TRANS |

TC GET is empty. TM is one U16: bits 0–12 are readback ON states in the same order; bits 13, 14, 15 say whether the entire OBC, EXP, PDU board readback is valid. Do not interpret OFF from an invalid board as a verified measurement; use `pwr_stat` for detailed per-rail validity. Selected write failures return an error. Multi-board writes are not transactional: completed changes are not rolled back if another board fails. PDU and EXP retain their existing hardware interlocks.

Console: `pwr_en get`, `pwr_en on <global_mask>`, `pwr_en off <global_mask> [force]`. Example `pwr_en on 0x4` turns on OBC USB1. The existing board/select/on console syntax uses the separate `01/32` extension below to retain per-rail known/failed masks.

## Sensor reports

TC is one U8 selector, exactly as the PDF: 0 all, 1 temperature, 2 BME, 3 IMU, 4 flow. Each group is a U16 validity mask followed by fixed I16 channel values. Bit `i` validates value `i`. Invalid values are `-32768` (`0080` in wire byte order), never an invented zero. Values outside I16 range become invalid. There is no schema header or pagination in these native reports.

| Selector | TM bytes | Value order and units |
| --- | ---: | --- |
| 1 | 24 | OBC board, OBC NTC1–4, PDU board, EXP board, EXP NTC1–4; all 0.01 °C |
| 2 | 8 | EXP BME temperature 0.01 °C, pressure 10 Pa, humidity 0.01 %RH |
| 3 | 16 | EXP IMU temperature 0.01 °C, acceleration XYZ mg, gyro XYZ degrees/s |
| 4 | 10 | EXP SLF1 flow µL/min, SLF1 temperature 0.01 °C, SLF2 flow µL/min, SLF2 temperature 0.01 °C |
| 0 | 58 | Concatenate groups 1, 2, 3, 4, including each group's validity mask |

The values come from existing OBC/EXP/PDU snapshots. No new camera capture is triggered. Unavailable or expired cache channels have clear mask bits and invalid sentinels. The separate paginated board/group/source report remains `01/07`.

## Camera configuration and status

Native `uvc_cfg` GET is empty and reads the default camera **1**. SET is 32 bytes and selects camera 1–4 in byte 0. The remaining existing layout is unchanged: schema U8=1 at 1, field mask U16 at 2, FOURCC[4] at 4, width/height U16 at 8/10, FPS numerator/denominator U16 at 12/14, exposure mode U8 at 16, exposure time U32 at 17, gain U16 at 21, autofocus U8 at 23, focus U16 at 24, automatic white balance U8 at 26, white-balance temperature U16 at 27, brightness I16 at 29, LED duty U8 at 31. Field-mask bits 0–12 follow that order (FPS is one field); bits 13–15 are reserved. TM is the effective 32-byte profile. [Field constraints](icd-v1p2-console-mapping.md#current-uvc_cfg-implementation-type-0x01-addr-0x10).

Native `uvc_ctl` TC is eight bytes: camera U8 at 0, operation U8=1 (capture) at 1, reserved U16=0 at 2, nonzero request ID U32 at 4. Successful TM is the four-byte uptime ACK. The caller already knows the supplied request ID.

Native `uvc_stat` TC is empty. Its six-byte TM contains **three U16 words**:

| Offset | Meaning |
| --- | --- |
| 0 | Latest job state, four nibbles for cameras 1–4: 0 unknown, 1 queued, 2 capturing, 3 saved, 4 failed |
| 2 | Latest job error, four corresponding nibbles: 0 none, 1 capture/LED failure, 2 missing/empty output |
| 4 | Known-job mask, bits 0–3 correspond to cameras 1–4; bits 4–15 reserved zero |

State is updated by the capture worker. Cameras with no job since FSW startup have unknown state and a clear known bit. The native summary does not include a filename or ticket. Use file transfer to collect saved images; use `01/31` for a particular request ID. Targeted profile GET and rich capture replies are extensions, not alternative native payload sizes.

## Fluidic and experiment configuration

`fld_cfg` (`01/16`) and `exp_cfg` (`02/01`, alias `pc_cfg`) have empty GET and 32-byte SET. SET requires byte 0 = 1. The existing [fluidic field/group IDs](icd-v1p2-console-mapping.md#fld_cfg-current-payload-layout) and [experiment configuration fields](icd-v1p2-console-mapping.md#pc_cfg-current-payload-layout) occupy the remaining bytes. Responses are always 32 bytes: a SET echo/result or the first configuration JSON page for GET. A JSON page has total length U16 at 0, byte offset U16 at 2 and up to 28 JSON bytes at 4 (zero padding). Selected subsequent pages use the extensions below, never a native 32-byte GET masquerading as SET.

Native `fld_ctl` retains its eight-byte device/operation/channel/valve-mask/value request; successful response is always uptime ACK, including queued pump operations. `exp_ctl` retains its eight-byte operation/selector/reserved request and also returns uptime ACK.

## Motor control

Native `rba_ctl` TC remains eight bytes: action U8, motor U8, speed U8, reserved U8=0, value I32/U32. [Action definitions and hardware constraints](icd-v1p2-console-mapping.md#current-rba_ctl-implementation) remain applicable to initialization, move, goto, jog, home, stop, disable and stop-hold. Native success always returns uptime ACK. The former action 8 result query is available only on `01/33`; it is rejected on native `01/1A` because a 12-byte result is not a TM ACK package.

## Thermal profiles

Native `thp_cfg` (`02/03`) GET is empty and reads default **heater profile 0**. SET is 32 bytes with byte 0=1. Byte 1 selects heater=0 / IR=1 and byte 2 selects its profile. The existing [heater/IR profile field layouts](icd-v1p2-console-mapping.md#thp_cfg-0x03) fill the remainder. TM is always 32 bytes. A targeted profile GET uses `02/10`.

Native `thp_en` (`02/04`, console alias `thp_ctl`) accepts empty GET or one-byte SET and returns one byte, following the detailed PDF TM table. Empty GET reads heater 0 status. For SET: bits 0–1 channel, bit 2 kind (heater 0 / IR 1), bits 3–4 action (0 status, 1 start, 2 stop, 3 heater resume), bits 5–7 zero. Heater channels 0–3 and IR channels 0–1 are valid. TM: phase bits 0–2, reason bits 3–5, configured bit 6, running bit 7. [State codes](icd-v1p2-console-mapping.md#thp_ctl-0x04).

## DAL extensions outside ICD command addresses

These use the same framing but are **not commands defined by the PDF**. They preserve richer DALTUI controls without overloading native ICD payloads. Update FSW and DALTUI together.

| TYPE/ADDR | Purpose | TC → TM payload bytes | Console use |
| --- | --- | --- | --- |
| 01/30 | Targeted UVC profile GET | camera U8 (1) → profile (32) | `uvc_cfg <camera> get` |
| 01/31 | UVC job status | camera U8 + request U32 (5) → camera/state/error/reserved/request U32 (8) | `uvc_stat <camera> get <id>` |
| 01/32 | Board power selection/readback | board U8 (1), or board/select U16/on U16/force U8 (6) → board/on U16/known U16/failed U16 (7) | `pwr_en <board> get` or existing board/output/set syntax |
| 01/33 | Motor job commands/result | Same motor request (8) → ticket U32 (4), immediate stop/disable (0), or result (12) | `rba_job <action…>`; result is ticket U32, state U8, reserved[3], position I32 |
| 01/34 | Pump job commands | Same fluidic request (8) → ticket U32 (4), or immediate acceptance (0) | `fld_job pump …` |
| 01/35 | Capture with rich reply | Same capture request (8) → camera/state/error/reserved/request U32 (8) | `uvc_job <camera> capture <id>` |
| 01/36 | Fluidic configuration page | Existing page request (32) → page (32) | `fld_cfg get <page>` |
| 02/10 | Targeted thermal profile GET | Existing kind/profile query (32) → profile (32) | `thp_cfg heater get <profile>` / `thp_cfg ir get <led>` |
| 02/12 | Experiment configuration page | Existing page request (32) → page (32) | `pc_cfg get <page>` |

No selector bytes are added to native empty GETs. Their default target is defined above. Extensions are explicit alternatives when a client needs a different target, richer validity, a page cursor or asynchronous job details.

## Native TC examples

These examples include the complete frame and payload-only CRC. Values are hexadecimal bytes.

| Request | Complete RS-422 TC |
| --- | --- |
| System identity GET | `A5 01 00 00 00 FF FF` |
| System connectivity self-test | `A5 01 00 01 00 01 D1 F1` |
| VOUT SET 12000 mV | `A5 01 02 08 00 E0 2E 00 00 00 00 00 00 79 1D` |
| Power GET | `A5 01 03 00 00 FF FF` |
| OBC USB1 ON | `A5 01 03 02 00 04 20 A9 F5` |
| Sensor report: all | `A5 01 06 01 00 00 F0 E1` |
| Camera configuration GET (camera 1) | `A5 01 10 00 00 FF FF` |
| Camera summary GET | `A5 01 12 00 00 FF FF` |
| Thermal profile GET (heater 0) | `A5 02 03 00 00 FF FF` |
| Thermal enable/status GET (heater 0) | `A5 02 04 00 00 FF FF` |
