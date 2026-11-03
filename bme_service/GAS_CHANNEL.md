# BME688 gas resistance on the DAL OBC

`bme_service` now uses a pinned copy of the [Bosch BME68x SensorAPI](https://github.com/boschsensortec/BME68x_SensorAPI)
(commit `80ea120a8b8ac987d7d79eb68a9ed796736be845`). The original local driver
read temperature, pressure, and humidity only. The Bosch API handles the heater
calibration, gas ADC conversion, and resistance compensation for the BME688.

## Measurement sequence

`src/bme688/bme688.c` sets temperature oversampling to 2x, pressure to 4x,
humidity to 1x, and a forced-mode gas heater profile of 300 °C for 100 ms.
Each sample triggers forced mode, waits for the Bosch measurement duration plus
heater time and a margin, then asks the API for a new field. It accepts gas
resistance only when the field has both `BME68X_GASM_VALID_MSK` and
`BME68X_HEAT_STAB_MSK`, and the compensated resistance is finite and positive.
The temperature, pressure, and humidity fields remain usable if gas is invalid.

`src/main.c` sends the result to `bme_fs_write`, which publishes these files:

| Path under `/run/dal/bme688/` | Meaning |
| --- | --- |
| `bme_gas_valid` | `1` only when the heater and gas measurement are valid. |
| `bme_gas_resistance_ohm` | Raw compensated resistance in ohms; `0.00` when invalid. |
| `bme_timestamp` | Unix seconds of the latest T/P/H sample. |

`hk_service` reads those files and publishes `bme_gas_valid` and
`bme_gas_resistance_ohm` in `/run/dal/hk_service/obc_data.json`. The JSON
resistance is `null` when gas is invalid. Consumers must check both `bme_valid`
and `bme_gas_valid` before using the resistance. Gas resistance is not ppm or an
air-quality index.

## Board validation

After installing the new `bme_service` and `hk_service`, inspect:

```sh
systemctl status bme.service hk_service.service
cat /run/dal/bme688/bme_status /run/dal/bme688/bme_gas_valid
cat /run/dal/bme688/bme_gas_resistance_ohm
cat /run/dal/hk_service/obc_data.json
journalctl -u bme.service -n 50 --no-pager
```

Confirm U9's actual I2C bus/address, heater stability, and board power/current
with hardware. The current default is `/dev/i2c-0`, address `0x76`. The 300 °C,
100 ms profile follows Bosch's forced-mode example; tune it only after measuring
the board's thermal and power behavior. This code has passed host tests, SDK
cross-build, and Yocto recipe build; the physical board has not yet been tested.

The OBC CSV format has not been changed. Add gas columns using a new CSV
filename/schema if persistent gas history is needed, so existing files retain
their original column layout.
