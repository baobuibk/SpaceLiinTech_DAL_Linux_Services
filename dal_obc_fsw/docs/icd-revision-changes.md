# ICD v1.0 → v1.2: changes and migration checklist

This page compares the original PDFs and records the work required in the current flight software and DALTUI. **This is a migration plan, not a claim that v1.2 has been implemented.** Source inspection: 2026-11-05. TODO items remain open until implementation and validation are complete.

- [Original ICD v1.0 — BEE-DAL_ICDr1p0 (1).pdf](assets/icd/BEE-DAL_ICDr1p0.pdf)
- [New ICD v1.2 — BEE-DAL_ICDr1p2_EE-TMTC.pdf](assets/icd/BEE-DAL_ICDr1p2_EE-TMTC.pdf)
- [Current flight command register](flight-command-register.md)
- [Existing ICD and console mapping](icd-console-mapping.md)

Offsets below are absolute frame byte offsets, starting at START (`0xA5`). LENGTH counts payload bytes only; total frame size is LENGTH + 7. Multibyte fields are little-endian.

## Verification of this alignment

`make check` passed. Rust formatting and 103 DALTUI tests passed. A focused 59-test simulation run covering native contracts, all sensor selectors, command inventory, framing, thermal profiles, UVC PWM and PC control passed. The full simulation was also run and still reported failures involving cached status/model assumptions, offline PDU behavior and firmware-source oracle expectations; it is not an all-green release gate. OBC, Pi and Windows artifacts were built, but physical serial/hardware acceptance has not been performed here.

## Current payload alignment

See the [v1.2 payload contracts](icd-v1p2-payload-contracts.md) for the current executable register. The native addresses now use the PDF payload lengths and uptime ACKs. Missing fields in opaque PDF arrays have explicit DAL definitions within those sizes. Detailed job replies and selected GETs were moved to separate extension addresses; their former native formats are rejected. `sys_info` self-test, sensor report selectors 0–4, empty profile GET and empty thermal-enable GET are implemented. The 11 unavailable commands remain a hardware implementation TODO, including `lev_cfg` and `rba_cfg`.

## Changes in the PDFs

Status reflects the current source/build, not installation on physical hardware. [Implemented v1.2 command layouts and limits](icd-v1p2-console-mapping.md).

| Command / field | v1.0 | v1.2 | Interpretation | Implementation status |
| --- | --- | --- | --- | --- |
| `01/05` sensor configuration | `sen_bme_cfg`, BME configuration | `sen_cfg`, sensor configuration | Renamed/generalized; TC remains 0/8 bytes and TM remains 8 bytes | Implemented — native `sen_cfg`; GET/SET 0/8 bytes, TM 8 bytes |
| `01/06` | `sen_imu_cfg` | `sen_stat` | Address changes function; separate IMU configuration command removed | Implemented — native `01/06` is sensor status; old IMU alias uses `02/0C` |
| `sen_stat` address | `01/07` | `01/06` in TC and TM | Address migration; `01/07` is no longer listed | Implemented with compatibility extension — native `01/06`; legacy `01/07` retained separately |
| `sen_stat` TC LENGTH | 2, despite a one-byte report field | 1 | Corrected payload length | Implemented — one-byte selectors 2 (BME) and 3 (IMU); 0/1/4 remain open |
| `sen_stat` TM | No separate payload cap stated | Maximum 200 bytes | Explicit upper bound | Implemented for supported reports — BME 8 bytes, IMU 16 bytes; both below 200 |
| `pwr_stat` (`01/04`) TC LENGTH | 2, despite a one-byte report field | 1 | Corrected payload length | Implemented — one-byte selectors 0/1/2/3; legacy board/mask TC rejected |
| `pwr_stat` TM | No separate payload cap stated | Maximum 200 bytes | Explicit upper bound | Implemented — aggregate reports 22–184 bytes, including a 16-byte PDU status trailer; hard cap 200; hardware fault-latch data unavailable |
| `thm_stat` (`01/15`) TM | LENGTH 6; CRC at 11 | LENGTH 8; CRC at 13 | Total frame grows from 13 to 15 bytes | Implemented — both FSW source paths emit 8 bytes; DALTUI checks length/reserved bytes |
| `lev_calib` address | Heading says `0x20`; TC/TM tables say `0x21` | TC/TM tables use `0x20` | Corrected conflict with `lev_cfg` at `0x21` | Open — command not implemented |
| `lev_calib` SET note | Says 128 bytes although table says 130 | Says 130 bytes | Note corrected; TC remains 0/130 bytes, TM 130 bytes | Open — command not implemented |
| `sys_rtc_cfg` (`01/01`) TC CRC | Offset 9 | Offset 5/9 | Correct offsets for GET with no payload and SET with 4 bytes | Existing framing supports variable payload CRC; no new implementation in this migration |
| `thp_en` (`02/04`) TC CRC | Offset 6 | Offset 5/6 | Correct offsets for GET with no payload and SET with 1 byte | Implemented — native TC 0/1 and TM 1; empty GET reads heater 0 status |
| `mon_en` (`02/06`) TC CRC | Offset 6 | Offset 5/6 | Correct offsets for GET with no payload and SET with 1 byte | Open — command not implemented |
| `tm_get` (`04/00`) TM LENGTH | 29 | 57 | Corrected to match the existing field table; fields and CRC offset 62 are unchanged | Implemented — 57-byte encoder, 64-byte TM, CRC at 62; unavailable fields use documented sentinels |
| `dat_sel` (`03/01`) TC / TM | TC 5 bytes, U16 index; TM chunk-size type inconsistent | TC 7 bytes, U32 index; TM chunk size U32, 41 bytes | Sender/receiver migration required | Implemented — FSW, console, PC client and DALTUI OBC → Data transfer; exact lengths and CRC32-verified download |

Operational file classification has also been updated: **HK (0) = housekeeping; SCI1 (1) = images; SCI2 (2) = LWL/PC logs**. This is a project mapping change, not a change to the ICD frame. Original folders are preserved; run `dat_lst` again after deployment.

### File selection: dat_sel (`03/01`)

Source: v1.0 printed page 31; v1.2 printed pages 30–31. Printed page numbers are four greater than PDF viewer page numbers in these files.

| Field | v1.0 | v1.2 |
| --- | --- | --- |
| TC LENGTH | 5 | 7 |
| TC Data Type | U8 at offset 5 | Unchanged |
| TC Data Index | U16 at offset 6; 0–65535 | U32 at offset 6; 0–4294967295 |
| TC Chunk size | U16 at offset 8; table says 1–65535 | U16 at offset 10; 1–32768 |
| TC CRC-16 | Offset 10 | Offset 12 |
| TC total frame | 12 bytes | 14 bytes |
| TM Chunk size type | U16, but Size says 4 bytes | U32, Size 4 bytes |
| TM layout | LENGTH 41, CRC at 46 | Unchanged: total 48 bytes |

Implementation: **completed** for TYPE `03` commands. [Wire layouts and DALTUI workflow](icd-v1p2-console-mapping.md#data-transfer-v12-type-0x03). Old five-byte selection frames are rejected; no implicit legacy fallback.

The v1.0 prose already limits chunks to 32768 bytes. v1.2 makes the TC range consistent with that limit. The TM type correction does not move any TM fields.

### Editorial changes

- `rba_stat` description changes from fluidic status to RBA status.
- `rsv_op1`, `rsv_op2`, and `rsv_op3` are described as Reserved op#1/#2/#3 instead of LEV manual control or monitoring configuration.
- Several configure/status descriptions are reworded without frame changes.
- The PDFs contain 32 and 31 pages respectively. Tables are repaginated; the new `dat_lst` TM table is no longer split across pages.

### Unchanged requirements

- Connector and pinout; power specifications; RS-422 transceiver specifications.
- UART 921600 baud, 8N1; little-endian multibyte fields; 2000 ms packet timeout.
- TC/TM envelope, error codes, CRC-16 and CRC-32 parameters.
- Generic TM ACK: U32 uptime, LENGTH 4, CRC offset 9, total 11 bytes.
- `dat_lst`: exactly one Mark filter byte; `0xFF` lists all, `0x00` unmarked, `0x01–0xFE` a specific mark. TM has three U32 counts, LENGTH 12. **No zero-payload TC variant is specified.**
- `dat_get`, `dat_des`, `dat_del`, `dat_mrk`, and the illustrated download sequence.
- Data type names HK, SCI1, SCI2 and their data-description table.

## Sensor implementation update

The [v1.2 sensor mapping](icd-v1p2-console-mapping.md) now implements `sen_cfg` GET/SET at `01/05`, native BME/IMU reports at `01/06`, and DALTUI EXP → Sensors controls. `01/07` remains an explicit legacy extension. Native sensor selectors 0/1/4 and unrelated migrations remain open. Native `pwr_stat` now uses a one-byte report selector with a maximum 184-byte response; `thm_stat` returns eight bytes. The findings and checklist below are the original pre-migration audit; completed sensor work is documented on the linked page rather than implying every migration item is complete.

## Current implementation findings

The current implementation contains extensions that already differ from v1.0. They must not be mistaken for changes introduced by v1.2.

| Area | Current implementation | Migration consequence |
| --- | --- | --- |
| `dat_sel` | Migrated: exact 7-byte U8/U32/U16 TC; 41-byte TM. Console/PC/DALTUI updated. | Deploy matching FSW and clients; old 5-byte requests are rejected. |
| `sen_stat` | Dispatcher uses `01/07`; handler requires four request bytes (board, group, source, cursor). Maximum reply is 106 bytes. | Moving to `01/06` alone is insufficient: report selection and reply semantics must also match the ICD. Existing reply cap is already below 200. |
| `sen_cfg` | No `01/05` handler in the dispatcher. | Implement the generic configuration command after its opaque 8-byte payload is defined. |
| `pwr_stat` | Handler requires three bytes; DALTUI sends board plus U16 mask. | This is an existing extension, not the one-byte ICD report selector. Both sides need a coordinated mapping. |
| `thm_stat` | FSW emits six packed duty/running bytes from CSP or `THM_STAT6` UART fallback. DALTUI accepts exactly six bytes. | Both encoder and decoder must change; the PDF does not explain the new eight-byte field contents. |
| `tm_get` | FSW now serializes the complete 57-byte v1.2 layout from cached sources. | See the native mapping for units, status words and unavailable values; MLX average has no verified cache source yet. |
| DALTUI telemetry | Uses `tm_obc_get` (`04/02`), not a direct `tm_get` (`04/00`) builder. | Preserve the distinction between the OBC extension and payload telemetry. |
| DALTUI file transfer / sensors | Added EXP → Sensors and OBC → Data transfer with native v1.2 commands. EXP IMAGE TRANSFER remains collector status. | Update clients with FSW; the new download page transfers files already stored on OBC. |
| LEV / monitor commands | `01/20` and `02/06` are not dispatched; `02/04` now follows the detailed native `thp_en` envelope. | Correct documentation first; implementation requires explicit command semantics and conflict resolution. |

Inspection points: FSW `src/services/bee_dal_tmtc_service.c`, `src/services/flight_console_service.c`, `include/services/sensor_stat.h`; PC client `tool/pc_client/bee_dal_client/`; DALTUI `src/proto.rs`, `src/live.rs`.

## Flight software TODO

### P0 — define the migration contract

- [x] Document the TYPE `03` transition: deploy FSW and clients together; exact seven-byte `dat_sel`, old five-byte requests rejected. No automatic legacy fallback.
- [x] Define the v1.2 `sen_cfg` eight-byte payload and sensor reports. DAL assignments supplement opaque PDF arrays without changing native lengths.
- [x] Define the eight-byte `thm_stat` payload. Six existing packed channel bytes are retained; bytes 6–7 are explicitly defined as reserved zero in the DAL layout, not claimed to be specified by the PDF.
- [x] Map `pwr_stat` report selectors to bounded aggregate OBC/EXP/PDU records. Native `sen_stat` supports all five report selectors; the legacy board/group sensor extension remains separate.
- [x] Align `02/04` with the detailed PDF: TC 0/1, TM 1; define empty GET as heater 0 status.

### P1 — serialization, dispatch, and clients

- [x] Change `handle_dat_sel()` to require the v1.2 seven-byte payload and read U8 type, U32 index, U16 chunk size. Validate the whole request before changing the selected-file state.
- [x] Change console `dat_sel` parsing to allow UINT32_MAX and serialize seven bytes. Update PC client `DAT_SEL_TC_FMT`, input validation, help, and fixtures; retain U32 chunk size in the TM.
- [x] Audit file-index storage and APIs for narrowing to U16. Keep the maximum chunk at 32768 and exercise the full-frame buffers at that boundary.
- [x] Implement `sen_stat` at `01/06` with selectors 0–4, bounded compact reports, validity masks and invalid sentinels. Keep the paginated board report at extension `01/07`.
- [x] Implement `sen_cfg` at `01/05` for BME/IMU configuration; native `01/06` is sensor status.
- [x] Align `pwr_stat` with its one-byte report request and enforce TM payload <= 200 bytes; retain the same cap for `sen_stat`.
- [x] Emit the agreed eight-byte `thm_stat` payload consistently for both CSP cache and UART fallback. Check EXP firmware dependencies before claiming compatibility.
- [x] Replace legacy `tm_get` serialization with the 57-byte ICD field sequence: time, status, bootcount, three board temperatures, four NTC temperatures, MLX average, BME data, seven IMU values, two flow-sensor pairs, RBA status, TA status. Define scaling, validity, and missing-data handling where the PDF is insufficient.
- [x] Update PC client telemetry decoding and any shared wire-layout constants for `tm_get`; keep `tm_obc_get` separate.
- [ ] Audit GET/SET framing for `sys_rtc_cfg`, `thp_en`, and `mon_en`. CRC placement must follow actual LENGTH; do not claim unsupported commands are implemented merely because offsets were corrected.
- [ ] Correct LEV address constants/documentation to `lev_calib = 01/20`, SET length 130; track implementation as a separate gap until the command is supported.

### P2 — verification and release

- [ ] Add exact-byte frame tests for changed TYPE/ADDR/LENGTH, LE encoding, CRC and total length. Cover malformed lengths and unsupported report selectors.
- [x] Test `dat_sel` indexes 65535, 65536, and UINT32_MAX without truncation (mock the index lookup where needed); chunks 1 and 32768; reject 0 and 32769. Test complete download and CRC verification.
- [ ] Update console integration tests, `tests/test_exp_csp.py`, and data-transfer, actuator, and telemetry simulation fixtures for the agreed layouts.
- [ ] Run `make check` and `make fsw-sim-test`; isolate simulation data directories when running independent suites.
- [x] Update the command register, v1.2 payload definitions, frame examples and version compatibility notes. Preserve the v1.0 transcription as historical reference.
- [x] Build/stage with `make deploy` and matching DALTUI packages. Hardware acceptance remains pending; this payload migration does not require EXP firmware changes.

## DALTUI TODO

### P1 — existing controls and status

- [x] Update `proto::pwr_stat()` and `dec_pwr_stat()` for the agreed v1.2 report mapping. Update OBC/EXP/PDU polling and Read status actions together; a one-byte request cannot simply reuse the current board/mask meaning.
- [x] Update `dec_thm_stat()` and its use in `live.rs` for the agreed eight-byte payload. Validate field semantics and keep heater/IR channel states correct; do not just relax the length check.
- [x] Update console command strings and RS-422 builders together; rich jobs and selected GETs use separate extension addresses.
- [ ] Keep `tm_obc_get` decoding separate. If `tm_get` is introduced into payload monitoring, add a dedicated 57-byte decoder and explicit mapping into the model/status panels.
- [ ] Update simulator responses and protocol fixtures for changed live paths; retain the established status/control colors and English UI text.
- [ ] Display a clear protocol/length error for incompatible replies rather than showing data decoded with the wrong layout.

### Conditional work — not existing DALTUI builders

- [ ] If direct sensor commands are added, use `sen_stat = 01/06` and the agreed one-byte report selector; use `sen_cfg = 01/05`. Do not confuse current camera CLI monitoring with `sen_stat`.
- [x] If file-transfer controls are added, serialize `dat_sel` as U8/U32/U16 (seven bytes), accept a U32 index, and cap chunks at 32768. IMAGE TRANSFER status alone does not require a new `dat_sel` UI.
- [ ] If LEV or monitor controls are added, use the corrected addresses and GET/SET layouts after their command semantics are defined.

### P2 — verification and delivery

- [ ] Add focused request/response tests for each changed builder and decoder, including truncated replies and wrong-version lengths.
- [ ] Run Rust formatting checks, tests, and release builds; verify simulator, OBC console socket, and physical RS-422 behavior.
- [x] Build matching OBC ARM, Raspberry Pi aarch64 and Windows x64 DALTUI artifacts; update package compatibility notes.

## Already aligned or unchanged — preserve during migration

- `dat_lst` requires an explicit one-byte mark in current FSW and console code. Preserve this behavior and its tests.
- Current `dat_sel` TM already writes U32 chunk size and 41 payload bytes; no TM offset migration is needed for that correction.
- Generic uptime ACK remains unchanged. Do not convert command-specific replies into uptime ACKs indiscriminately.
- No connector, UART baud-rate, CRC algorithm, or DALTUI navigation redesign is required by the PDF changes.
