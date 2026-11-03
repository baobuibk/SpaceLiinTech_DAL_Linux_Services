# Flight software architecture and ICD implementation

This page describes the current `service/dal_obc_fsw` source, compared with the revision 1.0 command register in `docs/icd.md`. “Implemented” below means that `dispatch()` routes the TYPE/ADDRESS to a handler; it does **not** mean the attached EXP, PDU, camera, or board hardware was verified. The wire contract is the **current source**, which sometimes differs from the printed ICD. See `docs/flight-command-register.md` for payload layouts and console syntax.

## Runtime architecture

```text
External host ── RS-422 binary frames ── /dev/ttyS1 ── decoder ─┐
                                                               ├─ serialized dispatch() ── command handlers
OBC application ── Unix socket text ── flight_console_service ─┘         │
                                                                         ├─ OBC power, RTC, USB camera
hk_service snapshot ── JSON ── hk_service.c cache ────────────────────────┤
EXP MCU ── UART CLI ── exp_uart_service.c ────────────────────────────────┤
PDU ── Modbus RTU ── gui_state.c / power services ────────────────────────┤
/home/bee/data ── bee_dal_files.c ────────────────────────────────────────┘
```

`src/main.c` starts the snapshot cache, RS-422 receiver, local console, and GUI state workers. `lib/bee_dal_proto/` decodes/encodes frames and CRCs. `src/services/bee_dal_tmtc_service.c` owns the command dispatcher; both ingress paths take its mutex, so a slow handler blocks the next ICD command. `flight_console_service.c` parses text into the same TYPE/ADDRESS and payload and formats text or JSON replies. The two ingress paths also share one file selection. `gui_state` is a socket-only snapshot request; `gui_shell` runs in a separate worker.

The flight software **reads**, rather than samples, OBC housekeeping: `hk_service` publishes `/run/dal/hk_service/obc_data.json`, and `hk_service.c` refreshes a protected cache each second. Data older than 30 seconds invalidates `tm_get` and `tm_obc_get`. EXP commands use the configured `DAL_EXP_UART_PORT` (default baud 250000); the optional periodic EXP GUI cache requires `DAL_EXP_UART_POLL=1`. PDU commands use Modbus on `/dev/ttyS3` by default (`DAL_GUI_PDU_PORT` overrides it). The file backend indexes logs, images, and data below `/home/bee/data`; `DAL_DATA_DIR` overrides that root for tests.

## Revision 1.0 ICD coverage

The comparison covers **all 43 command rows** in `docs/icd.md` / the printed PDF: **30 routed, 13 not routed**. “Routed” can still produce a data or unknown error if a device is unavailable or a payload is invalid. No `0x05` SFU command is defined as an individual row in the PDF register; the dispatcher rejects the entire family.

| TYPE/ADDR | ICD command | Current implementation |
| --- | --- | --- |
| `00/00` | `ping` | Routed; U32 uptime reply. |
| `01/00` | `sys_info` | Routed for zero-length read; **one-byte self-test request is not implemented**. Current reply is 96 bytes. |
| `01/01` | `sys_rtc_cfg` | Routed; read or set Linux clock. |
| `01/02` | `pwr_adj_cfg` | Routed as PDU VOUT get/set; current set payload is U16 mV, not the PDF's opaque 8 bytes. |
| `01/03` | `pwr_en` | Routed; OBC, EXP, and PDU rails. |
| `01/04` | `pwr_stat` | Routed; current request is board U8 plus source mask U16. |
| `01/05` | `sen_bme_cfg` | **Not routed** at this address. EXP BME configuration is available via `02/0C` text gateway. |
| `01/06` | `sen_imu_cfg` | **Not routed** at this address. EXP IMU configuration is available via `02/0C`. |
| `01/07` | `sen_stat` | Routed; current four-byte selector and paged records differ from PDF. |
| `01/10` | `uvc_cfg` | Routed; USB camera profile via external `usb_cam` tool. |
| `01/11` | `uvc_ctl` | Routed; queue a JPEG capture. |
| `01/12` | `uvc_stat` | Routed; poll capture state. |
| `01/13` | `thm_cfg` | Routed to EXP UART CLI. |
| `01/14` | `thm_ctl` | Routed to EXP UART CLI. |
| `01/15` | `thm_stat` | Routed to EXP UART CLI. |
| `01/16` | `fld_cfg` | Routed to EXP UART CLI. |
| `01/17` | `fld_ctl` | Routed to EXP UART CLI. |
| `01/18` | `fld_stat` | Routed to EXP UART CLI. |
| `01/19` | `rba_cfg` | **Not routed**; reserved. |
| `01/1A` | `rba_ctl` | Routed to EXP UART CLI. |
| `01/1B` | `rba_stat` | Routed to EXP UART CLI. |
| `01/20` | `lev_calib` | **Not routed**. |
| `01/21` | `lev_cfg` | **Not routed**. |
| `01/22` | `lev_ctl` | **Not routed**. |
| `01/23` | `lev_stat` | **Not routed**. |
| `02/00` | `pc_stat` | Routed to EXP UART CLI. |
| `02/01` | `pc_cfg` | Routed to EXP UART CLI; shares fluidic configuration handler. |
| `02/02` | `pc_ctl` | Routed to EXP UART CLI. |
| `02/03` | `thp_cfg` | Routed to EXP UART CLI. |
| `02/04` | `thp_ctl` | Routed to EXP UART CLI. |
| `02/05` | `mon_cfg` | **Not routed**. |
| `02/06` | `mon_en` | **Not routed**. |
| `02/07` | `rsv_op1` | **Not routed**. |
| `02/08` | `rsv_op2` | **Not routed**. |
| `02/09` | `rsv_op3` | **Not routed**. |
| `03/00` | `dat_lst` | Routed; list counts and establish a file snapshot. |
| `03/01` | `dat_sel` | Routed; select file and return size, CRC32, chunk count. |
| `03/02` | `dat_get` | Routed; return selected file chunk. |
| `03/03` | `dat_des` | Routed; release selection. |
| `03/04` | `dat_del` | Routed; current request uses a U32 index (five bytes). |
| `03/05` | `dat_mrk` | Routed as `dat_mark`; marks selected file. |
| `04/00` | `tm_get` | Routed; 29-byte legacy telemetry, requires fresh OBC snapshot. |
| `04/01` | `tm_cfg` | **Not routed**; explicit unknown-command reply. |

The implementation additionally accepts `02/0A exp_ping`, `02/0B exp_tlm`, `02/0C exp_run`, `04/02 tm_obc_get`, and debug extensions `FF/00` and `FF/01`; these are **outside** the revision 1.0 PDF register. `FF/00` needs the exact handoff token and stops flight software; `FF/01` executes a shell command. A `raw` console command can send any address, but cannot make an unrouted ICD command work.

## How host software controls the system

### External host over RS-422

Connect a host serial adapter to the OBC RS-422 link at **921600 baud, 8N1, no flow control**. The host sends binary TC frames, not command text: `A5 TYPE ADDR LEN_LO LEN_HI PAYLOAD CRC_LO CRC_HI`. CRC-16/CCITT uses polynomial `0x1021`, initial `0xFFFF`, and covers **payload only**; empty-payload CRC is `FF FF`. A normal TM echoes TYPE/ADDR, while an error returns `ACK/ERR=FF` and an error code (`00` invalid TC, `01` data, `02` memory, `03` unknown). Debug success uses `ACK/ERR=FE`.

The bundled Python REPL uses `pyserial`:

```sh
python3 -m pip install -r service/dal_obc_fsw/tool/pc_client/requirements.txt
python3 service/dal_obc_fsw/tool/pc_client/bee_dal_repl.py /dev/ttyUSB0
```

At `bee-dal>`, use `ping`, `tm_obc_get`, `pwr_stat OBC get`, `pwr_en OBC AI_power on`, `pdu_vout get`, `exp_tlm sensors`, or `exp_run pc_status`. The REPL also exposes `raw <type> <addr> [bytes...]` for an implemented address without a named REPL command. The flight dispatcher supports PC, thermal, fluidic, and motor ICD commands (`02/00`–`02/04`, `01/13`–`01/18`, `01/1A`–`01/1B`), but this PC REPL has **no named commands** for those families; a host application must encode their current payloads or use `raw` with the command register. `exp_run` sends EXP CLI text through the `02/0C` gateway.

**Host-client gap:** `tool/pc_client/bee_dal_client/protocol.py` still defines `SYS_INFO_FMT = "<BB"`, and its `do_sys_info()` expects two bytes. The flight handler sends 96 bytes, so that REPL command currently reports an unexpected length; the OBC local console or an updated host decoder is needed for `sys_info`. This is a client decoding issue, not a missing flight handler.

### Software running on the OBC

`dal_flight_console` talks to `/run/dal/dal_obc_fsw/console.sock` (mode `0600`, normally root-only). Its one-shot form is suitable for scripts:

```sh
dal_flight_console --json ping
dal_flight_console --json tm_obc_get
dal_flight_console pwr_en OBC AI_power on
dal_flight_console pdu_vout set 5000
dal_flight_console exp_tlm sensors
dal_flight_console exp_run pc_status
dal_flight_console dat_lst
dal_flight_console dat_sel 0 0 1024
dal_flight_console dat_get 0
dal_flight_console dat_des
```

An application can connect to that Unix socket directly, send one newline-terminated command per connection (prefix `json ` for JSON), and read until EOF. For example, `json pwr_stat PDU get\n` returns a structured reply. A successful ICD ACK means the handler replied; verify validity fields, EXP response text, and reported file CRC before treating telemetry or operations as complete. Do not interleave `dat_sel`/`dat_get` with another file transfer on either interface, because the selection is global. `gui_state` and `gui_shell` exist only on the local socket; the external host has no equivalent dedicated ICD address.

## Source and verification notes

The coverage table was checked against `src/services/bee_dal_tmtc_service.c:dispatch()`, `lib/bee_dal_proto/include/bee_dal_proto.h`, and the revision 1.0 register in `docs/icd.md`. Console behavior was checked in `src/services/flight_console_service.c`; host REPL behavior in `tool/pc_client/bee_dal_client/repl.py` and `protocol.py`. This audit is static; run `make check` and `make fsw-sim-test` for host tests, then verify device-dependent paths on the OBC/EXP/PDU hardware.
