# DAL OBC BME688 service

`bme_service` is configured to read U9 over `/dev/i2c-0` at address `0x76` and publishes
temperature, pressure, and humidity under `/run/dal/bme688` at a nominal 1 Hz
sampling rate.
It also publishes gas validity and raw gas resistance using Bosch's BME68x
SensorAPI. See [GAS_CHANNEL.md](GAS_CHANNEL.md) for the heater profile, field
meaning, and board checks. Do not interpret an invalid zero resistance as a
measurement.

```sh
make -C service/bme_service check
make -C service/bme_service deploy
```

`deploy` uses the OBC SDK at `/home/buildmachine/oecore-obcv110`; set
`BME_SDK_ROOT` for another installed SDK. It stages the ARM binary and
`bme.service` under `service/deploy/bme_service/`. To refresh the Yocto archive
after source edits, run `make -C service package-sensor-sources`. To install
all three sensor services on a running board, see
[`service/deploy/README.md`](../deploy/README.md).
