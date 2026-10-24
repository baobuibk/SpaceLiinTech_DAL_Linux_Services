# PDU AVR bootloader client

`avr_xbld` uses the same `xbld>` command menu as the SAMV71 client. It targets
the DAL PDU ATmega128A bootloader. The AVR has one application slot; `backup`
is shown in the familiar menu but is rejected when selected.

The updated application uses a plain text CLI and Modbus RTU on USART0.
`reboot` and `flash` send the text command `bootloader confirm` by default.
That command turns outputs off, records the EEPROM boot request, and resets.
The client then sends bootloader `bhold` and `bping`. The older Modbus holding
register **40060** with **0xB007** remains available with `--entry modbus`.
The text `reset confirm` command performs a normal reboot and does not set the
boot request. Flash data travels over USART0 using bootloader xTP/XCLI. The
PDU RS-485 USART1 can request entry through Modbus, but bootloader programming
uses USART0 RS-232.

The client uses the PDU bootloader's checked Intel HEX reader, AVR image
validator and xTP implementation copied from
`service/ref/SpaceLiinTech_DAL_PDU_AVR_BOOTLOADER_HW1.1/AVRBootLoader/tools/boot_host.py`.
It accepts only packaged AVR images with the 256-byte metadata page at
`0x1DF00`, hardware ID 5 and matching image/header CRCs. Use the PDU firmware's
`make signed` output (`release/DAL_PDU_signed.hex`), not the raw application HEX.

## Requirements and deployment

The OBC image includes Python 3 and `python3-pyserial`. The PDU bootloader must
run at 115200 baud on USART0. The default OBC port is `/dev/ttyS3` (PC104
RS232_1); use `--port` if the PDU is wired to a different RS-232 port. The
PDU's known RS-232 transceiver direction issue must be corrected on hardware.

From this directory:

```sh
make check
make host      # build and check build-host/avr_xbld
make target    # build and check build-target/avr_xbld for OBC
make deploy
```

`make host` and `make target` produce self-contained Python zipapp executables
from the same source. Python source is architecture-independent, so no Yocto SDK
cross compilation is needed. The OBC supplies Python 3 and `python3-pyserial`.
`make deploy` copies `build-target/avr_xbld` to
`service/deploy/avr_xbld/avr_xbld`. Copy that one file to the OBC. The
previously staged `boot_host.py` beside it is no longer required.

## Use

```sh
./avr_xbld --port /dev/ttyS3 --baud 115200
./avr_xbld --flash latest
./avr_xbld --flash DAL_PDU_signed.hex
./avr_xbld --help
```

At `xbld>`, use `check`, `reboot`, `ping`, `erase`, `write N`, `verify`, `jump`,
`flash N`, `status`, `bootinfo`, `read`, `dump`, `xbldinfo`, `help`, or `exit`.
`flash` validates the package before opening the serial port, requests the
bootloader, checks AVR geometry, erases, writes the image and metadata, verifies
the CRC, then jumps to the application. `write` requires a prior erase.

`check`, `latest`, and a bare firmware filename use
`/home/bee/firmware/PDU` by default. `--firmware-dir` overrides that directory.
`--port` selects the OBC UART device, such as `/dev/ttyS3` or `/dev/ttyS4`.
`--baud` selects the host UART rate; the PDU application and bootloader must
use the same rate. Both PDU USART0 builds currently use 115200, so changing
only the client baud will prevent communication. `--slave` changes the Modbus
address (default 1). `--entry text` is the default; `--entry modbus` selects the
older Modbus reset route. Only `--slot 0` is supported.

Hardware programming has not been exercised on a physical PDU.
