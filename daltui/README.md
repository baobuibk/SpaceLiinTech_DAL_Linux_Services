# daltui

Terminal UI for the three DAL subsystems: **OBC**, **EXP** and **PDU**.
Layout and look follow `sltui` (blue header, tab bar, status box, footer hints).
Built on ratatui/crossterm, so it runs over SSH or a serial console.

**Data sources** (switch on the CFG tab or with a flag):

| Source | Flag | What it does |
| --- | --- | --- |
| Console (default when its socket exists) | `--console [PATH]` | Text commands on the flight console socket (`json <cmd>`), run on the OBC as root. |
| RS-422 | `--rs422 [PATH]` | The same requests as binary `A5…CRC16` frames, for protocol checks. PATH is the flight frame socket (`DAL_OBC_RS422_SOCKET`) or a serial device. |
| Simulator | `--sim` | Built-in model; nothing is sent anywhere. Used when no console socket exists. |

Both live sources fill the same panels. Values the active source cannot provide show `n/a`
(for example CPU/memory, frame counters, EXP sensors, and PDU temperature/mode over RS-422), and
controls with no flight command behind them show `[ n/a ]` and refuse to run.

```bash
cargo build --release          # add --offline to use the local cargo cache
./target/release/daltui        # interactive (console if its socket exists, else simulator)
./target/release/daltui --rs422 /run/dal/dal_obc_fsw/rs422.sock
./target/release/daltui --console --poll   # opt in to periodic housekeeping reads
./target/release/daltui --console --manual # reads only when UPDATE is requested
./target/release/daltui --console /tmp/x/console.sock --rs422 /tmp/x/rs422.sock --compare   # protocol check
./target/release/daltui --dump exp 120x40 enter,j,j,right   # render a tab as text, no tty
```

Live mode defaults to **control-only**. DALTUI sends no startup, periodic,
page-change, or automatic post-action status queries. It sends commands only
when the operator selects a control, including an explicit Read status control.
The HOME UPDATE button is disabled in this mode. Use `--poll` or `--manual` when
housekeeping reads are needed. The flight software's own housekeeping worker
continues independently. Heater controls that require a current ownership
check ask for an explicit Read status first. Displayed rail and actuator states
can be stale until an explicit read or a `--manual`/`--poll` update.

Use a terminal of at least 100x35 (120x40 recommended).

On **CFG**, press `p` to open the flight software port editor. It reads saved
values from `/etc/default/dal-obc-fsw` through the local flight console.
Select a row and press Enter to save a device path or numeric setting, then
select **Restart FSW** to apply it. The page covers ground RS-422, EXP UART
CLI (default `/dev/ttyS4`, 250000 baud), PDU Modbus (`/dev/ttyS3`, unit 1),
and EXP CSP RS-485 (`/dev/ttyS2`, 115200 baud, address 2). PDU and EXP CSP
RS-485 must use separate devices. This page requires **CONSOLE** mode and
the updated flight software; simulator and RS-422 cannot edit service ports.
Restarting briefly interrupts the console and any ground RS-422 connection.

For the board deploy package, run `make -C service deploy` from `~/Linux_DAL`.
This cross-builds DALTUI and stages `service/deploy/daltui/daltui`. Copy the
whole `service/deploy/` directory to `/home/root/deploy/` on the OBC, then run
`make -C /home/root/deploy install` as root. It installs DALTUI at
`/home/root/tools/bin/daltui` and enables and starts `dal_obc_fsw.service`.
Launch the TUI with `TERM=xterm-256color /home/root/tools/bin/daltui --console`.

## Tabs and pages

OBC and EXP tabs have several pages, switched with `<` / `>` (or `,` / `.`). The strip above the panels shows them.

| Tab | Pages |
| --- | --- |
| HOME | **Housekeeping** strip (Update Housekeeping AUTO / MANUAL and the UPDATE button), then three cards (OBC, EXP, PDU) with health badge and key telemetry |
| OBC | **Overview**: system, devices, temperatures, 8 PAC1934 rails; eFuses, editable UTC Set Time, reboot. **Camera**: USB controls and JPEG capture on the left; CAMERA_LED PWM, four USB power switches, and camera selection. MIPI controls and PNG capture on the right; two boards, four sensors each, route and power actions. The original RAW is retained. Capture status and saved paths appear below the controls. |
| EXP | **Overview**: rails, heaters, IR, burn wire, MCU reset. **Pump U11** (single pump): set/read type, detect, init, frequency, voltage, enable. **Pump U15** (four-channel): detect, init, channel, shared frequency, per-channel voltage, enable. **Motors**: MOTOR rail, DRV8910, per-motor mode, max speed, PWM, optical MIN/MAX mapping and polarity; separate MANUAL and OPERATION command panels. **IR LEDs**: output status and limits, select channel, stage duty and PWM limits, queue configuration, read status, on/off. **Burn wire**: runtime and both wire status cards, separate controls per wire, Set config, Read status, and a timed Burn request confirmation. **Crystallization**: PC status and log location, valve/flow/route mapping, profile fields, Test/Replace/Circulation/ground operations, and manual V1–V4/U11 controls. **Heaters**: manual limits/duty, PID profile configuration, auto start/stop/resume and profile status. **IR Camera**: configure either EXP MLX9064x camera, enable its I2C buffer, detect/init/capture, inspect the PSRAM object ID, automatically display the full-frame maximum and its coordinates after capture, and measure maximum or average temperature around a selected center. |
| PDU | Mode, flags, VOUT/+12/+5_TRANS/+5 rails, faults, events; VOUT/TRANS enable, setpoint, mode, fault clear, temperature limit, heartbeat LED, bootloader |
| LOG | Every command, refusal and fault, newest last |

Capture paths follow the flight software: USB `/home/bee/data/images/usb/USBn_NNNNNNNN.jpg`
(MJPG), MIPI `/home/bee/data/images/mipi/MIPIc.s_H_NNNNNN.png` with a matching `.raw`
(5120x3840 BG10) retained alongside it.
Frequency, voltage and speed values are *staged*: they change on screen (yellow `->` shows
the pending value) and are only sent by the matching Set / Jog action, like the firmware CLI.

For each motor, turn on MOTOR power and initialize DRV8910. **STOP MOTOR** holds the current winding phase until the next move, jog, HOME, or a second STOP; **STOP ALL** releases both motors. The hold uses MCU `hold_pct` when configured, otherwise `drive_pct`. Select Motor 0 or 1, choose
**Mode** (MANUAL or OPERATION), then press **Set mode**. The MCU must have both motors stopped
to change mode; the TUI reads back the actual mode. **Read mode** checks it without changing it.
The MANUAL and OPERATION command panels are shown separately; the inactive panel is disabled.
Set **Max speed** (1–1000 st/s), **Duty cycle** (1–80% drive PWM duty), and **PWM frequency** (80/100/200/2000 Hz), then press their Set
actions. Duty cycle uses `motor5_cfg_set <motor> drive_pct <value>` and requires both motors idle. The right-hand **MOTOR MAPPING** box has its own motor selector and shows both maps.
For each motor, choose the PMU or PML optical bank, distinct MIN/MAX input numbers (0/1), and
the HOME endpoint; press **Map limits**. Set the electrical active level for each mapped input
and press **Set active levels**. Use **Read mapping** for each motor to confirm the MCU
assignments and polarity. Check physical switch validity with `exp_run limit_status all` before
pressing **HOME**. Mapping invalidates the position reference. Jog continues until **STOP MOTOR**;
the requested jog speed must not exceed the selected motor's configured maximum. For a finite
relative move, select **Direction**, press Enter on **Speed** and **Move steps** to type values,
then press **Move CW/CCW**. The TUI sends `motor5_move <motor> <signed_steps> <speed>` through
`exp_run`; CCW uses a negative step count. The move stops after the requested steps. Press
**STOP MOTOR** to interrupt it.
In the MCU's default MANUAL mode, **Jog** and finite **Move CW/CCW** do not supervise optical
limits. MANUAL **HOME** still uses its mapped switch. In OPERATION mode, **Move with limits**
supervises the mapped MIN/MAX switches; **HOME** still uses HOME. Read settings to confirm the
mapping before those actions. The built-in simulator models mode selection and finite steps,
but does not simulate physical optical switch transitions.

## EXP heater control

Open **EXP → Heaters**. The status panel shows all four heater profiles (0–3), each mapped to the heater channel with the same number. Use the four **Profile 0–3** Start/Stop actions to control each profile independently; a stale status triggers a fresh cache read before the action. Start requires the EXP 12 V rail to be ON; switch it on from the manual panel before starting. Select **Profile number** in the profile panel to edit that profile; the corresponding heater channel is shown as read-only. Use **Read limits** and **Read profile** to load MCU settings. The manual panel sets duty, duration, maximum duty, slew and maximum ON time. **Apply limits** writes the selected heater limits; **Set manual duty** runs for the entered duration, and **Manual OFF** stops it. A running or errored profile owns its heater: stop the profile before either manual command. DALTUI checks fresh table-3 status and the EXP MCU enforces the same ownership rule.

The right control panel edits the automatic temperature profile: main and secondary NTC channels, target, allowed NTC difference, overtemperature cutoff, sensor timeout, restart policy, duty slew and PID gains. Temperatures are entered in centi-°C (`3000 cC = 30.0 °C`); the equivalent Celsius value is shown next to each cC field. Gains are entered multiplied by 1000 (`1500` = 1.500). Stop a running profile before **Apply profile**. Use **Resume auto** and **Read status** for the selected profile or the four Start/Stop actions above. The status panel shows applied/requested duty, NTC readings, phase, fault and configuration from FSW's cached RPARAM table 3. FSW housekeeping updates table 3 continuously. DALTUI's **Housekeeping AUTO** round reads that cache together with the other telemetry; in **MANUAL** it reads on **UPDATE**. Opening the Heater page does not issue a separate read. A profile button fetches a fresh state if needed, and a control action schedules one confirmation read. After three seconds without a refresh, the last values remain visible in gray with a `stale` age label; values never received show `n/a`. Status reads use ICD `02/0F` with table ID `3` and do not send UART status commands. Manual and auto controls still use ICD `01/13`, `01/14`, `02/03` and `02/04` over either console or RS-422.

Use Enter to edit/select a field; `<` and `>` switch between the two control panels. `,` and `.` change EXP pages.

## Crystallization experiment

Open **EXP → Crystallization**. The left column shows MCU state and configuration controls;
the right column sends PC operation commands. Press `>` to focus PC OPERATION and `<` to
return to PC SETTINGS; `k/j` stays in the focused panel. Press Enter on a numeric field
to type its value, or on a choice to open its picker. Set the U11 pump type on **Pump U11** before
**Activate**. If `pc_ctl activate` returns flight error `0x01`, check the U11 type shown in
the PC status panel. With the SLN/HD rail off, choose the fitted `lowdriver` or
`highdriver` on **Pump U11**, press **Set type**, then retry **Activate**. The MCU may
also reject activation if its MRAM save fails; the FSW `[PC]` log prints that reason.
Map four unique zero-based valve outputs (0–3), set the pulse (1–100 ms), select
flow sensor 0/1, and set the log sampling interval for the next run. Configure a Standby, Test,
Replace, Circulation, or ONGROUND route by choosing the route, case/preset, and four valve
positions, then press **Apply route**. The profile editor has a separate control for each
MCU field. Select Global, Test, Replace, or Circulation, edit the desired fields, then press
**Apply changed fields**. Only changed fields are sent; `*` marks pending edits and `?`
marks fields whose MCU value has not been confirmed. `minflow`
and `volume` use thousandths of a microliter. Global also has an editable ASCII name. Press
Enter on a numeric field to type its value.
**Read MCU profile** refreshes reported settings without discarding unapplied edits.
Test, Replace, and Circulation each have their own pump frequency, pump voltage, and target
volume controls near the top of the panel. The MCU uses target volume to stop a run when
Global Run mode is Volume.

For **Test**, **Replace**, or **Circulation**, the profile includes minimum flow in 0.001 µL/min
units, check enable, grace period after pump start, and `none`, `warn`, or `stop` for low flow.
Test/Replace also allow 1–100 failing samples; Circulation uses one. Each changed field is a
separate `pc_cfg profile` transaction. Failed fields remain marked for retry. Change profiles
only while PC is in STANDBY.

In the current MCU code, `none` and `warn` both keep the run active and still update `last_error`;
`warn` does not emit a separate warning event. `stop` latches ERROR, stops the pump, waits
for settling, then moves valves to Standby.

The MCU waits for the requested valve route to complete and for the flow service to be ready
before enabling the pump. There is no configurable valve-to-pump delay. `flow_delay` (default
500 ms) delays **low-flow checking**, not acquisition: the flow service can sample earlier, and
volume-mode integration starts with the pump. `pump_off_settle` (default 500 ms) is a different
global field: it waits after turning the pump off before changing valves. The log sampling
interval (default 1000 ms) applies only to PCLG recording, not the safety check.

**Read detailed status** asks the MCU for activation, state, error, flow, volume, case, run mode,
remaining time, datalog and auto-resume flags. The one-byte `pc_stat` is still included in routine
housekeeping. Select case Both/A/B and start Test, Replace or Circulation on the right. `pc_ctl`
ACKs Test/Replace/Circulation when the MCU accepts the CLI request, so the TUI follows up with
`pc_status` to check the resulting state. **Stop**, **Standby** and **Clear error** remain
available as recovery commands. Datalog and auto-resume are persisted on the MCU; changing the
sample interval or datalog setting takes effect at the next run.

The **Manual valves** group stages directions A/B for logical V1–V4 and applies the
complete four-valve mask through `fld_ctl valve mask`. Activate PC and use STANDBY
or ONGROUND with the pump off. The **Manual pump U11** group uses the existing
`fld_ctl pump u11` commands. Deactivate PC first because the MCU reserves U11
during PC operation; then turn on SLN/HD, initialize U11, set frequency/voltage,
and start or stop the pump. The fitted U11 driver type is selected on **Pump U11**.
The PC status panel shows the commanded valve positions and U11 output state.

The MCU writes PCLG v3 objects to PSRAM. Flight software downloads ready objects
over UART and stores them under `/home/bee/data/logs/pc/` by default. The panel
shows **not confirmed by FSW** because it does not identify the latest PC log
file. The `log_pc` UART trace is separate from saved PCLG data.

## Keys

`[` `]` / `Tab` / `1-6` switch tab · `,` `.` switch page · `<` `>` switch control panel · `k/j` select within that panel · `Enter`/`Space` edit, toggle or run ·
`u` update housekeeping now (live sources) · `f` inject a fault on the current subsystem
(OBC: NTC1 over-temp, EXP: CAN link loss, PDU: TRANS eFuse fault) · `p` pause the simulation (SIM only) · `q` quit.
Controls marked `[ ! ]` (reboot, resets, bootloader) ask for `Y` first. Burn request uses its own timed CANCEL/FIRE dialog.

Step-by-step procedures (simulation, rig, running on the board, cross-build with `tools/build-board.sh`) are in the MkDocs site: `mkdocs/docs/operation.md` (`cd mkdocs && mkdocs serve`).

## Update Housekeeping: AUTO / MANUAL

On HOME, `k/j` select the row and `h/l` (or `Enter`) switch **AUTO** and **MANUAL**.

* **CONTROL ONLY** (live default): no automatic reads. Only commands selected by the operator are sent; HOME UPDATE is disabled.
* **AUTO** (`--poll`): one round of read-only commands every poll interval (CFG, 2 s by default).
* **MANUAL**: nothing is polled. `UPDATE` (select it and press `Enter`, or press `u` on any tab) sends one round
  (14 commands on the console, 13 on RS-422), waits for all replies, then shows `last HH:MM:SS  N/M replies OK  X ms`
  and writes one line to LOG. The raw TC/TM of each command is in the CFG command history. Use it to test commands
  without the flight log scrolling. Control actions (rails, pumps, motors) are still sent when you press them.
* Start in MANUAL with `daltui --manual`. In SIM there is no link, so UPDATE only prints a note.

## Talking to the flight software

Each request is one `Cmd` (`src/proto.rs`) with two forms: the console text line and the binary
`(type, addr, payload)`. The console translates its text into the same triple, and its `json`
reply carries the TM payload as `payload_hex`, so one decoder serves both transports and
`--compare` can check them byte for byte. Polling runs on a worker thread (`src/link.rs`); the UI
only queues jobs and drains results, so a slow EXP command never blocks the keys.

| Panel | Command (TYPE/ADDR) |
| --- | --- |
| OBC onboard temp, NTC1-4, BME688, PAC U6/U2 | `tm_obc_get` 04/02 |
| OBC eFuse, USB, MIPI switches; set | `pwr_stat OBC` 01/04; `pwr_en OBC <out> on\|off` 01/03 |
| EXP rails, heaters, IR | `pwr_stat EXP`, `pwr_en EXP`; `thm_stat` 01/15, `exp_run heater_set` / `ir_set` 02/0C |
| Pumps U11/U15 | `fld_stat` 01/18; `fld_ctl pump` 01/17; `fld_cfg pump detect`/`pump_type` 01/16; `exp_run pump_init` |
| PC crystallization | `pc_stat` 02/00; `pc_cfg` 02/01 for valve/flow/routes/profile/datalog; `pc_ctl` 02/02 for operations; `exp_run pc_status` / `pc_profile_show` 02/0C for details |
| DRV8910, motors | `rba_stat` 01/1B; `rba_ctl init\|home\|stop\|stop_hold` 01/1A; `exp_run motor5_mode_set`, `motor5_run`, `motor5_move`, `motor5_cfg_set`, `motor5_pwm_freq_set`, `motor5_limits_set`, `limit_cfg_set` 02/0C |
| PDU VOUT, TRANS, inputs, setpoint | `pwr_stat PDU`, `pdu_vout get\|set` 01/02, `pwr_en PDU` |
| PDU latched eFuse fault clear | `pdu_fault_clear` 01/1C; PDU refuses while a live fault remains |
| PDU heartbeat and bootloader | `pdu_heartbeat` 01/1D reads/sets LED enable and on/off periods; `pdu_bootloader enter` 01/1E resets the PDU into its bootloader |
| PDU temperature, mode | `gui_state` (console only) |
| Board online, versions | `sys_info` 01/00 |
| USB camera capture | `uvc_ctl` 01/11, `uvc_stat` 01/12 |
| EXP IR camera | `exp_run camera_config`, `camera_buffer_enable/disable`, `camera_detect`, `camera_init`, `camera_capture`, `camera_status`, `camera_temp` 02/0C |

The EXP IR Camera page uses the MCU's 32×24 thermal sensor frame. Enable the selected I2C buffer,
detect and initialize the sensor, then capture. The capture action polls `camera_status` until
the MCU reports a final result and shows its PSRAM object ID. Set X/Y/width/height within
32×24 and choose **Measure max temp** to read the hottest pixel in that region from the
last completed frame. Temperature is displayed in °C; the MCU sends centi-°C.
An object ID identifies image data held on the EXP MCU. Flight software downloads it to
`images/exp` under `DAL_EXP_DATA_DIR` after a table-6 notification and CRC verification;
the object ID alone is not a Linux image path.
The IR Camera panel shows **Saved image** after the flight software's table-6
snapshot confirms the matching boot ID and object ID were written on the OBC.
The capture action checks for completion automatically; **Read status** checks
again on demand. Use `dat_lst` to browse saved files.
The staged I2C address in **Apply settings** selects an address already programmed on the sensor.
**Program sensor addr** changes the physical address with only that branch enabled; power-cycle
the sensor if the MCU reports `restart_required=1`.

MIPI route/capture use `gui_shell` in Console mode. EXP temperatures and flows use
the cached RPARAM table 2 snapshot in `gui_state` (Console mode); they remain `n/a`
over RS-422. Not wired yet: EXP/OBC reset and the PDU registers beyond VOUT/TRANS/setpoint/fault clear/heartbeat/bootloader (the flight
software does not expose them). Ranges for pump frequency/voltage and motor speed are the
firmware limits from `flight_console_service.c`.

The rig's EXP model implements the PC CLI replies expected by `pc_cfg` and `pc_ctl`.
If `pc_cfg valve map 0 1 2 3` returns flight error `0x01` in an older rig process,
restart `python3 tools/fsw_rig.py`: its generic `OK` reply does not match the MCU's
`OK pc_valve_conf_channel`. This error is a rejected response, not a valve-map range error.

### Trying it without hardware

```bash
cd ~/Linux_DAL/service/daltui
cargo build --release           # rebuild daltui after source changes
./target/release/daltui --sim   # UI-only simulation; no flight software needed
make -C ../dal_obc_fsw          # rebuild host FSW and console client for the rig
python3 tools/fsw_rig.py        # host FSW + fake EXP/PDU/MIPI; prints CONSOLE, RS422, MIPI_DATA
python3 tools/fsw_rig.py -v     # same, and print the fsw -v log plus the commands it sends to the EXP and PDU
python3 tools/fsw_rig.py -v --quiet-housekeeping  # hide routine FSW/EXP/PDU polling traces
# Restart the rig after editing tools/rig_pc.py; a running process keeps its old model.
python3 tools/fsw_rig.py --periodic-logs off  # show commands and frames; hide routine CSP/PDU polling
python3 tools/fsw_rig.py --periodic-logs on   # show routine CSP/PDU polling again
./target/release/daltui --console <CONSOLE> --rs422 <RS422> --compare
./target/release/daltui --console <CONSOLE>
```

Run the rig and daltui in separate terminals. In Console mode, MIPI capture writes a
synthetic 5120×3840 PNG and its RAW frame under the printed `MIPI_DATA` directory.
Inspect or copy them before stopping the rig; Ctrl+C removes its temporary directory.

On the OBC set `DAL_OBC_RS422_SOCKET=/run/dal/dal_obc_fsw/rs422.sock` in `/etc/default/dal-obc-fsw`
and restart `dal_obc_fsw`. The frame socket serves one client at a time, so daltui keeps a single
connection and closes it when it exits or you change source.

## Simulated behaviour

The sim mirrors the firmware rules from the reference code, so interlocks can be exercised:
heaters and IR need EXP 12 V; burn needs the MOTOR rail, a valid persisted configuration and a fresh five-second arm; pump detect/init/enable need SLN/HD (and the pump type can only change with SLN/HD off); the motor driver needs the MOTOR rail and must be detected then initialized before a jog; a USB capture needs a powered camera; rails refuse to
switch off while a consumer owns them, VOUT needs TRANS (and turning TRANS off drops VOUT),
a PDU fault latches ERROR mode until cleared, heaters trip at 60 °C.

## Where things live

- `src/controls.rs` control tables (add a control here, then handle its `id` in `sim.rs`)
- `src/sim.rs` subsystem state and simulator rules; the structs are shaped like the real telemetry
  and are filled by `src/live.rs` when a real source is selected
- `src/proto.rs` frames, CRC, command builders, payload decoders (with unit tests on captured payloads)
- `src/link.rs` console and RS-422 transports and the worker thread; `src/json.rs` small JSON reader
- `src/live.rs` control id to command (`plan`), reply to model (`decode`/`apply`), polling
- `tools/fsw_rig.py` hardware-free sandbox around the host flight software
- `src/app.rs` state and keys, `src/ui/` drawing


IR LEDs uses three columns with IR 0 above IR 1 in each column:
**Output / EXP** shows the last confirmed applied configuration and live duty,
**Config draft** shows edits, and **Control** has independent EDIT/Set buttons
for Max duty, Duty, PWM, and On time. Each Set writes only that field and reads
back EXP status, including when polling is disabled. Drafts are never overwritten
by status. If confirmation times out, Output retains the last observed values.
IR ON uses the applied duty, not an unsubmitted edit; use Read status first in live
mode. Set Duty while ON changes output without restarting the running timer.
Duty and Max duty span 0–100%; maximum defaults to 100%. On time spans 0–5000 ms;
zero stays on until OFF. Changes to On time apply on the next ON. Settings are RAM
only. The firmware must support `ir_cfg_set <ch> duty_pct <value>` and report
`set_duty_pct` in `ir_status`.

### IR camera point measurements

The status cards show **Max temperature** and **Max position** for the latest
completed capture, updated by the automatic capture-status follow-up even with
background polling disabled. These fields require EXP firmware with
`CAMERA_PEAK` support; older firmware leaves them unavailable.

**Point measurement** uses **Center X** (0–31), **Center Y** (0–23), and **Delta**
(0–31 pixels). The square includes both endpoints and is clipped to the image
edges: center (10,10), delta 2 measures (8,8) through (12,12); center (0,0),
delta 2 measures (0,0) through (2,2). Delta 0 measures a single pixel.
**Measure max** and **Measure average** report **Point max** and **Point average**
on the status card. Changing the region or capturing a new frame clears these
point results. Neither measurement takes a new capture; capture again for a new
sample. Average measurement requires the new EXP firmware's optional `mean` mode.

### Burn request confirmation

On **Burn wire**, each wire has **Set config**, **Read status**, and **Burn request [RUN]**.
Burn request opens a five-second dialog with **CANCEL** selected by default and
sends the internal arm request for that wire. Use arrows or Tab to select CANCEL
or FIRE, then Enter; Esc cancels. FIRE requires a matching, unexpired arm
confirmation. Waiting for arm confirmation does not restart the dialog timer.
The countdown uses monotonic wall time, including while simulation is paused.
Timeout and cancellation close the dialog and send the internal abort/disarm
command. Neither can fire. Navigation cannot change wire or page while the
dialog is open. The Overview Burn request shortcut uses the same dialog.
