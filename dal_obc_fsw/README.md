# DAL OBC flight software

This is the editable Makefile project for the `dal-obc-fsw` Yocto recipe. It
combines the RS422 ICD endpoint and its telemetry cache in one process. The
separate `temp_service` and `bme_service` sample OBC sensors. Flight software
collects those readings and PAC1934 power data, publishes the OBC snapshot,
and writes the `HK0` CSV. Its CSP worker reads EXP RPARAM tables 2, 3, 6,
and 12 each second, caches them in memory, writes the EXP sensor snapshot and
`HK1` CSV, and serves `exp_csp_snapshot 0|2|3|6|12` from RAM. The OBC cache polls its own snapshot
every second, validates it, and copies it into mutex-protected memory.
RS422 telemetry handlers read that cache; they never wait for sensor I/O or
file parsing.

## OBC housekeeping

To expose the binary RS-422 command path to a root-only board client, set
`DAL_OBC_RS422_SOCKET=/run/dal/dal_obc_fsw/rs422.sock` in
`/etc/default/dal-obc-fsw` and restart the service. The socket accepts the
same `A5…CRC16` frames as `/dev/ttyS1`; replies return on the same connection.
It is disabled by default, and the physical RS-422 port continues to run.
See [the architecture guide](docs/flight-software-architecture.md) for framing
and dispatch details. `daltui` can use the local console, the optional local
frame socket, or an external RS-422 serial device.

The data producers cover:

| Source | Linux interface | Values |
| --- | --- | --- |
| TC1047A and four NTCs | SAMA7 IIO ADC | Board and NTC1–4 temperature |
| BME688 U9 | `/dev/i2c-0`, address `0x76` | Temperature, pressure, humidity, gas resistance when valid |
| PAC1934 U6 | IIO device at I2C `0-001f` | 5V_SAMA7, 5V_FPGA, 3V3, 5V_AI voltage/current |
| PAC1934 U2 | IIO device at I2C `0-0010` | PMIC_3V3, PMIC_DDR_1V35, PMIC_CORE_1V15, PMIC_CPU_1V15 voltage/current |

Flight software publishes `/run/dal/dal_obc_fsw/obc_data.json` by atomic rename.
Its default OBC collection interval is five seconds. The flight software
accepts schema version 1 snapshots with `board="obc"`, `online=1`, and an
`obc_timestamp` no older than 30 seconds. A transient read failure retains
the last complete snapshot until it becomes stale. Before the first valid
snapshot, or after staleness, the RS422 telemetry commands return the ICD
data-error response. `tm_get` now returns the ICD v1.2 57-byte payload (64-byte TM, CRC16 at offset 62), using cached PDU/EXP/OBC data. Invalid I16/U16 fields use 0x8000/0xFFFF. MLX average is unavailable until a verified full-frame average is published; no synthetic reading is generated. See docs/icd-v1p2-console-mapping.md for units and field mappings.

`dal` and `tm_obc_get` (04/02) retain the separate OBC housekeeping snapshot interface.

### OBC housekeeping wire table (`0x04/0x02`)

The TC has `LENGTH=0` and CRC `0xFFFF`. The TM ACK echoes TYPE and ADDRESS
and has a **64-byte little-endian payload**:

| Offset | Type | Field | Units |
| ---: | --- | --- | --- |
| 0 | U16 | Valid mask | Bit 0 onboard ADC; bits 1–4 NTC1–4; bit 5 BME T/P/H; bit 6 gas; bit 7 PAC U6; bit 8 PAC U2 |
| 2 | U32 | OBC snapshot time | Unix seconds |
| 6 | U32 | Boot count | boots |
| 10 | I16 | Onboard temperature | °C × 100 |
| 12 | I16[4] | NTC1–4 temperature | °C × 100 |
| 20 | I16 | BME temperature | °C × 100 |
| 22 | U32 | BME pressure | Pa |
| 26 | U16 | BME humidity | %RH × 100 |
| 28 | U32 | BME gas resistance | Ω |
| 32 | U16[8] | U6 channels 1–4, then U2 channels 1–4 voltage | mV |
| 48 | I16[8] | U6 channels 1–4, then U2 channels 1–4 current | mA |

An invalid channel has its validity bit clear and its value set to zero.
NTCs below −40 °C are treated as unpopulated. Values outside the integer
range are saturated. The PAC rail names and order are listed above; no rail
name strings are transmitted. This new entry needs to be added to the revised
ICD command table before the MCU client relies on it.

The deploy installer starts `temp`, `bme`, and `dal_obc_fsw`; it stops and
disables `hk_service` and the unused standalone `rs422` unit. Flight software
reads the PAC1934 IIO devices and owns both housekeeping CSV streams. It
reads GPIO, RTC, MIPI and PWM status for the GUI, and polls PDU Modbus
telemetry over PC/104 RS232_1 (`/dev/ttyS3`, 115200 baud) every five seconds.
Set `DAL_GUI_PDU_PORT` to use another connected port. PC/104 RS-485
`/dev/ttyS2` is available for PDU only when EXP CSP RS-485 fallback is
disabled or assigned a different port.
The CSP worker reads EXP firmware and hardware versions from RPARAM table 0 at
startup and after an MCU reboot. Compact thermal, fluidic, motor, and experiment
status comes from table 12. Use `exp_csp_refresh 0` to reread identity on demand.
With `DAL_EXP_CSP_ENABLE=0`, compact status uses the legacy UART cache and EXP
identity in `sys_info` is unavailable. Set `DAL_EXP_UART_POLL=1` in
`/etc/default/dal-obc-fsw` to add
periodic `tlm_json system`, `sensors`, and `power` reads for the GUI cache.
Flight software reassembles `json_chunk` responses before publishing that
cache in `gui_state`. `exp_run`, `exp_ping`, and `exp_tlm` still make on-demand
UART requests. `DAL_EXP_UART_PORT` defaults to `/dev/ttyS4` and baud defaults
to 250000. The CSP worker owns RPARAM reads and
queued writes. It uses CAN (`can0`) first; after two failed poll cycles it
probes CSP/KISS over RS-485 (`/dev/ttyS2`, 115200 8N1, CSP source address 2).
It switches only after a ping and table 2 read succeed, and probes CAN every
ten seconds to return to it. A queued write uses the current link once; check
its ticket result before retrying. Set `DAL_EXP_CSP_RS485_ENABLE=0` to disable
fallback, or configure `DAL_EXP_CSP_RS485_PORT`, `DAL_EXP_CSP_RS485_BAUD`, and
`DAL_EXP_CSP_RS485_ADDRESS` for a different connection. Reserve that serial
port for EXP CSP; do not also assign it to `DAL_GUI_PDU_PORT`. The EXP MCU must
run firmware with its UART4 CSP/RS-485 interface enabled. If both CSP links
fail, the worker reads RPARAM tables 0, 2, 3, 6, and 12 through the MCU UART0
`param_show` command on `DAL_EXP_UART_PORT`. This last path is read-only;
queued CSP control commands fail while it is active. If table 2 cannot be
read and validated there, the EXP snapshot reports `online=0`. CAN and RS-485
are probed again for recovery. See
[architecture](../../docs/flight-software-architecture.md#read-cache-and-control-path)
for cache freshness and source mapping.

The local flight console supports `port_cfg get`, `port_cfg set <KEY> <value>`,
and `port_cfg restart` for the flight serial settings. It saves validated
values atomically in `/etc/default/dal-obc-fsw`; restart the service to use
them. DALTUI exposes these controls under **CFG → Flight software serial ports**.
Its ICD file-list command includes the `HK0_YYYYMMDD.csv` and
`HK1_YYYYMMDD.csv` files in `/home/bee/data/logs/hk/` as log files. The daily CSV that is still being appended can
change during a chunk transfer; the host should verify the returned CRC and
retry if it changes. Deletion of the current UTC day's CSV files is rejected
because flight software may still be writing them.

### Automatic EXP data collection

The EXP data worker watches the fresh RPARAM table-6 inventory. When an object
appears, it verifies its descriptor with `data_ready_collect <id>`, downloads
32-byte chunks with `data_read <boot> <id> <offset> <length>` over the EXP UART,
checks length and CRC-32, and saves the file before sending
`data_ready_done <boot> <id> <length> <crc32>`. Failed transfers remain on the
MCU and retry; the worker never discards an object. Camera frames go to
`images/exp/EXP_Cn_Bxxxxxxxx_Oxxxxxxxx_En.bin`, PC logs to `logs/pc/`, and
LWL data to `logs/lwl/` under the data root. `dat_lst` and `dat_get` expose
the saved EXP camera files over RS-422.

The board default root is `/home/bee/data`. Set `DAL_EXP_DATA_DIR` in
`/etc/default/dal-obc-fsw` to change it; set `DAL_DATA_DIR` to the same
absolute path so the ICD file service can list the downloads. Use
`DAL_EXP_DATA_ENABLE=0` to disable collection. Table 6 must be available over
CAN, RS-485, or the UART `param_show` fallback. The data commands use
`DAL_EXP_UART_PORT` (default `/dev/ttyS4`).
The `exp_csp_snapshot 6` response includes `download` with the latest saved
boot ID, object ID, source, and relative filename; it is `null` until a file
has been saved by this flight software process.

For host simulation, `python3 tools/fsw_rig.py` from `service/daltui` prints
its temporary `EXP_DATA` path. Use `--exp-data-dir /tmp/dal-exp-sim` to keep
downloads after the rig stops.

## Build and install

For debug sessions, run `dal_obc_fsw -v --quiet-housekeeping` to hide verbose
TC/TM dumps for routine `tm_get`, `tm_obc_get`, `pwr_stat`, `sen_stat`, and
`exp_tlm` reads. With `-v`, local console requests and responses are logged as
`[CONSOLE] RX` and `[CONSOLE] TX`; replies longer than 4096 bytes show a preview
and their full byte count. Quiet mode also hides successful routine console
reads and `gui_state`, while errors and control commands remain visible. The
commands still run and their housekeeping snapshots/CSV
are still written. Control commands and error replies remain visible. The
deploy package includes `dal-obc-fsw.env`; `make install` sets
`DAL_OBC_VERBOSE=1` and `DAL_OBC_QUIET_HOUSEKEEPING=1` in
`/etc/default/dal-obc-fsw` while preserving other port and path settings.
The installer restarts the service. View the live log with
`journalctl -u dal_obc_fsw.service -f -o cat`. Set either variable to `0`
after installation to change the logging mode, then restart the service.
A later `make install` restores both values to `1`.
See the [board log guide](docs/flight-software-logs.md) for live, recent, and
filtered `journalctl` commands.

```sh
make -C service/dal_obc_fsw check
make -C service/dal_obc_fsw target
make -C service/dal_obc_fsw deploy
```

The default ARM SDK is `/home/buildmachine/oecore-obcv110`; override it with
`SDK_ROOT=/path/to/sdk`. `deploy` stages the binary and systemd unit under
`service/deploy/dal_obc_fsw/`. This includes `dal_flight_console`. Run
`make -C service deploy` to stage it with
the sensor services, `dal`, and the ARM `daltui` binary. The DALTUI cross-build
uses `/home/buildmachine/oecore-obcv110` by default; override it with
`DALTUI_SDK_DIR=/path/to/sdk`. Copy the complete `service/deploy/`
directory to `/home/root/deploy/` on the board:

```sh
make -C /home/root/deploy install
make -C /home/root/deploy status
dal get obc_sensor all
dal_flight_console
/home/root/tools/bin/daltui --console
```

To use the local RS-422 frame socket instead, enable `DAL_OBC_RS422_SOCKET`
and run DALTUI with `--rs422`; see the
[DALTUI board guide](docs/daltui-running.md#use-the-rs-422-frame-socket-on-the-same-obc).

To refresh the Yocto source zip after editing the project, run
`make -C service package-dal-obc-fsw`, then build `dal-obc-fsw` or the image.
The Yocto recipe installs the updated binary but leaves its systemd unit
disabled by default; the deploy installer enables it on a running board.

The host tests check compilation, the OBC wire payload, and snapshot parsing
and age handling. The ARM SDK and BitBake builds verify cross compilation.
Live sensor values, RS422 timing, and file transfer still require a board test.

## Local flight console on the OBC

The flight software runs a Unix socket listener thread at
`/run/dal/dal_obc_fsw/console.sock`. The socket is accessible to root. The
`dal_flight_console` client opens an interactive prompt or runs one command:

```sh
dal_flight_console
dal_flight_console tm_obc_get
dal_flight_console --json tm_obc_get
dal_flight_console shell ls -l /home/root
```

At the prompt, Up/Down browse command history, Left/Right move the cursor,
and Backspace or Delete edit the line. Type `help` for the complete command
list, `json on` or `json off` to switch format, and `exit` to quit. The commands are `ping`,
`sys_info`, `sys_rtc [epoch|now]`, `pdu_vout get|set <mV>`,
`pwr_en <OBC|EXP|PDU> get|<output> on|off [force for EXP off]`,
`pwr_stat <OBC|EXP|PDU> get [source|all|mask <mask>]`, `tm_get`,
`tm_obc_get`, `dat_lst <mark>`,
`dat_sel <type> <index> <chunk_size>`, `dat_get <chunk>`, `dat_des`,
`dat_del <type> <index>`, `dat_mark <mark>`, `shell <command>`, and
`raw <type> <address> [byte ...]`. `debug_handoff yes` invokes the UART
debug handoff and stops the flight software. Numeric arguments accept decimal
and `0x` hexadecimal. `raw` exposes any ICD command, including commands
that currently return an unsupported-command error.

`sys_rtc` without an argument reads the Linux system epoch (TYPE/ADDR
`01/01`, empty TC payload, four-byte U32 LE TM payload). `sys_rtc <epoch>`
sets the Linux clock and attempts to set `/dev/rtc2`; `sys_rtc now` uses the
OBC's current clock, so it does not correct a clock that is already wrong.
The separate PC RS-422 client treats `sys_rtc now` as the PC's current time.
For host sandbox tests, `DAL_OBC_RTC_SIMULATE=1` keeps a process-local RTC
offset instead of setting the Linux clock or `/dev/rtc2`. The daltui rig enables
this flag; the board service leaves it unset.

`pdu_vout get` reads cached PDU VOUT setpoint, voltage, current, and status;
`pdu_vout set <mV>` writes a target of 800–20000 mV to the PDU holding
register `40001`. Both map to ICD `01/02` (`pwr_adj_cfg`). The set TC carries
one U16 LE millivolt value plus six reserved zero bytes (eight payload bytes); the TM carries four U16 LE
fields (eight bytes). PDU firmware powers TRANS before accessing its TPS55287
regulator if TRANS was off. This command does not send a separate VOUT enable
request. The [ICD mapping](../../docs/icd-console-mapping.md#pwr_adj_cfg-0x02)
defines every field and the error behavior.

Native ICD `pwr_en` at `01/03` uses an empty GET or a two-byte masked SET and returns two bytes. See the [v1.2 payload definitions](docs/icd-v1p2-payload-contracts.md) for bit assignments and all native commands.

The board-specific power extension uses `01/32`, with a one-byte board ID:
`03` OBC, `04` EXP, or `05` PDU. For example, run `pwr_en OBC AI_power off`,
`pwr_en EXP MOTOR on`, or `pwr_en PDU VOUT on`. `pwr_en <board> get` reads
all control states on that board. For several outputs at once, use
`pwr_en <board> set <select_mask> <on_mask>`. Only EXP accepts `force`,
and only for off requests. See the [power command payload and bit tables](../../docs/icd-console-mapping.md#unified-power-enable-command-current-flight-software).

`sys_info` reads the background PDU Modbus identity and EXP RPARAM table 0/12
caches. PDU and OBC hardware revisions are configured in
`include/dal_obc_config.h`; EXP hardware revision comes from table 0. Its 96-byte reply contains PDU, EXP, and OBC
records with online status, hardware revision, firmware version, and build ID.
If a board read fails or times out, that board is shown offline with an
unknown firmware version. The wire layout is in
`include/services/sys_info_wire.h` and `../../docs/flight-software.md`.

The GUI uses `gui_state` to receive a JSON response containing OBC, EXP, and
PDU telemetry plus GPIO, RTC, MIPI route, and CAMERA_LED status. Its
`gui_shell <seconds> <command>` requests execute board tools in flight
software and return JSON with `ok`, `exit`, `timeout`, and `text`. Long GUI
commands run in separate socket workers so telemetry requests remain
responsive. `dal_obc_fsw --gui-hw` implements MIPI power/route/capture and
CAMERA_LED control and writes the named power GPIOs through their `gpio-leds`
brightness entries. The FPGA power command shares the FPGA programming lock.
MIPI capture retains the full BG10 RAW frame and converts it to a matching
full-resolution PNG under `/home/bee/data/images/mipi/`; conversion failure
leaves the RAW file available for diagnosis.

Text telemetry shows decoded sensor values and the exact response payload in
hex. JSON replies include `ok`, the echoed command, ICD type and address,
acknowledgement/error fields, readable text, and `payload_hex`. Shell replies
show the exit status and captured stdout/stderr. Each shell command runs in a
new `/bin/sh` process, so `cd` does not persist between commands. RS422 and
the local console serialize calls through the same dispatcher and share the
same file selection state; finish a file transfer before starting one on the
other interface.

## Test the RS422 command interface from a PC

Connect a USB–RS422 adapter to the OBC RS422 lines. `dal_obc_fsw` owns
`/dev/ttyS1` on the OBC; run the client on the other end of the link. It
uses 921600 baud, 8N1, and no flow control. Install the host dependency from
`tool/pc_client/requirements.txt`, then run:

```sh
python3 service/dal_obc_fsw/tool/pc_client/bee_dal_repl.py /dev/ttyUSB0
```

At the `bee-dal>` prompt:

```text
ping                         # ICD ping and uptime
tm_obc_get                   # full OBC HK snapshot, TYPE 0x04 / ADDR 0x02
tm_get                       # ICD v1.2 57-byte payload telemetry
exp_tlm sensors              # EXP UART schema-2 sensor JSON, TYPE 0x02 / ADDR 0x0B
exp_run motor5_status all    # EXP UART CLI command, TYPE 0x02 / ADDR 0x0C
shell uptime                 # debug extension: execute text on the OBC
shell dal get obc_sensor all # return the local DAL JSON result
raw 0x04 0x02                # send the OBC HK request without decoding fields
```

The `shell` command is the firmware's `0xFF/0x01` debug extension; ordinary
text bytes typed into a serial terminal are not ICD frames. The shell reply
reports exit status and the command's combined stdout/stderr.

### EXP software reset from DALTUI

EXP > Overview > Reset EXP MCU sends `exp_run reset` through the existing EXP
UART gateway. The command is transmitted once, with UART output drained before
closing; FSW returns `EXP reset command sent; reboot not confirmed` because the
MCU resets without returning its normal CLI prompt. This confirms transmission,
not a completed reboot. Check EXP status after startup. No automatic reset retry
is performed. This requires the updated FSW binary as well as updated DALTUI;
the existing EXP firmware `reset` handler is used unchanged.
