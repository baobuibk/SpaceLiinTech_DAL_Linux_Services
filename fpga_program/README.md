# FPGA flash deployment coordinator

This project copies the `fpga_program.sh` shell source from the DAL Yocto
`fpga-program` recipe. It stages an architecture-independent executable for
the DAL OBC. `target` checks the shell syntax, runs the host tests, and copies
the script to `build-target/fpga_program`. `deploy` installs that executable at
`service/deploy/fpga_program/fpga_program`.

```sh
make -C service/fpga_program target
make -C service/fpga_program deploy
```

The service-level `make -C service deploy` also stages this program. Copy the
result to `/home/root/tools/bin/fpga_program` on the OBC. It requires root,
`flock`, `sha256sum`, and the separate
`/home/root/tools/bin/spi_flash_s25` executable. Stage and install that flash
tool too; this project does not bundle it.

On the OBC, use `fpga_program check` to list `/home/bee/fpga/*.bin`, then
`fpga_program flash INDEX` to select an image. `fpga_program --bin PATH` also
accepts a direct path. Add `--power-cycle` when an FPGA power cycle is needed.
Programming writes SPI flash at address zero, verifies readback, and releases
FPGA reset. A successful exit does not prove that the FPGA configured or is
running; check the FPGA version or heartbeat separately. See the Yocto recipe's
`fpga-program/README.txt` for the full hardware sequence and exit codes.
