# Flash the PDU (AVR)

The PDU application accepts the plain text `bootloader confirm` command on MCU USART0. The `avr_xbld` client sends it by default, waits for the AVR bootloader, and programs the image through binary xTP/XCLI on the same USART0 RS-232 link. The RS-485 USART1 Modbus connection can request bootloader entry but is not the programming link. The PDU bootloader has **one application slot (slot 0)**. There is no second slot to flash; `backup` and `--slot 1` are unsupported.

## 1. Build a packaged firmware image

From the PDU firmware project, run:

```sh
make signed
```

This builds the application and produces `release/DAL_PDU_signed.hex`. Use that file, not `release/DAL_PDU.hex`. The package includes the AVR bootloader metadata page and CRCs; it is CRC checked, not cryptographically signed. The PDU firmware Makefile expects the AVR bootloader packager at its configured `BOOT_HOST` path. If needed, set `BOOT_HOST` to the `boot_host.py` in `SpaceLiinTech_DAL_PDU_AVR_BOOTLOADER_HW1.1/AVRBootLoader/tools`.

## 2. Build and transfer the client and image

On the Linux build host, from `service/bootloader_ota/avr_xbld`:

```sh
make check
make deploy
```

`make deploy` stages the executable Python zipapp at `service/deploy/avr_xbld/avr_xbld`. The OBC needs Python 3 and `python3-pyserial`. Create the image directory on the OBC:

```sh
mkdir -p /home/bee/firmware/PDU
```

From the Linux build host's `Linux_DAL` directory, set the OBC host name and copy both files:

```sh
OBC_HOST=dal-obc
scp service/deploy/avr_xbld/avr_xbld "root@${OBC_HOST}:/home/bee/avr_xbld"
scp service/ref/SpaceLiinTech_DAL_PDU_AVR_FW_HW1.1/release/DAL_PDU_signed.hex "root@${OBC_HOST}:/home/bee/firmware/PDU/"
```

On the OBC, run `chmod 755 /home/bee/avr_xbld` if the copied executable has lost its execute bit.

The client also accepts an absolute image path elsewhere. `--firmware-dir DIR` changes the directory scanned by `check` and `latest`.

## 3. Check the connection and select an image

On the OBC, with PDU USART0 connected to `/dev/ttyS3`:

```sh
/home/bee/avr_xbld --port /dev/ttyS3 --baud 115200
```

At the `xbld>` prompt:

```text
xbld> check
xbld> ping
```

`check` lists only valid packaged images from `/home/bee/firmware/PDU` and reports invalid HEX files. `ping` checks the application text CLI or bootloader. The PDU application and bootloader currently both use 115200 baud on USART0; changing only the client rate will break communication. Select a different `--port` if the PDU RS-232 connection uses another OBC device.

## 4. Flash and verify

Use the image number shown by `check`:

```text
xbld> flash 1
```

The client validates the package before opening the serial port. `flash` then sends `bootloader confirm`, waits for the bootloader, checks flash geometry, erases, writes the application and metadata, verifies the CRC, and jumps to the application. The PDU may turn its outputs off as it enters the bootloader.

For a one-shot update:

```sh
/home/bee/avr_xbld --port /dev/ttyS3 --baud 115200 --flash latest
```

`latest` selects the newest valid package in `/home/bee/firmware/PDU`. A specific bare filename also resolves there:

```sh
/home/bee/avr_xbld --port /dev/ttyS3 --flash DAL_PDU_signed.hex
```

## Manual steps and checks

If you need to run the stages separately, enter:

```text
xbld> reboot
xbld> erase
xbld> write 1
xbld> verify
xbld> jump
```

Run `check` before `write 1`; `write` requires a prior erase. `reboot` enters the bootloader using `bootloader confirm`. `reset confirm` is a normal application reboot and does not request the bootloader. The menu also provides `status`, `bootinfo`, `xbldinfo`, and `help`; `help` shows startup options. Only `--slot 0` is valid.

If the text entry route is unavailable but the application responds to Modbus on the same USART0 port, start the client with `--entry modbus --slave 1` (adjust the slave ID as needed). This writes `0xB007` to holding register 40060 to request bootloader entry; programming still happens on USART0 RS-232. If the bootloader does not respond, check the actual UART wiring, baud, and the known PDU RS-232 transceiver direction issue before retrying.
