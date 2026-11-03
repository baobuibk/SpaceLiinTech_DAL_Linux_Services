# DAL OBC temperature service

`temp_service` reads the SAMA7 ADC channels for U12 TC1047A and four external
NTCs. It discovers the ADC IIO device by its channel attributes, so probing
U2/U6 PAC1934 cannot change which `iio:deviceN` it reads. The default output
is `/run/dal/temp/` and `hk_service` consumes it there.

```sh
make -C service/temp_service check
make -C service/temp_service deploy
```

`make -C service package-sensor-sources` refreshes the Yocto source archive.
The staged ARM binary and unit are in `service/deploy/temp_service/`. On the
board, inspect `temp_status`, `temp_timestamp`, and `temp_raw_ad*` before using
the converted temperatures. The schematic review identifies a PC22
`ADC_TEMP_1`/`ADC_TEMP1` net-name mismatch for the onboard sensor; software
cannot confirm that physical connection.
