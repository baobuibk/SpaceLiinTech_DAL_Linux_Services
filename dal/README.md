# DAL console client (OBC phase)

`dal` is a C command-line client for the OBC housekeeping snapshot already
published by `hk_service`. It does not open sensor devices or trigger a new
sample. Every successful query writes exactly one JSON object to standard
output. Errors also return one JSON object and a nonzero exit status.

```sh
make -C service/dal check
make -C service/dal deploy
```

`deploy` cross-compiles with the installed OBC SDK and stages
`service/deploy/dal/dal`. Use `SDK_ROOT=/path/to/sdk` to change the SDK path.
`make -C service deploy-dal` is the top-level equivalent. The deploy-folder
installer copies the staged executable to `/home/root/tools/bin/dal`.

On the OBC:

```sh
dal discovery obc
dal get obc_sensor
dal get obc_sensor temperature
dal get obc_sensor environment
dal get obc_sensor power
dal get obc_sensor status
dal get obc_sensor ntc1
dal get obc_sensor bme_gas
dal get obc_sensor pac_u6
dal get obc_sensor bme_temp_c
dal telemetry obc all
```

`temperature` selects the onboard ADC/NTC fields; `environment` selects BME
temperature, pressure, humidity and gas; `power` selects both PAC1934 chips.
Any top-level JSON field can be requested by its exact name. The result
envelope contains the source timestamp, age, stale flag and source validity
flags. By default, data older than 15 seconds is marked stale; set
`--max-age SECONDS` to change this. An absent or malformed snapshot returns an
error object. `discovery` lists the current snapshot fields and groups, and
sets `available` to false when the snapshot cannot be read.

Individual selectors include `onboard_temp`, `ntc1` through `ntc4`, `bme_gas`,
`pac_u6`, and `pac_u2`. The PAC selectors return only that chip's four rails,
with rail names, voltages and currents in matching order. An `N/A` NTC value
is preserved from the housekeeping snapshot; inspect `quality.temp_valid`
before using temperature values.

For a host-side fixture or alternate runtime location, add
`--source /path/to/obc_data.json` before the command. The default is
`/run/dal/hk_service/obc_data.json`.

This first version covers **OBC only**. EXP and PDU selectors return
`UNSUPPORTED_BOARD`; their data paths can be added behind the same command
interface later.
