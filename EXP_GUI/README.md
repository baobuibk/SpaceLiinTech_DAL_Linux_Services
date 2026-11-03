# EXP terminal UI

`exp_tui` controls the EXP directly from a Linux terminal. It uses the serial
transport and firmware commands copied from the EXP test GUI, and does not
need X11, Wayland, or `dal_obc_fsw`. The original desktop program is kept as
an optional build. Its source and original instructions came from
`service/ref/https-github.com-baobuibk-SpaceLiin_BEE-DAL_EXP_V1.0_FW/tools/exp_test_gui`;
see [UPSTREAM_README.md](UPSTREAM_README.md).

## Build and run

```sh
make -C service/EXP_GUI
make -C service/EXP_GUI run
```

This opens a terminal UI over SSH, a serial console, or a local terminal.
Set the EXP serial device with `p`, then press `o` to open it. You can also
start with a configured port:

```sh
make -C service/EXP_GUI run ARGS="--link console --port /dev/ttyUSB0"
```

## Simulate without hardware

```sh
make -C service/EXP_GUI sim
```

This opens the same terminal UI with four simulated links already connected.
Navigate the command pages and press Enter to see sample responses; power
rail commands change their simulated ON/OFF readback. The header and every
reply are marked `SIMULATED`. No serial device is opened and no camera file
is written. You can also run an installed binary with `exp_tui --demo`.

The four direct connections are Console/UART2 (115200 baud), XCLI/UART0
(230400), RPARAM/RS485 UART4 (115200), and SLCAN USB-CAN (adapter baud
115200). Press `1`–`4` to choose a connection. Each can have its own serial
device and baud rate. `EXP_GUI_CONSOLE_PORT`, `EXP_GUI_XCLI_PORT`,
`EXP_GUI_RS485_PORT`, and `EXP_GUI_CAN_PORT` set initial port paths. For a CAN
adapter, the program sends `C`, `S8` (1 Mbit/s), then `O` when opening. It
needs an SLCAN compatible adapter; a Linux `can0` SocketCAN interface is
not a serial device.

Use Tab or Left/Right to choose a command section, Up/Down to choose an
action, and Enter to send it. Commands ending with a space open an editor for
arguments. `/` opens a free command field; `e` edits the highlighted command.
Press `t` to read RPARAM table 0–10 and `d` to download the last camera object
reported in a `DATA` metadata line over UART2. Downloads are verified and
saved to `camera_downloads/`, or to `EXP_TUI_DOWNLOAD_DIR` if set. `x` clears
the response log, and `q` exits. The `burn_fire` command requires a second
confirmation.

`exp_tui --help` prints options without opening a terminal UI, and
`exp_tui --list-commands` prints the command catalog. On Linux, the user must
have permission to open the selected serial device (often through the
`dialout` group).

## Other targets

```sh
make -C service/EXP_GUI check          # terminal and shared protocol tests
make -C service/EXP_GUI check-desktop  # original desktop tests
make -C service/EXP_GUI install        # installs ~/.local/bin/exp_tui
make -C service/EXP_GUI deploy         # stages ARM binary in service/deploy/EXP_GUI
make -C service/EXP_GUI run-desktop    # original graphical app; needs X11/Wayland
make -C service/EXP_GUI clean
```

The desktop build uses the optional Cargo `desktop` feature. Its camera file
dialogs use `zenity` on Linux. The TUI has no graphical dependency.
The repository's `make -C service deploy` includes the TUI. After copying
`service/deploy` to the OBC, `make -C /home/root/deploy install` installs
`/home/root/tools/bin/exp_tui` with the other tools. Launch that binary from
an interactive SSH or serial terminal.
