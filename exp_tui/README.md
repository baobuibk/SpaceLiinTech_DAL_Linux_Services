# exp_tui

Terminal UI for the DAL EXP board, styled after the EXP test GUI screenshots
(blue header, section tabs, link bar, PERIPHERALS | page | CONSOLE columns, yellow cards).
Built on ratatui/crossterm: no X11, runs over SSH or a serial console.

```bash
make sim                          # host simulation, no hardware
make run ARGS='--port /dev/ttyUSB0'   # host, real Console (UART2)
make host                         # native release build
make target                       # armv7-unknown-linux-musleabihf (static)
make deploy                       # target + copy to ../deploy/exp_tui
make check                        # unit tests
make dump PAGE=heaters SIZE=120x40    # render one page as text, no tty
```

Add `OFFLINE=1` to any make target to build from the local cargo cache.
Cross build needs `rustup target add armv7-unknown-linux-musleabihf` (linker: `rust-lld`, see `.cargo/config.toml`).

Navigation (levels): `Tab`/`Shift-Tab` walk the top menu (HOME, POWER, PUMPS, SENSORS, ACTUATORS,
EXPERIMENT, PORT SETTINGS). `Enter` opens the menu into the left list, `↑/↓` choose a page,
`→` moves to the page, `→` again to the console, `←` goes back one level.
In a page: `↑/↓` select, `Enter` press/edit a field, `Space` change a pick.
`/` console, `p` port settings (`e` pick port from a list, `t` type a path, `b` edit baud (digits only), `o` open/close), `1-4` link,
`a` auto-refresh on/off, `PgUp/PgDn` log, `Ctrl-L` clear log, `q` quit.

Status values refresh automatically every 2 s with read-only commands; that traffic is not
shown in the console log, which is meant for debugging.

## Status (v1)
- All 20 pages of the desktop GUI exist, with commands taken from the firmware help strings.
- `--sim` answers from a built-in model (`src/sim.rs`); camera capture shows a synthetic 32x24 frame.
- Live mode: **Console (UART2) only**. XCLI (xTP), RS485/CAN (CSP/RPARAM) need framing not ported yet.
- Not ported: IR image download (XMODEM), RPARAM table reads, request-id auto polling.
- Readout needles for BUBBLE/BURN/IR/FLOW/IMU/PC replies are guesses; verify against real output.
