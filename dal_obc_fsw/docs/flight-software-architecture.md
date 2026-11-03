# Flight software architecture and ICD implementation

This page describes the current `service/dal_obc_fsw` source, compared with the revision 1.0 command register in `docs/icd.md`. “Implemented” below means that `dispatch()` routes the TYPE/ADDRESS to a handler; it does **not** mean the attached EXP, PDU, camera, or board hardware was verified. The wire contract is the **current source**, which sometimes differs from the printed ICD. See `docs/flight-command-register.md` for payload layouts and console syntax.

## Runtime architecture

```text
External host ── RS-422 binary frames ── /dev/ttyS1 ── decoder ─┐
OBC frame client ── Unix socket ── rs422.sock ── decoder ────────┤
                                                               ├─ serialized dispatch() ── command handlers
OBC application ── Unix socket text ── flight_console_service ─┘         │
                                                                         ├─ OBC power, RTC, USB camera
OBC collector ── JSON ── hk_service.c cache ──────────────────────────────┤
EXP MCU ── CAN/CSP ── exp_csp_service worker ── EXP JSON + queued RPARAM ┤
EXP MCU ── UART CLI ── control and optional GUI JSON ────────────────────┤
PDU ── Modbus RTU ── gui_state poll/cache ────────────────────────────────┤
/home/bee/data ── bee_dal_files.c ────────────────────────────────────────┘
```

`src/main.c` starts the OBC collector, EXP CSP worker, snapshot cache, RS-422 receiver, local console, and GUI state workers. `lib/bee_dal_proto/` decodes/encodes frames and CRCs. `src/services/bee_dal_tmtc_service.c` owns the command dispatcher; both ingress paths take its mutex, so slow device handlers block the next ICD command. Most telemetry handlers listed below copy cached data. `rba_stat` can make one EXP UART request when its cache is unavailable. `flight_console_service.c` parses text into the same TYPE/ADDRESS and payload and formats text or JSON replies. The two ingress paths also share one file selection. `gui_state` is a socket-only snapshot request; `gui_shell` runs in a separate worker.

The local MIPI capture path routes a board and sensor, saves one complete BG10 RAW frame, then uses the image's `raw_png` tool to publish a full-resolution PNG with the same basename. It retains the RAW file and removes the converter's low-resolution intermediate. Both published files are discoverable through the image file index.

The OBC collector samples PAC1934 and reads `temp.service` and `bme.service` every five seconds. It publishes `/run/dal/dal_obc_fsw/obc_data.json` and writes `HK0` CSV under `/home/bee/data/logs/hk`. The snapshot cache rejects data older than 30 seconds. The EXP CSP worker reads node 11, RPARAM tables 2, 3, 6, and 12 once per second when transactions complete within the interval. It also reads identity table 0 once at startup, retries after a failed read, and refreshes it when table 12 reports a new MCU boot ID. It keeps these tables in RAM, publishes table 2 to `/run/dal/dal_obc_fsw/exp_data.json`, and writes `HK1` CSV every five seconds. Queued CSP requests run on that worker, away from the shared dispatcher lock; slow requests or link timeouts can delay a polling cycle. EXP UART commands use `DAL_EXP_UART_PORT` (baud 250000 by default). PDU commands use Modbus on `/dev/ttyS3` by default (`DAL_GUI_PDU_PORT` overrides it).

## Read cache and control path

`DAL_EXP_UART_PORT` defaults to `/dev/ttyS4`; its baud defaults to 250000.
The local console command `port_cfg get|set <KEY> <value>|restart` manages the
flight serial settings in `/etc/default/dal-obc-fsw`. Changes take effect after
restarting `dal_obc_fsw.service`. DALTUI exposes this command on CFG → Ports;
it is deliberately local-console only because changing a link can disconnect
the RS-422 operator. PDU Modbus and EXP CSP RS-485 must use separate devices.

| ICD read | Source and update | Stale or missing sample |
| --- | --- | --- |
| `01/00 sys_info` | PDU identity registers 40100–40109 are polled every 60 s. EXP firmware and hardware versions come from cached RPARAM table 0; build ID comes from cached table 12. OBC identity is local. | EXP is offline until table 0 succeeds and a fresh table 12 sample has the same boot ID. |
| `01/02 pdu_vout get`, `01/03 pwr_en PDU get`, `01/04 pwr_stat PDU`, `01/07 sen_stat PDU` | `gui_state.c` reads PDU Modbus inputs and coils every 5 s and caches one sample. | Read fails or reports unavailable after 15 s. `pwr_stat` retains per-source validity flags. |
| `01/03 pwr_en EXP get`, `01/04 pwr_stat EXP` rail bits | CAN/CSP RPARAM table 2 `rail_enabled[3]`, polled about once per second. PAC measurements also use the table 2 snapshot. | Rail bits are unavailable when the latest poll failed or the sample is older than 3 s. |
| `01/15 thm_stat`, `01/18 fld_stat`, `02/00 pc_stat` | CAN/CSP RPARAM table 12 supplies these compact status groups. | Returns `FF/03` if the group is missing or stale. |
| `01/1B rba_stat` | Uses fresh table 12 motor status when available; otherwise reads `rba_wire_stat` over EXP UART for this request. | Returns `FF/03` if the UART request fails. |

The EXP firmware also publishes read-only RPARAM table 3 (`Temperature control`) once per second. The FSW worker reads it over CSP CAN or CSP RS-485, then uses `param_show 3` over EXP UART when both CSP transports fail. `exp_csp_snapshot 3` (console or RS-422 `02/0F` payload `03`) returns all four profile states, faults, NTC measurements, setpoints, requested/applied duty, limits and sample ages from the in-memory cache. A failed poll or sample older than three seconds yields `fresh=false`; the last data stays available for diagnosis. DALTUI shows stale status as unavailable. Automatic status updates no longer send `thp_ctl ... status` over UART. Start/stop/resume commands still use `thp_ctl` and must be confirmed by a subsequent table 3 sample.

The EXP firmware publishes read-only RPARAM table 12 (`ICD compact status`) with the exact 6/16/16/1-byte THM/FLD/RBA/PC status payloads, group validity/timestamps, and build ID. The CSP worker reads `getall 11 12` alongside tables 2 and 6, validates schema 6 and layout 1, and caches the full result. A group is usable only when the latest poll succeeded, the received sequence is no older than three seconds, its validity bit is set, and its MCU sample age is at most three seconds. `rba_stat` reads `rba_wire_stat` over EXP UART once when this cache is unavailable; other status commands return an error for a missing group with CSP enabled. With `DAL_EXP_CSP_ENABLE=0`, the legacy cached UART path remains available for those other commands, but `sys_info` has no EXP identity. No background UART `version` command runs. `DAL_EXP_UART_POLL=1` separately enables GUI JSON polling.

`pdu_vout set` and `pwr_en PDU` still send Modbus writes and read back the hardware state; the PDU cache is refreshed immediately after the write. EXP control commands still use the existing UART path, and the table 12 cache catches up on its next CSP cycle. An ACK for a control command is not a substitute for checking its returned state and a subsequent status sample. Device I/O is serialized by the corresponding worker or I/O mutex.

## Revision 1.0 ICD coverage

The comparison covers **all 43 command rows** in `docs/icd.md` / the printed PDF: **30 routed, 13 not routed**. “Routed” can still produce a data or unknown error if a device is unavailable or a payload is invalid. No `0x05` SFU command is defined as an individual row in the PDF register; the dispatcher rejects the entire family.

| TYPE/ADDR | ICD command | Current implementation |
| --- | --- | --- |
| `00/00` | `ping` | Routed; U32 uptime reply. |
| `01/00` | `sys_info` | Routed for zero-length read from cached identity; **one-byte self-test request is not implemented**. Current reply is 96 bytes. |
| `01/01` | `sys_rtc_cfg` | Routed; read or set Linux clock. |
| `01/02` | `pwr_adj_cfg` | Routed as PDU VOUT get/set; current set payload is U16 mV, not the PDF's opaque 8 bytes. |
| `01/03` | `pwr_en` | Routed; OBC, EXP, and PDU rails. |
| `01/04` | `pwr_stat` | Routed; current request is board U8 plus source mask U16. |
| `01/05` | `sen_bme_cfg` | **Not routed** at this address. EXP BME configuration is available via `02/0C` text gateway. |
| `01/06` | `sen_imu_cfg` | **Not routed** at this address. EXP IMU configuration is available via `02/0C`. |
| `01/07` | `sen_stat` | Routed; current four-byte selector and paged records differ from PDF. |
| `01/10` | `uvc_cfg` | Routed; per-camera USB profile, including CAMERA_LED PWM duty, via external `usb_cam` tool. |
| `01/11` | `uvc_ctl` | Routed; apply the saved PWM duty, queue a JPEG capture, then turn the LED off. |
| `01/12` | `uvc_stat` | Routed; poll capture state. |
| `01/13` | `thm_cfg` | Routed to EXP UART CLI. |
| `01/14` | `thm_ctl` | Routed to EXP UART CLI. |
| `01/15` | `thm_stat` | Routed to cached EXP table 12 status. |
| `01/16` | `fld_cfg` | Routed to EXP UART CLI. |
| `01/17` | `fld_ctl` | Routed to EXP UART CLI. |
| `01/18` | `fld_stat` | Routed to cached EXP table 12 status. |
| `01/19` | `rba_cfg` | **Not routed**; reserved. |
| `01/1A` | `rba_ctl` | Routed to EXP UART CLI. |
| `01/1B` | `rba_stat` | Routed to cached EXP table 12 status. |
| `01/20` | `lev_calib` | **Not routed**. |
| `01/21` | `lev_cfg` | **Not routed**. |
| `01/22` | `lev_ctl` | **Not routed**. |
| `01/23` | `lev_stat` | **Not routed**. |
| `02/00` | `pc_stat` | Routed to cached EXP table 12 status. |
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

The implementation additionally accepts `02/0A exp_ping`, `02/0B exp_tlm`, `02/0C exp_run`, `02/0D exp_csp_submit`, `02/0E exp_csp_result`, `02/0F exp_csp_snapshot`, `04/02 tm_obc_get`, and debug extensions `FF/00` and `FF/01`; these are **outside** the revision 1.0 PDF register. `FF/00` needs the exact handoff token and stops flight software; `FF/01` executes a shell command. A `raw` console command can send any address, but cannot make an unrouted ICD command work.

## How host software controls the system

### EXP RPARAM over CAN/CSP

The flight process owns periodic EXP CSP traffic. `02/0D` queues a printable ASCII payload such as `get 2 bme_mC`, `set 5 setpoint_mC 25000 25000 25000 25000`, `run pc_status`, or `refresh 0`. Its response is a little-endian U32 ticket. `02/0E` takes that U32 ticket and returns U8 state (`0` queued, `1` running, `2` done, `3` failed), little-endian U16 text length, then command output. Results are retained in a 16-entry ring; fetch them promptly. `refresh 0` rereads EXP identity table 0 and replaces its cache. `set` is limited to configuration table 5. Table 2 sensor values are read-only. Check the result state and text after every write; queue acceptance is not device acknowledgment. `lite-client` uses `can0` by default; `DAL_EXP_CSP_IF`, `DAL_EXP_CSP_NODE`, and `DAL_EXP_CSP_CLIENT` override the interface, node (default 11), and binary path. The worker accepts both sensor schema 5 and schema 6.

### CSP transport failover

The worker starts on CAN. After two failed ping or table 2 poll cycles it probes RS-485 every five seconds and switches only if both ping and table 2 succeed. While on RS-485 it probes CAN every ten seconds; a successful ping and table 2 read restore CAN. A failed RS-485 poll triggers an immediate CAN probe. The same worker serializes polling and queued commands, so a command already sent is never automatically replayed on the other link. Check its ticket result before deciding whether to retry a write. A transport change does not invalidate cached telemetry by itself; poll failures and the three-second freshness limit still govern replies.

The fallback uses `lite-client -a2 -k/dev/ttyS2,speed=115200` to reach EXP node 11 over CSP/KISS. The MCU uses address 11 on both interfaces and routes replies for OBC source 2 to UART4 RS-485; source 1 remains on CAN. The Yocto board maps the RS-485 adapter to `/dev/ttyS2` and controls direction through its device tree. Configure `DAL_EXP_CSP_RS485_ENABLE=0` to disable fallback; `DAL_EXP_CSP_RS485_PORT`, `DAL_EXP_CSP_RS485_BAUD`, and `DAL_EXP_CSP_RS485_ADDRESS` override the defaults. Do not share this port with PDU Modbus or another process. The serial link requires compatible MCU firmware and fitted wiring; the host test covers protocol selection and recovery, not physical bus timing.

If neither CSP link supplies a valid table 2 sample, the worker tries the MCU UART0 CLI command `param_show 2` on `DAL_EXP_UART_PORT`. A complete, validated reply selects a read-only UART fallback; `param_show 0`, `3`, `6`, and `12` then feed the same identity, heater status, inventory, and ICD caches. A missing, partial, or invalid table 2 reply publishes `online=0` and `data_valid=0` in the EXP snapshot, so `sys_info` reports EXP offline and cached status commands reject stale data. Queued CSP control commands fail explicitly while UART fallback is active; they are never translated into UART writes. The worker continues probing CAN every ten seconds and RS-485 during those probes, returning to a CSP link after a successful ping and table 2 read. `param_show` is slower than CSP and can delay a polling cycle; its output is validated against the expected table schema before use.

On the OBC, use `dal_flight_console exp_csp_get 2 bme_mC`, `dal_flight_console exp_csp_set 5 setpoint_mC 25000 25000 25000 25000`, `dal_flight_console exp_csp_refresh 0`, and `dal_flight_console exp_csp_result <ticket>`. `exp_csp_run` submits an EXP shell command through CAN. Polling and requests share one worker, so a slow CSP transaction delays the next EXP refresh but does not hold the RS-422 dispatcher. The separate `hk_service.service` is disabled to avoid a second CAN client.

`dal_flight_console exp_csp_snapshot 0`, `2`, `3`, `6`, or `12` reads the latest in-memory data with no CAN transaction. RS-422 TYPE `02`, ADDR `0F` accepts one byte (`0`, `2`, `3`, `6`, or `12`) and returns the same UTF-8 JSON. The envelope has `table`, `fresh`, `age_ms`, and `data`; `data` is null until the first successful read. Table 0 `fresh` means its cached boot ID matches a fresh table 12 sample; it does not expire merely with time. For tables 2, 3, 6, and 12, a failed poll leaves the last good data with `fresh=false`; freshness requires the latest poll to succeed and the sample to be no older than three seconds. Check table 12 `valid_mask` and `group_tick_ms` before interpreting a group; its envelope freshness alone does not qualify each group. Table 6 includes inventory counters and metadata for the oldest pending object; it is not a complete object list.

### External host over RS-422

Connect a host serial adapter to the OBC RS-422 link at **921600 baud, 8N1, no flow control**. The host sends binary TC frames, not command text: `A5 TYPE ADDR LEN_LO LEN_HI PAYLOAD CRC_LO CRC_HI`. CRC-16/CCITT uses polynomial `0x1021`, initial `0xFFFF`, and covers **payload only**; empty-payload CRC is `FF FF`. A normal TM echoes TYPE/ADDR, while an error returns `ACK/ERR=FF` and an error code (`00` invalid TC, `01` data, `02` memory, `03` unknown). Debug success uses `ACK/ERR=FE`.

The bundled Python REPL uses `pyserial`:

```sh
python3 -m pip install -r service/dal_obc_fsw/tool/pc_client/requirements.txt
python3 service/dal_obc_fsw/tool/pc_client/bee_dal_repl.py /dev/ttyUSB0
```

At `bee-dal>`, use `ping`, `tm_obc_get`, `pwr_stat OBC get`, `pwr_en OBC AI_power on`, `pdu_vout get`, `exp_tlm sensors`, or `exp_run pc_status`. The REPL also exposes `raw <type> <addr> [bytes...]` for an implemented address without a named REPL command. The flight dispatcher supports PC, thermal, fluidic, and motor ICD commands (`02/00`–`02/04`, `01/13`–`01/18`, `01/1A`–`01/1B`), but this PC REPL has **no named commands** for those families; a host application must encode their current payloads or use `raw` with the command register. `exp_run` sends EXP CLI text through the `02/0C` gateway.

**Host-client gap:** `tool/pc_client/bee_dal_client/protocol.py` still defines `SYS_INFO_FMT = "<BB"`, and its `do_sys_info()` expects two bytes. The flight handler sends 96 bytes, so that REPL command currently reports an unexpected length; the OBC local console or an updated host decoder is needed for `sys_info`. This is a client decoding issue, not a missing flight handler.

### Binary frame socket on the OBC

Set `DAL_OBC_RS422_SOCKET=/run/dal/dal_obc_fsw/rs422.sock` in `/etc/default/dal-obc-fsw` and restart `dal_obc_fsw.service`. The socket is disabled when the variable is unset. It is a Unix stream socket with mode `0600`, so the board client must run as root. Send the same `A5 TYPE ADDR LEN_LO LEN_HI PAYLOAD CRC_LO CRC_HI` bytes used on `/dev/ttyS1`; read one complete TM frame in the same format. There is no text command or extra socket header. A connection can send multiple frames. Invalid CRC frames are discarded, and an incomplete frame is reset after the inter-byte timeout. The real serial port remains active.

Each input has its own decoder. Completed frames from the serial port, frame socket, and local text console share the dispatcher mutex; each TM goes back to its originating transport. The socket currently serves one connected client at a time. DALTUI can use this endpoint with `--rs422 /run/dal/dal_obc_fsw/rs422.sock`; see the [board run guide](daltui-running.md#use-the-rs-422-frame-socket-on-the-same-obc).

For verbose debug without routine housekeeping frame dumps, start flight
software with `-v --quiet-housekeeping`. This filters `tm_get`, `tm_obc_get`,
`pwr_stat`, `sen_stat`, and `exp_tlm` trace lines on both binary transports;
the requests and replies still work. Error replies and other commands remain
visible. The deploy installer sets `DAL_OBC_VERBOSE=1` and
`DAL_OBC_QUIET_HOUSEKEEPING=1` in `/etc/default/dal-obc-fsw` while preserving
other settings. Read the live log with
`journalctl -u dal_obc_fsw.service -f -o cat`.

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
