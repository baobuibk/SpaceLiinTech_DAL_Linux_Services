# Operation

Two ways to run `daltui`: on the build host against the simulator or a sandbox copy of the flight software, and on the real board against the running `dal_obc_fsw`.

All paths assume the project at `~/Linux_DAL/service/daltui`. The terminal must be at least **100x35** (120x40 recommended).

## 1. Simulation (build host)

### 1.1 Build

```bash
cd ~/Linux_DAL/service/daltui
cargo build --release --offline        # drop --offline if the cargo cache is not needed
cargo test --offline                   # optional: CRC, framing, JSON and decoder tests
```

The binary is `target/release/daltui`.

### 1.2 Built-in simulator

```bash
./target/release/daltui --sim
```

Nothing is sent anywhere. The simulator enforces the firmware interlocks (rail ownership, pump and motor prerequisites, heater trip, PDU fault latch) and refusals appear in LOG.

| Key | Action |
| --- | --- |
| `[` `]` / `Tab` / `1-6` | Switch tab |
| `,` `.` | Switch page (OBC, EXP) |
| `<` `>` (or Left/Right) | Move between control panels on the current page; on a single-panel page, move to the adjacent page |
| `k` / `j` | Select a control within the focused panel |
| `Enter` / `Space` | Edit a number, choose an option, toggle a switch, or run an action |
| `f` | Inject a fault on the current subsystem |
| `p` | Pause the simulation |
| `Y` | Confirm a `[ ! ]` action (reboot, burn fire, resets, bootloader) |
| `q` | Quit |

On **OBC → Overview → Set Time**, press `Enter` to edit the UTC clock. Enter an exact `YYYY-MM-DD HH:MM:SS` value, then press `Enter` to send `sys_rtc` to the flight software. `Ctrl+U` clears the input; `Esc` cancels. The editor starts with the displayed RTC time. Invalid dates and times are rejected before a command is sent.

### OBC Camera page

Select **OBC → Camera**. The left panel controls four USB power switches, the selected capture camera, CAMERA_LED PWM, and JPEG capture. The right panel selects MIPI camera 1 or 2 and one of its four AR2020 sensors, then powers, routes, or captures from that selection. Use `j`/`k` to move between controls, `h`/`l` to change selections or PWM, and `Enter` to run an action. The capture status and saved file path appear below each panel.

The flight software stores USB JPEGs at `/home/bee/data/images/usb/USB<camera index>_<request id>.jpg`. MIPI capture saves a 39,321,600-byte BG10 RAW frame and a 5120×3840 PNG at `/home/bee/data/images/mipi/MIPI<camera index>.<sensor>_H_<sequence>.raw` and `.png` (indices start at zero). `DAL_UVC_DATA_DIR` and `DAL_GUI_MIPI_DATA_DIR` override those directories. USB capture uses `service/usb_cam` with a saved UVC profile (default MJPG 1920×1080). The Yocto script `/home/root/tools/camera_tools/capture_ar2020.sh` captures the AR2020 RAW frame; `/home/root/tools/bin/raw_png` converts it to PNG. MIPI route and capture require **Console** mode through `gui_shell`. USB LED PWM is part of the UVC profile; RS-422 can use USB camera commands, but has no equivalent MIPI route/capture command.

### EXP IR LEDs and burn wire

Use **EXP → IR LEDs**. The page has Output / EXP, Config draft, and Control columns, each with an IR 0 row and an IR 1 row. Each field has an EDIT and its own Set button: Max duty, Duty, PWM, and On time. Editing changes only the draft; Set sends only that field and confirms it from EXP status. Read status never overwrites drafts. IR ON uses the applied duty. Set Duty adjusts a running LED without restarting its timer; zero turns it off. On time 0 means continuous, and 1–5000 ms is a timed run; a new On time applies to the next ON. EXP 12 V must be enabled and only one channel can be ON at a time. Configuration is RAM-only.
The IR page's left panels show the EXP 12 V rail, each LED's active state and actual duty, and the selected channel's limits. Entering the page reads `ir_status` once; **Read status** refreshes those details on demand. This page does not show heater or pump telemetry.
The firmware also has a lower-level minimum-off interval and separate camera-feedback/temperature-PID profiles. Its `ir_cfg_set` CLI exposes only the three limits above; these other modes are outside this manual IR page.

Use **EXP → Burn wire** to select wire 0 or 1. The three persisted configuration fields are:

| Field | Allowed value |
| --- | --- |
| PWM duty | 1–100% |
| Burn time | 50–1,000 ms; one continuous PWM interval |
| Maximum total current | 1–3,000 mA absolute MOTOR 12 V rail current, including other loads |

**Persist config** sends `burn_cfg_set` for all three fields; **Read status** verifies what EXP stored. The MCU rejects old ten-field FRAM configurations after upgrade, so configure each wire again. Turn the **MOTOR** rail on, select the wire, then **Arm selected**. It expires after five seconds. **FIRE armed wire** requires `Y` confirmation; **Abort** switches both outputs off. Burn control uses the MOTOR rail and fresh PAC feed measurements; the MCU retains a fixed 8–16 V motor-rail check. `COMPLETED` means the timed pulse ended and does not prove the wire separated. The rig models these steps without driving hardware.
The Burn page's left panels show the MOTOR rail, arm/fire state, active wire, configuration status and last result for both wires. Entering the page reads `burn_status` once; arm, fire, abort and persist actions refresh it again. Heater and pump panels remain on the EXP Overview page.

### EXP crystallization

Open **EXP → Crystallization**. The left side shows `pc_status` and settings; the right side sends
operations. Set the U11 pump type first. Stage unique valve channels 0–3, valve pulse, flow sensor
0/1 and logging interval, then use the corresponding **Apply** action. The route editor stores
Standby, Test, Replace, Circulation or ONGROUND valve positions for the selected case or preset.
The profile editor gives each field its own control. Select Test, Replace, or Circulation, then
set **Pump frequency** (Hz), **Pump voltage** (Vpp), and **Target volume** (µL) near the top of
the settings panel. They are stored separately for each run profile. The frequency editor accepts
8–1992 Hz and the voltage editor accepts 0–250 Vpp; the MCU also checks the configured U11
pump type. Target volume must be positive and is used to end the run when Global **Run mode** is
Volume. Fields outside the selected profile show `n/a`. The Global profile also has an editable ASCII name
(1–23 characters, no spaces). Enter `minflow` and `volume` in 0.001 µL units. A `*`
marks a changed field; `?` means its prior MCU value has not been confirmed. The MCU's
`pc_profile_show` does not report `pump_enable`, so it stays `?` until a successful Apply.
**Apply changed fields** sends one `pc_cfg profile` command per changed
field and clears its mark after a successful reply. Failed fields remain staged for retry; this
operation is not atomic. **Read MCU profile** refreshes fields reported by the MCU without
discarding unapplied edits. Select Both/A/B before starting Test, Replace or Circulation. The `pc_ctl` ACK confirms
submission; the TUI then reads `pc_status` to show the actual state.

Select **Test**, **Replace**, or **Circulation** in PC profile to edit the flow
policy: minimum flow, check enable, grace period, and action (`none`, `warn`, `stop`). Test and
Replace permit 1–100 failing samples; Circulation uses one. Apply sends only changed fields
for the selected profile. The MCU waits for valve-route completion
and sensor readiness before enabling the pump; there is no configurable valve-to-pump pause.
In current MCU code, `none` and `warn` both continue the run and update `last_error`; only
`stop` initiates shutdown. `flow_delay` (default 500 ms) starts when the pump runs and delays threshold checking, not
sensor sampling. The global `pump_off_settle` (default 500 ms) waits after pump OFF before the
next valve movement. The PCLG sample interval (default 1000 ms) is separate.

Datalog is a separate persisted setting. At a run start the MCU builds PCLG v3 objects in PSRAM;
`log_pc` only enables a UART trace. Downloaded PC files belong under
`/home/bee/data/logs/pc/PC_NNNNNNNN.bin`, which the panel shows as a destination. The current
flight software does not yet collect the MCU's `data_list`/`data_read` objects, so the panel marks
file download as unconfirmed. These controls use ICD `02/01` (`pc_cfg`), `02/02` (`pc_ctl`), and
`02/0C` (`exp_run pc_status`/`pc_profile_show`) through either Console or RS-422.

### 1.3 Sandbox with the real flight software

The rig starts the host build of `dal_obc_fsw` with a scripted EXP and a fake PDU, so the UI can be tried against real command handling without hardware. Everything lives in a temp directory and `systemctl` is stubbed.

Build the host binaries after flight software changes, and rebuild daltui after UI changes:

```bash
make -C ~/Linux_DAL/service/dal_obc_fsw
cd ~/Linux_DAL/service/daltui && cargo build --release
```

**Terminal 1**, the rig (Ctrl+C stops it and removes its temp directory):

```bash
cd ~/Linux_DAL/service/daltui
python3 tools/fsw_rig.py -v --quiet-housekeeping | tee /tmp/rig.out
```

It prints two socket paths and the MIPI image directory in a random directory, for example:

```text
CONSOLE /tmp/fsw-auto-0omud68a/console.sock
RS422 /tmp/fsw-auto-0omud68a/rs422.sock
MIPI_DATA /tmp/fsw-auto-0omud68a/mipi_images
```

With `-v` the rig also prints, in time order, `[fsw]` (flight software logs, including console and RS-422 requests), `[EXP]` (CLI lines the fsw sent to the EXP), `[PDU]` (Modbus requests), and `[CSP]` (CAN/RPARAM requests). The CFG tab also shows command history.

Use `python3 tools/fsw_rig.py --periodic-logs off | tee /tmp/rig.out` to keep command and frame logs while hiding routine CSP `ping`/`getall`, EXP telemetry, and PDU Modbus polling. Use `--periodic-logs on` (or `-v`) to show them. The old `--quiet-housekeeping` option remains an alias for `off`. PDU background polling and on-demand reads use identical Modbus requests, so the filter hides both when their request signatures match.

**Terminal 2**, the UI. Use the real path; a literal `xxxx` or `<CONSOLE>` gives "no such file".

```bash
cd ~/Linux_DAL/service/daltui
./target/release/daltui --console "$(awk '/^CONSOLE/{print $2}' /tmp/rig.out)"
./target/release/daltui --rs422   "$(awk '/^RS422/{print $2}'   /tmp/rig.out)"
```

The `^` anchor matters: the `-v` log has other lines containing the word CONSOLE.

For MIPI, use Console mode and select **OBC → Camera → MIPI → Capture**. The rig
simulates the camera and converter, creating a black test PNG and matching RAW file.
Inspect the files while the rig runs:

```bash
ls -lh "$(awk '/^MIPI_DATA / {print $2}' /tmp/rig.out)"
```

Ctrl+C removes the rig's temporary directory, so copy any files you need first.

The rig enables a simulated RTC inside the host FSW process. **OBC → Set Time**
accepts an entered UTC value and `sys_rtc` reads it back without changing the
build host's clock. On the board, the command sets the Linux clock and attempts
to update `/dev/rtc2`.
EXP rail commands in the rig update its fake UART power state and the fake CSP
RPARAM table 2 together. The EXP power panel reflects the new rail voltages and
switch states after the next telemetry poll.
EXP temperatures and flows also come from table 2 through `gui_state.exp` in
Console mode. The UI checks each sensor group's valid bit, status, and age;
missing or stale readings show `n/a`. Flow values are converted from µL/min to
mL/min for display. The RS-422 source does not currently expose this JSON snapshot.
To change a heater duty, first turn **EXP 12 V** on, then select its duty control
and press `>`; each press adds 5%. In the rig, the fake EXP publishes the applied
duty in RPARAM table 12. The UI shows the MCU-confirmed value while that cache
catches up, and warns if the MCU does not confirm the command.
The rig also provides a fake CAMERA_LED PWM directory; changing PWM on the
OBC Camera page updates that fixture instead of host `/sys/class/pwm`.
On the board, CAMERA_LED defaults to `/sys/class/pwm/pwmchip0/pwm3`. If the
flight log reports `CAMERA_LED PWM export missing`, check `ls /sys/class/pwm`
and set `DAL_GUI_CAMERA_LED_PWM=/sys/class/pwm/pwmchipN/pwm3` in
`/etc/default/dal-obc-fsw` to match the registered PWM chip, then restart the
service. If no `pwmchipN` exists, check the PWM driver and device tree.

**Check that console and RS-422 agree** (12 read-only commands, TM payloads compared byte by byte, volatile epoch and age fields masked):

```bash
./target/release/daltui --compare \
    --console "$(awk '/^CONSOLE/{print $2}' /tmp/rig.out)" \
    --rs422   "$(awk '/^RS422/{print $2}'   /tmp/rig.out)"
```

`pc_stat` returns flight error `FF/01` on both paths in the sandbox; that is expected.

### 1.4 Test commands without the log scrolling

Live DALTUI starts in **CONTROL ONLY** mode: it sends no automatic housekeeping or post-action queries. Only controls explicitly selected by the operator send commands, including a selected **Read status** control. To request housekeeping manually, start with `--manual`; then press **UPDATE** (or `u`) for one round. Use `--poll` to restore automatic reads every two seconds. The TC/TM hex of each command is in the CFG command history.

```bash
./target/release/daltui --manual --console "$(awk '/^CONSOLE/{print $2}' /tmp/rig.out)"
```

### 1.5 Headless render (no terminal needed)

```bash
./target/release/daltui --dump home                      # TAB = home|obc|exp|pdu|log|cfg
./target/release/daltui --dump exp 120x40 down,enter     # size, then comma-separated keys
```

## 2. Real board

Procedure for the DAL OBC (SAMA7G5, `dal_obc_fsw` as a systemd unit).

!!! warning "Status"
    The cross-build in step 2.1 was run and produces an ARM EABI5 hard-float executable. Running it on the board has **not** been verified yet: check the first run on a bench unit, not on a flight unit.

### 2.1 Cross-build for the board

The Yocto SDK (`/usr/local/oecore-x86_64`, Cortex-A7 hard-float, glibc) provides the linker. The script wraps it:

```bash
cd ~/Linux_DAL/service/daltui
tools/build-board.sh                   # optional argument: the SDK directory
file target/armv7-unknown-linux-gnueabihf/release/daltui
```

The Rust target `armv7-unknown-linux-gnueabihf` must be installed (`rustup target add armv7-unknown-linux-gnueabihf`; the script does it when missing). The binary is about 0.8 MB.

### 2.2 Install through the deploy package

The complete service deploy package builds the ARM DALTUI binary and includes it
as `deploy/daltui/daltui`. On the build host:

```bash
cd ~/Linux_DAL
make -C service deploy                     # DALTUI_SDK_DIR=/path/to/sdk if needed
scp -r service/deploy root@<board-ip>:/home/root/
```

On the OBC, `make -C /home/root/deploy install` installs DALTUI at
`/home/root/tools/bin/daltui`, installs the flight software unit, enables it
for future boots, starts it now, and waits for the console socket. DALTUI is
interactive; it is launched manually as described below.

For a standalone DALTUI update, copy just its cross-built binary:

```bash
scp target/armv7-unknown-linux-gnueabihf/release/daltui root@<board-ip>:/home/root/tools/bin/daltui
ssh root@<board-ip> chmod +x /home/root/tools/bin/daltui
```

`<board-ip>` is the address of your board; use whichever login method you normally use for it.

### 2.3 Make sure the flight software is running

`dal_obc_fsw` is disabled by default in the image. On the board:

```bash
systemctl status dal_obc_fsw
systemctl start dal_obc_fsw            # if it is not running
ls -l /run/dal/dal_obc_fsw/console.sock
```

The console socket is `/run/dal/dal_obc_fsw/console.sock`, mode `0600`, so **daltui must run as root**. Housekeeping comes from `temp_service` and `bme_service` (files under `/run/dal/temp` and `/run/dal/bme688`), the PAC1934 chips and EXP over CAN. If `tm_obc_get` returns flight error `0x01`, the OBC snapshot is missing or older than 30 s; check those services first.

### 2.4 Run the UI

```bash
ssh -t root@<board-ip> 'TERM=xterm-256color /home/root/tools/bin/daltui --console'
```

With no source flag, `daltui` uses the console when `$DAL_FLIGHT_CONSOLE_SOCKET` or `/run/dal/dal_obc_fsw/console.sock` exists, otherwise it falls back to the simulator and says so in LOG. The header badge shows `CONSOLE LINK`; `NO LINK` in red means the socket cannot be reached (see CFG, Link status).

On a serial console, set the size first: `stty rows 40 cols 120`.

### 2.5 Safe first run

1. Start in MANUAL so nothing is sent until you ask: `daltui --console --manual`, then press `u` on HOME. Check the strip shows all replies OK (in the sandbox `pc_stat` always returns an error; on the board, compare with the CFG history before treating it as a fault).
2. Open CFG and read the command history. Each line is one command with its TC and TM.
3. Switch to AUTO (HOME, `h`) for continuous monitoring. The poll interval is on CFG (0.5 to 10 s).
4. Controls are real: rails, pumps, motors, burn wire and heaters act on the hardware. Frequency, voltage and speed are staged and only sent by the matching Set / Jog action; `[ ! ]` actions ask for `Y`.
5. Controls shown as `[ n/a ]` have no command on the selected transport; MIPI route/capture require Console mode, while reboot and EXP reset are not wired. PC crystallization commands are on the dedicated EXP page. The UI never sends `reboot`.

### 2.6 RS-422 on the board

The frame socket is off unless `DAL_OBC_RS422_SOCKET=/run/dal/dal_obc_fsw/rs422.sock` is set in `/etc/default/dal-obc-fsw` and the unit is restarted. It serves one client at a time and is also `0600`:

```bash
daltui --rs422 /run/dal/dal_obc_fsw/rs422.sock
```

Do not point `--rs422` at `/dev/ttyS1` on the board: the flight software owns that port. A serial device is for a host with a USB RS-422 adapter (`--rs422 /dev/ttyUSB0 --baud 921600`).

### 2.7 Watching the flight software at the same time

```bash
journalctl -u dal_obc_fsw -f
```

For per-frame RX/TX logging, run the unit with `-v` through a systemd drop-in (`ExecStart=` first cleared, then `/home/root/tools/bin/dal_obc_fsw -v`) and restart it. It only logs frames from RS-422 and the frame socket, not console commands.

## 3. Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `connect …: No such file or directory` | Wrong or placeholder path. Take the path printed by the rig, or check `/run/dal/dal_obc_fsw/console.sock` on the board. |
| `connect …: Permission denied` | Socket is root-only. Run as root. |
| `SIM` badge although you wanted a live link | The console socket did not exist at start. Select the source on CFG, or pass `--console PATH`. |
| `CONSOLE NO LINK` (red) | No reply from the socket. CFG, Link status, shows the last error. |
| `fsw closed the frame socket` on RS-422 | Another client holds the frame socket (one client at a time). Close it, then Reconnect on CFG. |
| OBC temperatures show `n/a` | `tm_obc_get` returned flight error `0x01`: the HK snapshot is stale (> 30 s) or the sensor services are down. |
| Garbled layout | Terminal smaller than 100x35, or `TERM` unset on the serial console. |
