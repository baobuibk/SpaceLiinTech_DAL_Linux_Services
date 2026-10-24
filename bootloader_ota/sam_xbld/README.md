# eXecutor Bootloader Client

This project keeps the original `xbld_client` command-line options and
interactive `xbld>` command list. Its bootloader-entry step sends `reset` to
the current EXP firmware's UART0 text CLI, then pings the binary bootloader.
The client does not send xCLI packets to the running application.

## Setup

```bash
bash setup.sh
```

```
/home/root/tools/bin/samv71_xbld
```

The setup menu retains the normal build/install choice. Choice 2 installs
the binary and removes generated build files while keeping this checked-in
source. Set `XBLD_BIN_DIR` to install elsewhere.

Manual build:

```bash
make            # release build -> build/samv71_xbld
make debug      # build with ASan/UBSan -> build-debug/samv71_xbld
make check      # build and check CLI startup (no MCU connection)
make clean
```

## OBC board build and deploy

From this project directory, run:

```bash
make obcv110    # or: make target
make deploy     # build and copy to service/deploy/samv71_xbld/samv71_xbld
```

The cross build sources the Yocto SDK at `~/oecore-obcv110` and writes an ARM
Cortex-A7 executable to `build-obcv110/samv71_xbld`. Override the SDK location
with `SAM_XBLD_SDK_ROOT=/path/to/sdk`. `make deploy` stages the executable in
`service/deploy/samv71_xbld/samv71_xbld`; it does not install it on the board. Native and
OBC objects use separate build directories. The staged ARM binary cannot run
on the x86 build host.

## Usage

### One-shot CLI mode

```bash
samv71_xbld [options] [command]
```

Connection options:
- `-p, --port <dev>` serial port (default `/dev/ttyS4`, OBC PC104 RS-232_2)
- `-b, --baud <rate>` baud rate (default `250000`)
- `--no-arq`, `--no-ecc`, `--crc-type <16|32>` protocol tuning

Common commands:
- `--ping` — ping the bootloader
- `--status` — print bootloader state
- `--erase` — erase application flash
- `--write <file.hex|latest>` — write hex (no erase/jump)
- `--flash <file.hex|latest>` — full flow: text reset + ping + erase + write + verify + jump
- `--verify` / `--jump` / `--reboot` — verify CRC / jump to app / reboot to bootloader
- `--dump <addr> <size> <file>` — dump flash to a .bin/.hex file
- `xbldinfo` — show the last write/flash attempt (host-only, no serial port needed)

Slot selection: use `--slot <0|1>` or `--backup` (0 = main, 1 = backup). Files named `..._backup.hex` are auto-detected as slot 1.

Examples:

```bash
samv71_xbld --port /dev/ttyS4 --flash latest
samv71_xbld --port /dev/ttyS4 --flash firmware_backup.hex
samv71_xbld --port /dev/ttyS4 --ping --quiet && echo OK
```

Exit codes: `0` ok, `1` protocol error, `2` bad args, `3` timeout.

### Interactive mode (REPL)

Running with no command enters the REPL:

```bash
samv71_xbld --port /dev/ttyS4
```

REPL commands: `ping`, `check` (scan the firmware directory `/home/bee/firmware/EXP` and list numbered files), `write <N|path|latest> [backup]`, `flash <N|path|latest> [backup]`, `erase/verify/jump [backup]`, `status`, `reboot`, `bootinfo`, `xbldinfo`, `help`, `exit`.
The REPL `help` command also prints every startup argument, including
`--port`, `--baud`, protocol options, and one-shot commands. Exit and restart
the client to change a startup argument.

Tip: run `check` first, then use the listed index, e.g. `write 1` or `flash latest`.
The one-shot `--flash latest` and `--write latest` options also scan
`/home/bee/firmware/EXP`. Pass a full path for a file outside that folder.
`ping` checks the bootloader first, then probes the application's text prompt.
`reboot` sends the literal `reset` text command before bootloader `bping`.
Standalone `erase` and `write` probe the bootloader and send that text reset
when the application has resumed. The bootloader retries autoboot after 16
seconds of idle time, so a later menu command may need to enter it again.

The client defaults to 250000 baud. `--baud <rate>` changes the Linux serial
rate for the session; the application and bootloader must both use that rate.
The reference EXP generated UART0 C file still shows divisor 33 (about
227273 baud at 120 MHz), so verify that the deployed MCU build contains your
250000 baud update before using `--flash`.

## Change log
See `ChangeLog.txt` for the change history.
