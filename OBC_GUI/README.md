# DAL OBC terminal UI

`obc_gui` is a terminal application for the DAL OBC, its single EXP at CAN
node 11, and the PDU. It uses the same `ratatui` and `crossterm` stack as `sltui`, so it runs
over an SSH or serial terminal without X11. The previous `exp_test_gui` desktop
source is retained in `desktop_legacy/` for reference. The board application
gets telemetry and sends every board command through `dal_obc_fsw`.

## Pages and controls

| Page | Data and actions |
| --- | --- |
| HOME | Three side-by-side OBC, EXP and PDU cards with online state and telemetry; OBC eFuses, EXP sensors/switches, PDU VOUT/TRANS, temperature and faults |
| OBC SENSORS | TC1047A, four NTCs, BME688, RTC `/dev/rtc2`; edit a UTC date/time or Unix epoch |
| OBC POWER | Both PAC1934 devices and eight named rails; AI and FPGA eFuse ON/OFF controls |
| PERIPHERALS | I2C/SPI/CAN presence; per-camera USB power ON/OFF and capture; CAMERA_LED PWM duty |
| MIPI CAMERAS | Two selectable MIPI ports, each with sensors 0–3; power, route readback, route apply and RAW capture |
| EXP STATUS | `hk_service` CAN RPARAM snapshot; optional flight software UART `tlm_json` system/sensors/power cache when `DAL_EXP_UART_POLL=1` |
| EXP CONTROL | On-demand UART telemetry and firmware CLI commands through flight software |
| EXP | Integrated EXP monitoring and control: the EXP test GUI sections and peripherals (power rails, PAC1934, pumps, NTC/BME688/IMU/flow/RTC, heaters, motors/DRV8910, limits, IR, cameras, bubble, burn wire, PC status). Status is read automatically; buttons, fields and dropdowns sit next to their readbacks |

The [GUI command map](../../docs/exp-gui-command-map.md) lists the EXP
firmware commands and their RS-422 frames. The EXP page (tab 8) is driven by a
catalog (`src/exp/catalog.rs`) shared with the standalone `exp_tui`: yellow
cards, a section bar, a peripheral list and a page of buttons, fields and
dropdowns with live readbacks. The current page's read-only commands are re-sent
automatically (every 4 s on hardware, 2 s in host-sim) and the answers fill the
readouts; the header line shows `AUTO ON/OFF` and the age of the last data.
Automatic traffic stays out of the command log; commands you press are logged.

EXP page keys: Left/Right change section, Down or Enter go to the peripheral
list, Up/Down choose a peripheral, Right or Enter go to its controls, Up/Down
select a control, Enter presses a button or edits a field (Enter/Esc finish),
Space changes a dropdown, Left/Esc go back one level, `a` toggles
auto-refresh, `/` opens a free firmware command line, `r` refreshes now.
`Tab` and `1`-`8` still switch pages. `burn_fire` needs a second Enter.
All commands use `dal_flight_console --json exp_run` through the flight
software socket, one at a time. A failed automatic read stops auto-refresh and
logs the error (press `a` to resume). Some readout formats (bubble, burn, IR,
flow, IMU, limits) are matched against guessed firmware reply text and may show
`--` until checked on hardware; the raw reply is always in the command log.
HOME / RPARAM tables shows the EXP CAN snapshot supplied by
flight software `gui_state`; press `r` to refresh it. The original desktop GUI's UART2 XMODEM
image download still needs a separate CAN/RS-485 Table 11 collector before
an OBC file can be offered to the remote RS-422 host.

The TUI requests `gui_state` from the flight software Unix socket every
second. Flight software supplies its OBC housekeeping cache, EXP snapshot,
RTC, GPIO and MIPI readback, CAMERA_LED PWM duty, device presence, and PDU
Modbus telemetry. It polls the PDU every five seconds on `/dev/ttyS3` at
115200 baud, unit 1. The GUI labels old snapshots stale and respects the
OBC and EXP validity flags. Start `hk_service`, `temp_service`, `bme_service`,
and `dal_obc_fsw` for full telemetry. PDU is shown offline if its poll fails.

All board commands enter the flight software socket. The GUI sends RTC changes
as `sys_rtc` and other actions as `gui_shell` requests; flight software runs
the installed board tools and returns JSON with exit status and text. Commands
run in a GUI background worker with a timeout. `usb_cam` handles camera
power, SEL1/SEL0, active-low OE, enumeration, and a 1280×720 JPEG capture.
Captures are saved to `/home/root/cameraN-<unix time>.jpg`. Manual USB power
buttons set the individual `usb_N_pwr_en` lines;
capture also powers the selected camera and leaves it powered. The app does not
take over `/dev/ttyS4`. EXP control commands go through the flight software's
EXP UART0 text CLI; CAN RPARAM remains owned by the separate `hk_service`.
The RTC editor starts with the flight software's `/dev/rtc2` UTC value. Use Left/Right,
Home/End, Backspace and Delete to edit `YYYY-MM-DD HH:MM:SS`; a Unix epoch is
also accepted. Enter validates the value and calls
`dal_flight_console sys_rtc <epoch>`, which updates the system clock and
`/dev/rtc2` through flight software. The OBC eFuse buttons
control only `pow_onoff_ai` and `pow_onoff_fpga`.
Flight software writes these and the manual USB power lines through the
device tree's `/sys/class/leds/<name>/brightness` entries and verifies
readback. They do not require entries in `xio.conf`.

The MIPI page follows the board device tree: camera 1 uses `pow_mipicam_1`
and `en_i2c_mipicam_1`; camera 2 uses the corresponding `_2` signals.
`sel_mipicam_on2_off1` selects camera 1 when low and camera 2 when high.
Each camera board has AR2020 sensor slots 0–3, selected through the installed
sensor and lane switch sysfs controls. **Apply route** isolates the other I²C
branch, powers the selected camera, sets the board select line, and verifies
both switch readbacks. **Capture** applies the route and runs the installed
`capture_ar2020.sh`; a nonempty `5120×3840` BG10 RAW image is saved as
`/home/root/mipiN-sensorS-<epoch>.raw`. The selected camera remains powered
after routing or capture; use its Power OFF action when finished. These operations share the hardware
lock with sltui/obc-experimentd. Capture requires `media-ctl`, `v4l2-ctl`,
the AR2020 kernel pipeline, and the capture script on the target.

CAMERA_LED uses PD29/PWM3, exposed as `/sys/class/pwm/pwmchip0/pwm3` in the
board device tree. Flight software owns this PWM and the MIPI hardware
operations. If PWM chip numbering changes, set
`DAL_GUI_CAMERA_LED_PWM=/sys/class/pwm/pwmchipN/pwmM` in the flight service.
Duty is limited to 0–100%; 0 disables PWM.

## Build and install

```sh
make -C service/OBC_GUI check
make -C service/OBC_GUI target
make -C service/OBC_GUI deploy
```

The default target is static `armv7-unknown-linux-musleabihf`, using
`rust-lld`, like `sltui`. Install it once with
`rustup target add armv7-unknown-linux-musleabihf` if needed. `deploy` stages
`service/deploy/OBC_GUI/obc_gui`. After copying `service/deploy` to the board,
`make -C /home/root/deploy install` installs it in `/home/root/tools/bin`.

```sh
/home/root/tools/bin/obc_gui           # interactive terminal UI
/home/root/tools/bin/obc_gui status    # one-shot text status
```

On the host, `make -C service/OBC_GUI` builds a native release binary. The
interactive UI needs a real terminal; `status` works in scripts.

For a hardware-free host simulation of every page and action, run
`make -C service/OBC_GUI/host_sim run`. See the
[host simulation guide](host_sim/README.md). The header clearly marks this
mode as simulated and actions never access hardware.

Keys (pages 1-7): `Tab` or `1`–`8` selects a page, `Up`/`Down` chooses an action, `Enter`
runs it, `r` refreshes data, `PgUp`/`PgDn` scrolls a page, `[`/`]` scrolls
the result log, and `q` quits. EXP command entry accepts up to 95 bytes.

The flight socket defaults to `/run/dal/dal_obc_fsw/console.sock`; override it
with `OBC_GUI_FLIGHT_SOCKET`. `OBC_GUI_FLIGHT_CONSOLE` and
`OBC_GUI_FLIGHT_FSW` override the installed client and flight binary paths.
The GUI builds USB capture requests using `OBC_GUI_USB_CAM`. Set
`DAL_EXP_UART_PORT` in `/etc/default/dal-obc-fsw` to the OBC serial device
physically wired to EXP UART0. The flight software configures it for 250000
baud unless `DAL_EXP_UART_BAUD` overrides that value. No port is assumed:
the current DAL device tree labels ttyS1 as external RS-422, ttyS2 as
PC/104 RS-485, ttyS3/ttyS4 as PC/104 RS-232, and ttyS5 as FPGA.
Configure flight hardware access with
`DAL_GUI_PDU_PORT`, `DAL_GUI_PDU_UNIT`, `DAL_GUI_LED_ROOT`,
`DAL_GUI_FPGA_LOCK`, `DAL_GUI_MIPI_LANE`,
`DAL_GUI_MIPI_SENSOR`, `DAL_GUI_MIPI_CAPTURE`, `DAL_GUI_MIPI_DATA_DIR`,
`DAL_GUI_MIPI_LOCK`, and `DAL_GUI_CAMERA_LED_PWM` in its systemd environment.
