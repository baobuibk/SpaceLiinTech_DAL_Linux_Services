# Run DALTUI

DALTUI is the terminal interface for OBC, EXP, and PDU. Run it with the built-in simulator, against a host flight-software rig, on the OBC, or on an external computer connected by RS-422. Use a terminal of at least **100×35** cells (120×40 recommended). The source and detailed operation guide are in `service/daltui/` and `service/daltui/mkdocs/docs/operation.md`.

## Simulate on the build host

Build the host binary, then start the UI-only simulator:

```sh
cd ~/Linux_DAL/service/daltui
cargo build --release
./target/release/daltui --sim
```

`--sim` needs no flight software or devices. To exercise the real flight-software command path with simulated EXP, PDU, and cameras, use the rig in two terminals:

```sh
# Terminal 1: build the host flight software, then keep the rig running.
make -C ~/Linux_DAL/service/dal_obc_fsw
cd ~/Linux_DAL/service/daltui
python3 tools/fsw_rig.py -v --quiet-housekeeping | tee /tmp/rig.out
```

```sh
# Terminal 2: connect to the path printed as CONSOLE by the rig.
cd ~/Linux_DAL/service/daltui
./target/release/daltui --console "$(awk '/^CONSOLE / {print $2; exit}' /tmp/rig.out)"
```

The rig also prints an `RS422` Unix socket for testing binary frames locally:

```sh
./target/release/daltui --rs422 "$(awk '/^RS422 / {print $2; exit}' /tmp/rig.out)"
```

Its temporary sockets and images disappear when you stop the rig with Ctrl+C. `-v` prints flight-software RX/TX logs; `--quiet-housekeeping` hides routine device polls.

## Run on the OBC beside flight software

The complete deploy package builds the ARM binary with the Yocto SDK and
installs it together with flight software:

```sh
cd ~/Linux_DAL
make -C service deploy             # DALTUI_SDK_DIR=/path/to/sdk if needed
scp -r service/deploy root@<board-ip>:/home/root/
ssh root@<board-ip> 'make -C /home/root/deploy install'
```

Start or verify `dal_obc_fsw`, then run DALTUI through its **local console socket**:

```sh
ssh root@<board-ip> 'systemctl status dal_obc_fsw; ls -l /run/dal/dal_obc_fsw/console.sock'
ssh -t root@<board-ip> 'TERM=xterm-256color /home/root/tools/bin/daltui --console /run/dal/dal_obc_fsw/console.sock'
```

If the service is stopped, start it on the board with `systemctl start dal_obc_fsw`. The console socket is mode `0600`, so run DALTUI as root. Pass `--console` explicitly: without a reachable socket, DALTUI can start in simulator mode. Live DALTUI defaults to control-only: no automatic queries are sent. Use `--manual` to allow one housekeeping round when you press `u`, or `--poll` for automatic reads. On the board, control actions affect real hardware.

### Use the RS-422 frame socket on the same OBC

The frame socket is disabled by default. On the board, add this line to
`/etc/default/dal-obc-fsw` (keep the existing debug, port, and data settings),
then restart flight software:

```sh
DAL_OBC_RS422_SOCKET=/run/dal/dal_obc_fsw/rs422.sock
```

```sh
systemctl restart dal_obc_fsw.service
test -S /run/dal/dal_obc_fsw/rs422.sock && ls -l /run/dal/dal_obc_fsw/rs422.sock
TERM=xterm-256color /home/root/tools/bin/daltui --rs422 /run/dal/dal_obc_fsw/rs422.sock
```

Run these commands as root: the socket has mode `0600`. `--rs422` sends the
same `A5…CRC16` ICD frames as an external ground PC; it does **not** open
`/dev/ttyS1` itself. Flight software continues to serve the physical RS-422
port in parallel. The frame socket accepts one client at a time. Use
`--manual` with DALTUI to request one housekeeping update using `u`; the default
control-only mode sends no automatic reads. Commands available only through the local text console, such as
MIPI route/capture through `gui_shell`, require `--console` instead.

## Run from an external host over RS-422

Connect a USB–RS-422 adapter on the host to the OBC RS-422 lines. The flight software owns `/dev/ttyS1` on the OBC. With `dal_obc_fsw` running, build and start DALTUI on the **external host**:

```sh
cd ~/Linux_DAL/service/daltui
cargo build --release
./target/release/daltui --rs422 /dev/ttyUSB0 --baud 921600
```

The serial link uses 921600 baud, 8N1, and no flow control. Substitute the actual adapter device and ensure your host user can open it. DALTUI sends the ICD `A5…CRC16` frames and receives telemetry on the same serial link. The Unix `console.sock` and optional `rs422.sock` are local to the OBC and cannot be opened from an external host. Features without an RS-422 command, such as MIPI route and capture through `gui_shell`, show as unavailable; use console mode on the OBC for those operations. Check the **CFG** page for link status and command history.

## Collect OBC files

See [Thu thập file trên OBC bằng DALTUI và command](obc-file-collection.md) for the OBC → Data transfer workflow, Pi download folders, console commands and RS-422 command-client downloads.

## Windows terminal

Use the Windows x64 executable with `--rs422 COM3` or `--sim`. See [Windows setup and EXE build](daltui-windows.md) for the package, optional build target and PowerShell commands.
