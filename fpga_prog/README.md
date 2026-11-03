# DAL FPGA programmer

Standalone C application for DAL OBC v1.1.0: program the Artix-7 boot image
in U18 **S25FL128LAGMFI010**, verify it, return Flash ownership, and release
FPGA configuration reset. It does not program U17's Microwire EEPROM.

## Build with the OBC SDK

From the repository root:

```sh
./service/fpga_prog/build-sdk.sh
file service/fpga_prog/build/fpga_prog
```

The wrapper sources
`/home/buildmachine/oecore-obcv110/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi`.
Override `FPGA_SDK_ROOT` for another installation of the same SDK. Output is
`build/fpga_prog`, an ARM Cortex-A7 hard-float Linux executable. SDK selection
does not change the DAL board mappings below.

Native tests (no hardware access):

```sh
make -C service/fpga_prog test
```

To stage installation using the SDK:

```sh
./service/fpga_prog/build-sdk.sh install DESTDIR=/tmp/fpga-stage PREFIX=/usr
```

This project is standalone; it is not yet added to the Yocto image recipes.
No background service automatically programs hardware at boot.

## Board mapping

| Function | DAL interface | Meaning |
|---|---|---|
| Flash | `/dev/spidev2.0` | FLEXCOM11, SPI mode 0, 1 MHz |
| Configuration reset | `fpga_program_b`, PD0 | 1 asserts reset through Q7; 0 releases |
| Flash ownership | `sel_flash_spi_fpga`, PB18 | 0 MPU; 1 FPGA |
| FPGA power | `pow_onoff_fpga`, PE1 | Must already be enabled; application does not toggle power |

GPIOs use `/sys/class/leds/<name>/brightness`. The programmer requires access
to these files, spidev, and `/run/lock`. `--device PATH` can override the SPI
node after verifying the deployed device numbering; GPIO mappings remain DAL.
The schematic document's PB6/NPCS1 naming discrepancy remains a hardware
bring-up check; this tool uses the existing DAL device tree's CS0 assignment.

## Input and commands

Use a raw, uncompressed/unencrypted Xilinx SPI x1 boot `.bin` or a Vivado
`.bit` for XC7A35T-FTG256, generated for boot address zero. For `.bit`, the
tool checks the container header and part name, then writes and verifies only
the embedded raw payload. The reference `blink.bit` payload matches
`blink.bin` and `blink_flash.bin` byte for byte. Supply a trusted image whose
provenance and checksum you have checked. The tool does **not** authenticate
images or prove the bitstream will boot. ASCII `.mcs` and IFB FMOD-header
containers are not accepted. The existing IFB `fmod_sign.py` format is not
used here.

```sh
# Host: validate input and print extent without opening hardware.
service/fpga_prog/build-host/fpga_prog --dry-run program boot.bin

# Board, after installation and enabling the FPGA power rail:
fpga_prog id
fpga_prog program boot.bin
fpga_prog verify boot.bin
fpga_prog program blink.bit
fpga_prog reload
```

`id` and `verify` also interrupt the FPGA: reset is asserted before acquiring
Flash. Successful `id` preserves an already-asserted reset; successful verify
or program releases reset. `reload` triggers a new configuration from the
existing Flash contents without reading, erasing, or writing Flash. It asserts
reset for 100 ms, selects the FPGA Flash path, waits 200 ms, then releases reset.
It does not open the SPI device. Successful commands return Flash ownership to FPGA.
The exit codes are 0 success, 1 hardware/operation failure, and 2 invalid input.

## Programming and failure behavior

The application acquires `/run/lock/dal-fpga-prog.lock`, checks power and file
access, asserts PD0, waits 100 ms, selects MPU access, and checks JEDEC ID
`01 60 18`. It uses dedicated four-byte-address read/program/erase commands
(`13h`, `12h`, `21h`) without changing persistent Flash configuration.

Programming erases from address zero through the final **4 KiB sector** occupied
by the image. Bytes after the image within that final sector become `0xff`;
later sectors remain untouched. Erased bytes are checked, data is programmed
in at most 256-byte pages, and the complete payload is compared byte-for-byte
with readback. Busy polling has bounded timeouts and transport errors propagate.
Flash protection is not automatically disabled.

After `id`, `verify`, or `program`, the tool waits 200 ms between returning
Flash ownership to the FPGA and releasing reset. A previously asserted reset
is preserved by `id`.

Only success returns ownership and releases reset. Failure or handled
SIGINT/SIGTERM leaves reset asserted after it has been acquired. Retry with a
known-good image; do not manually release to a failed update. GPIO failures,
SIGKILL, power loss, and other programs can defeat software state guarantees.
The advisory lock coordinates this application's instances only; other SPI/GPIO
users must be stopped. There is no golden-image fallback or interrupted-update
recovery image in this initial implementation.

## Validation and bring-up

Host tests emulate SPI NOR and GPIO, verify page/sector boundaries and failure
handling, and inject failure at every backend operation of an update. CLI tests
cover invalid containers, sizes, arguments, and hardware-free dry runs.
The supplied SDK builds with warnings treated as errors.

Board validation is still required: confirm Flash CS numbering, power domains,
WP#/IO2 and RESET#/IO3 levels while PROGRAM_B is asserted, GPIO polarity,
reset timing, Flash ID, and successful cold boot. The tool assumes the Flash
is awake in single-SPI mode. It does not recover QPI/deep-power-down modes.
DONE/INIT_B are not MPU inputs in the supplied design; successful programming
means verified Flash bytes and reset release, not confirmed FPGA readiness.
Check the FPGA version/heartbeat through a separate UART application.

References: [board interface guide](../../docs/DAL_OBC_V110_MPU_FPGA_Design_and_Programming.md),
[Infineon S25FL128L/S25FL256L datasheet](https://www.infineon.com/assets/row/public/documents/10/49/infineon-s25fl128l-s25fl256l-128-mb-16-mb-256-mb-32-mb-fl-l-flash-spi-multi-i-o-3-datasheet-cn.pdf).
