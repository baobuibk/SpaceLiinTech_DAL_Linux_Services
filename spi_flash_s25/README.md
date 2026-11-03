# S25 SPI flash tool

This project copies the DAL Yocto `spi-flash-s25` C source and host tests.
`target` runs the host tests, then uses the OBC SDK to build an ARM executable.
`deploy` stages that executable at `service/deploy/spi_flash_s25/spi_flash_s25`.

```sh
make -C service/spi_flash_s25 target
make -C service/spi_flash_s25 deploy
```

The service-level `make -C service deploy` also stages this tool. The build
uses `/home/buildmachine/oecore-obcv110` by default; set `SPI_FLASH_SDK_ROOT`
to another compatible SDK root if needed. Install the staged binary at
`/home/root/tools/bin/spi_flash_s25` on the OBC so `fpga_program` can find it.

The command accesses `/dev/spidev2.0` and the FPGA flash mux and power GPIOs.
Run `spi_flash_s25 --help` for its commands. The Yocto recipe's
`spi-flash-s25/README.txt` documents board setup, flash operations, and
hardware limitations. Host tests use emulated flash and GPIOs; they do not
program the board.
