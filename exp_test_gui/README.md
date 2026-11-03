# EXP test GUI for OBC and SSH

The default `exp_test_gui` program is a terminal interface for the EXP MCU.
It runs in an interactive SSH or serial terminal without X11, Wayland, or
OpenGL. It uses the firmware commands reviewed in
[UI_FIRMWARE_REVIEW.md](UI_FIRMWARE_REVIEW.md). The original graphical program
remains available as an optional desktop build; see
[UPSTREAM_README.md](UPSTREAM_README.md) for its panels.
The terminal layout keeps the original six section tabs, twenty peripheral
panels, panel buttons, and the adjacent command/reply log.
The first Home/Overview panel follows
[`EXP_GUI/screenshot/home.png`](../EXP_GUI/screenshot/home.png): the blue header,
section tabs, port-status strip, Peripherals list, System checks buttons, and
Console/TX/RX log are in the same order. Use Up/Down and Enter for the five
System checks buttons; `/` opens the console command input, `x` clears the log,
and `p` opens the active link's port setting.

## Build and deploy

```sh
make -C service/exp_test_gui target
make -C service/exp_test_gui deploy
```

`target` uses the Yocto SDK at `/home/buildmachine/oecore-obcv110`. Set
`EXP_TEST_GUI_SDK_ROOT` to override that path. `deploy` stages the ARM hard float
binary at `service/deploy/exp_test_gui/exp_test_gui`. It does not install the
binary into the Yocto image. The parent `make -C service deploy` includes it.
After copying the deploy tree to the board, its `make install` installs
`/home/root/tools/bin/exp_test_gui`.

For host use, run `make -C service/exp_test_gui run
ARGS="--link console --port /dev/ttyUSB0"`. The user needs access to the
selected serial device. Run `make -C service/exp_test_gui demo` to explore
simulated replies without hardware. `exp_test_gui --help` and
`exp_test_gui --list-commands` print text without starting the UI.

## Terminal controls

| Key | Action |
| --- | --- |
| `1`–`4` | Select Console/UART2, XCLI/UART0, RS485/UART4, or SLCAN USB-CAN |
| `p`, `b`, `o` | Set serial device, set baud, open/close selected link |
| Tab or Left/Right | Change peripheral panel |
| `[` or `]` | Move to the previous or next section tab |
| Up/Down, Enter | Select and press a panel button |
| `/`, `e` | Type any command, or edit the selected command |
| `t`, `d` | Read RPARAM table, or download the last camera object over UART2 |
| `x`, `q` | Clear replies, quit |

The four links have independent device paths and baud rates. Initial paths
can be set with `EXP_TEST_GUI_CONSOLE_PORT`, `EXP_TEST_GUI_XCLI_PORT`,
`EXP_TEST_GUI_RS485_PORT`, and `EXP_TEST_GUI_CAN_PORT`. UART2 and UART4 default
to 115200 baud; UART0 defaults to 230400. The CAN link needs an SLCAN serial
adapter, not a Linux `can0` interface. Set `EXP_TEST_GUI_DOWNLOAD_DIR` to
change the XMODEM download folder (default `camera_downloads/`). Downloaded
objects are CRC checked and retained on the MCU until explicitly released.
After confirming the saved file, select `CAMERA: release verified object ID`
and enter the downloaded object ID.

The terminal catalog covers all twenty firmware control panels as buttons and
shows text replies. Buttons needing values open the command editor. Thermal
image viewing and BMP conversion remain in the optional
graphical program:

```sh
make -C service/exp_test_gui desktop
make -C service/exp_test_gui check-desktop
```

The desktop binary is `target/release/exp_test_gui_desktop` and requires a
display server. The source for the command catalog is
[`src/tui_catalog.rs`](src/tui_catalog.rs). Run `make -C service/exp_test_gui
check` for terminal and shared protocol tests.
