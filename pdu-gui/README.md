# DAL PDU Monitor

Rust terminal application for the **SpaceLiinTech DAL PDU AVR HW1.1**, using
Ratatui and Crossterm like `sltui`. It runs directly in a terminal or over SSH;
**DISPLAY, Wayland, OpenGL and a desktop environment are not needed**.
It monitors all implemented Modbus tables and writes supported coils and holding
registers. It is not AVR firmware and is not added to the Yocto image.

## Build and run

Requires Rust 1.88 or later (validated with 1.98.1) and a C linker. The first
build downloads dependencies; Cargo.lock records the resolved versions.

```bash
cd service/pdu-gui
cargo build --release --locked
./target/release/dal-pdu-gui
# Supply connection settings and open the port immediately:
./target/release/dal-pdu-gui --port /dev/ttyUSB0 --baud 115200 --slave 1 --connect
# Remote interactive session (allocate a terminal):
ssh -t user@host /path/to/dal-pdu-gui
```

Use a terminal of at least **70 columns × 20 rows** (120×35 recommended).
The binary name is retained so existing launch commands work. Serial permissions
are those of the user running it. Windows COM ports are supported by the
libraries but have not been tested here. Linux serial support does not need
libudev-dev. No automatic connection or writes occur on a plain launch.

The previous desktop interface remains optional:

```bash
cargo run --locked --features desktop --bin dal-pdu-desktop
```

Only this optional binary needs X11/Wayland/OpenGL on Linux. The default build
excludes eframe/winit and their graphical dependencies.

## Build for the real OBC target

The build wrapper loads the SDK from `/home/buildmachine/oecore-obcv110` and
cross-compiles the terminal binary for `armv7-unknown-linux-gnueabihf`, using
the SDK's Cortex-A7 hard-float compiler, NEON/VFPv4 options and target sysroot.
It does not need the SDK environment to be sourced in your shell.

```bash
cd service/pdu-gui
# One-time prerequisite, if not already installed:
rustup target add armv7-unknown-linux-gnueabihf
make target                 # equivalent: ./build.sh target
make host                   # equivalent: ./build.sh host
make deploy                 # build ARM executable and copy to ../deploy/dal-pdu-gui
```

Target executable:
`target/armv7-unknown-linux-gnueabihf/release/dal-pdu-gui`.
Use this ARM executable on the OBC; `target/release/dal-pdu-gui` is the host build.
The target build excludes the optional desktop interface. It is dynamically
linked against the SDK's glibc and must run on a compatible Yocto image.

To use another installation of the same SDK:

```bash
PDU_SDK_ROOT=/path/to/oecore-obcv110 make target
```

`make deploy` creates `service/deploy/` if needed and stages the ARM executable
there as `dal-pdu-gui` with executable permissions, replacing any previous copy.
The staged binary is ignored by Git. It does not copy anything to a remote board.

Copy the ARM executable to the board using your usual deployment workflow,
then run it from the serial console or an SSH session with a terminal allocated.
The script only builds: it does not deploy or alter any rootfs overlay.
The ARM binary has been cross-built and its ELF architecture/ABI inspected;
execution on physical hardware remains untested.

## Connect and navigate

1. On the DAL OBC, connect the PDU to the PC/104 RS-485 bus and select
   `/dev/ttyS2`. The board's PE5 direction pin is driven by FLEXCOM0 RTS in
   kernel RS-485 mode; the application does not toggle it. For a USB connection,
   use an automatic-direction USB–RS485 adapter. RS-232 requires a USB–RS232
   adapter or the matching onboard RS-232 port. Do not use a bare TTL adapter.
2. Press **s** for settings. Edit the port (`/dev/ttyUSB0`, a `/dev/serial/by-id/`
   path, or `COM3`), baud, slave, and timeout. **Tab** changes fields; **Ctrl-U**
   clears the active field; **Enter** saves; **Esc** cancels.
3. Press **c** to connect/disconnect. Defaults: **115200 baud, slave 1, 8N1,
   no flow control**, 500 ms response timeout. Disconnect before changing settings.
4. Navigate with these keys:

| Key | Action |
|---|---|
| Tab / Shift-Tab / Left / Right / 1–4 | Switch table |
| Up / Down / j / k | Select parameter |
| PageUp / PageDown / Home / End | Scroll/select |
| Enter / e | Edit selected writable value |
| Enter in editor, then y | Review, then apply the write |
| n / Esc in review | Cancel without sending |
| r | Refresh |
| t | Prepare PC time in clock editor; still requires review/apply |
| l | Recent activity and write results |
| q / Ctrl-C | Quit (Ctrl-C also works inside a dialog) |

PDU RS232 is USART0 PE0 RX / PE1 TX, PC104 H1-19/20, fixed at 115200 baud.
PDU RS485 is USART1 PD2 RX / PD3 TX, H1-37/38, with PD5 controlling the PDU's
own transceiver. Its baud is configurable and defaults to 115200.
Firmware documentation identifies an RS232 transceiver issue (IF-001) that must
be corrected on the board. Both links expose the same map. Run only one master
application against a serial port at a time.

## Monitor and edit

- The worker polls all tables, waits one second after each completed scan, and
  repeats. **r** requests an extra scan. The interface remains responsive during
  serial timeouts. A disconnected or invalid row is shown as unavailable;
  select it for the exact error. The sample age includes the scan duration.
- Addresses are the firmware's conventional **one-based** addresses. Wire offsets
  are calculated internally (40100 → 99). Register words and 32-bit values use
  high word first; Modbus CRC bytes use low byte first.
- The table shows engineering values, units and hexadecimal raw words. Signed
  temperatures use °C; power is converted to mW. Firmware sentinel values display
  as unavailable/unset rather than plausible measurements.
- To edit a writable value, press **e**, type a value, press **Enter** to review the
  target and effect, then **y** to apply. A normal connection/refresh never writes.
  Each write is validated and acknowledged; a subsequent scan displays actual
  device state. Read-only identity fields cannot be edited.
- Boolean values accept `0`, `1`, `off`, `on`, `false`, `true`. Integers accept
  decimal or `0xHEX`. Temperature limits accept 0.1 °C steps. Select a parameter
  to see its permitted range and effects. All 32-bit writes use one FC16 transaction.
- VOUT setpoints are 800–20000 mV in 10 mV steps. Programming one can power
  TRANS on. Enabling VOUT requires a programmed setpoint; disabling TRANS also
  disables VOUT. The firmware ultimately decides whether an operation is allowed.
- EEPROM configuration changes are sent only on explicit Apply. RS485 baud
  changes apply **after reboot**, not to the current connection. RS232 stays 115200.
- To inspect an event, write its sequence to 40050 (0 = newest), then inspect
  30041–30049. Clock fields display Unix seconds UTC; **t** fills
  the clock editor without sending anything.
- 40060 accepts only `0xB007`. Applying it powers outputs off and enters the
  bootloader; normal Modbus monitoring may stop until the application returns.
- Writes are **never automatically retried**. A timeout can mean the device acted
  but its response was lost. Inspect the actual state before submitting again.
  Action registers (clear fault, mode retry, bootloader) need not read back the
  value written; an acknowledgment is not a guarantee of a persistent value.

## Source map

- `src/map.rs`: complete firmware register catalog, scaling, limits and write encoding.
- `src/rtu.rs`: serial RTU transport, deadline, CRC, exceptions and acknowledgment checks.
- `src/worker.rs`: serialized background I/O and periodic reads; no writes during polling.
- `src/main.rs`: terminal entry point, CLI settings and read-only probe.
- `src/tui.rs`: Ratatui screens, keyboard controls and staged write dialogs.
- `src/bin/desktop.rs`: optional egui/eframe desktop interface.
- `tests/`: protocol tests and Unix PTY tests using a simulated Modbus slave.

Register definitions were checked against
[`app_modbus.c`](../ref/SpaceLiinTech_DAL_PDU_AVR_FW_HW1.1/firmware/DAL_PDU/DAL_PDU/src/dev/M0_App/app_modbus.c),
[`modbus_implementation.md`](../ref/SpaceLiinTech_DAL_PDU_AVR_FW_HW1.1/doc/docs/modbus_implementation.md),
and the existing Python tool. Reserved gaps are never polled or written.
Framework references: [Ratatui](https://docs.rs/ratatui/0.29.0/ratatui/),
[Crossterm](https://docs.rs/crossterm/0.28.1/crossterm/),
[serialport](https://docs.rs/serialport/4.10.1/serialport/).

## Validation

```bash
cargo fmt --check
cargo clippy --locked --all-targets -- -D warnings
cargo test --locked
python3 tests/tui_smoke.py target/release/dal-pdu-gui
# Optional, read-only hardware probe; exits at the first failed register read:
cargo run --locked -- --probe /dev/ttyUSB0 115200 1
```

Tests cover all four terminal views, dialogs and keyboard editing without a display, CRC/fragmented frames,
exceptions, invalid replies, register offsets, endian order, limits, signed and
unavailable values, real serial reads/writes over a PTY, and worker polling.
The real terminal application is also tested in a pseudo-terminal with display
variables unset. No physical PDU was available during development. Before
operational use, verify both physical links, compare telemetry with the existing
Python tool, exercise a benign LED-period write, and verify firmware responses
for output controls, fault states and reboot operations on a suitable bench setup.
