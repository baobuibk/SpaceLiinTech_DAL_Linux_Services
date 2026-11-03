# CAT93 Microwire EEPROM tool

This project copies the DAL Yocto `spi-eeprom-cat93` C source and its host
protocol test. `target` runs the host checks and builds an ARM executable with
the OBC SDK. `deploy` stages it at
`service/deploy/spi_eeprom_cat93/spi_eeprom_cat93`.

```sh
make -C service/spi_eeprom_cat93 target
make -C service/spi_eeprom_cat93 deploy
```

The service-level `make -C service deploy` also stages this tool. The build
uses `/home/buildmachine/oecore-obcv110` by default; set `SPI_EEPROM_SDK_ROOT`
to another compatible SDK root if needed. Install the staged binary at
`/home/root/tools/bin/spi_eeprom_cat93` on the OBC.

The tool accesses the CAT93C46B via `/dev/spidev0.0`. The DAL board has ORG
grounded, so specify `--org 8` for data commands. The device tree must provide
an active-high GPIO chip select; verify that the booted image has this setting
before using the EEPROM. `spi_eeprom_cat93 --help` lists commands. The Yocto
recipe's `spi-eeprom-cat93/README.txt` describes switching, backup, and
write/erase behavior. Host tests emulate the protocol and do not access board
hardware.
