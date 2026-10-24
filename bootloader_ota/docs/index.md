# Bootloader OTA manual

Use the OBC serial clients to update the EXP SAMV71 and PDU AVR without rewriting the OBC SD card. Each device needs its own packaged Intel HEX image and client.

| Device | OBC client | Default OBC port | Baud | Default image directory |
| --- | --- | --- | --- | --- |
| [EXP](exp.md) | `samv71_xbld` | `/dev/ttyS4` | 250000 | `/home/bee/firmware/EXP` |
| [PDU](pdu.md) | `avr_xbld` | `/dev/ttyS3` | 115200 | `/home/bee/firmware/PDU` |

The port defaults reflect the expected OBC wiring. Check the actual device wiring before flashing. The selected baud must match both the running application and its bootloader. Close any console or service that already has the serial device open.

From `service/`, run `make deploy` to build and stage both clients. Use `make deploy-samv71` or `make deploy-avr` to stage only one. Each executable goes under `service/deploy/<client>/<client>` on the build host. Copy the executable and packaged firmware image to the OBC; `make deploy` itself does not transfer either file to the board.

Follow the device page for image preparation, the interactive menu, and the complete flash command. A successful OBC software build does not establish that the serial wiring or bootloader works on a physical board.

## View this manual

From `service/bootloader_ota`, run `mkdocs serve` for a local preview or `mkdocs build` to generate the site. The pages are also readable directly in `docs/`.
