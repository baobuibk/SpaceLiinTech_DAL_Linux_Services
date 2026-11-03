# Install deployed OBC services

Build and stage the service package on the build machine:

```sh
cd /home/buildmachine/Linux_DAL
make -C service deploy
scp -r service/deploy root@<OBC_IP>:/home/root/
```

On the OBC, run as root:

```sh
make -C /home/root/deploy install
make -C /home/root/deploy status
dal_flight_console ping
dal_flight_console tm_obc_get
```

`install` checks all staged files, stops the existing services, copies the
ARM binaries and systemd units, including `daltui` at
`/home/root/tools/bin/daltui`, and enables and starts `bme.service`,
`temp.service` and `dal_obc_fsw.service`. It checks
that they are active and waits for the flight console socket at
`/run/dal/dal_obc_fsw/console.sock`. It stops and disables the unused legacy
`rs422.service`; the flight software owns the DAL RS422 interface on
`/dev/ttyS1`.

Gói deploy đặt `DAL_OBC_VERBOSE=1` và `DAL_OBC_QUIET_HOUSEKEEPING=1` trong
`/etc/default/dal-obc-fsw` khi cài đặt, giữ nguyên các biến cổng và đường dẫn
khác. `make install` khởi động lại service để áp dụng cấu hình. Xem
[hướng dẫn xem log](flight-software-logs.md) để theo dõi journal, lọc lệnh và
đổi mức chi tiết; lần cài tiếp theo sẽ đặt lại hai biến về `1`.

The binaries live in `/home/root/tools/bin`. The installer also links
`dal` and `dal_flight_console` from `/usr/bin` so they work by name in a
normal shell. The `dal` tool is an optional snapshot client, while
`dal_obc_fsw` now collects OBC housekeeping, writes `HK0` CSV, and reads EXP RPARAM table 2 over CAN/CSP. The installer stops and disables `hk_service.service` so it cannot compete for CAN.
The installed tools include `fpga_prog`, `fpga_program`, `spi_flash_s25`,
`spi_eeprom_cat93`, `samv71_xbld`, and `avr_xbld`.
The legacy `rs422.service` remains disabled.
The installer removes a previously installed `rs422-service` executable.

Build the DALTUI ARM binary as part of `make -C service deploy`; its default
SDK is `/home/buildmachine/oecore-obcv110` and can be overridden with
`DALTUI_SDK_DIR=/path/to/sdk`. On the OBC, run the installed UI with
`TERM=xterm-256color /home/root/tools/bin/daltui --console` as root. DALTUI
is interactive and does not run as a systemd service.

After installation, `make -C /home/root/deploy start` restarts and verifies
the three active services. `make -C /home/root/deploy stop` stops them.
`make -C /home/root/deploy status` shows their current state and checks
the flight console socket.

The deploy package updates user-space programs and systemd units. Kernel
and device tree changes require booting a newly built DAL image; the
deploy installer does not replace the board's DTB. The DAL image build
command from the repository root is `source build.sh dalobcv110`.

See [flight software commands and interfaces](flight-software.md) for the
local console, RS422 ICD, housekeeping payload, and file transfer workflow.
See [DAL commands and responses](dal-commands.md) for field-level JSON
queries of the housekeeping snapshot.
