# DAL commands and responses

The `dal` C client reads the latest OBC snapshot published by `dal_obc_fsw` at
`/run/dal/dal_obc_fsw/obc_data.json`. A query reads cached values; it does not
start a new ADC, BME, or PAC measurement. Each successful data query writes
**one JSON object on one line** to standard output.

The current client covers **OBC only**. EXP and PDU data are not implemented
in this command yet.

## Commands

| Command | Result |
| --- | --- |
| `dal discovery obc` | List supported groups, selectors, and fields in the current snapshot. |
| `dal get obc_sensor` | Return the complete OBC snapshot. |
| `dal get obc_sensor all` | Same as the preceding command. |
| `dal get obc_sensor temperature` | Onboard temperature and four NTC readings. |
| `dal get obc_sensor environment` | BME temperature, pressure, humidity, and gas resistance. |
| `dal get obc_sensor bme` | Alias for `environment`. |
| `dal get obc_sensor power` | Both OBC PAC1934 chips and their eight rails. |
| `dal get obc_sensor status` | Board, boot, time, online, and validity fields. |
| `dal get obc_sensor SENSOR` | One supported sensor selector or exact snapshot field. |
| `dal telemetry obc SELECTION` | Alias for `dal get obc_sensor SELECTION`. |
| `dal --help` | Print usage text. |

`dal discovery` and `dal discover` are equivalent. The group or field selection
is optional for `telemetry`; the default is `all`.

### Individual selectors

| Selector | `data` contents |
| --- | --- |
| `onboard_temp` | `temp_onboard_c` |
| `ntc1`, `ntc2`, `ntc3`, `ntc4` | `sensor` and one `value_c` from `temp_ntc_c` |
| `bme_gas` | `bme_gas_valid` and `bme_gas_resistance_ohm` |
| `pac_u6` | Four U6 rail names, voltages, and currents |
| `pac_u2` | Four U2 rail names, voltages, and currents |

You can also select any exact top-level field reported by `dal discovery obc`,
such as `bme_temp_c`, `temp_ntc_c`, `pwr_volt_v`, or `boot_count`.

### Options

Place options **before** the command:

| Option | Default | Purpose |
| --- | --- | --- |
| `--source PATH` | `/run/dal/dal_obc_fsw/obc_data.json` | Read another snapshot file. |
| `--max-age SECONDS` | `15` | Maximum snapshot age before `stale` becomes `true`. |
| `--help`, `-h` | — | Show usage. |

For example:

```sh
dal --max-age 30 get obc_sensor all
dal get obc_sensor ntc1
dal get obc_sensor pac_u6
dal get obc_sensor bme_gas
```

If `dal` is not in the shell's `PATH`, run `/home/root/tools/bin/dal` instead.

## Response format

A successful telemetry response has this envelope:

| Field | Meaning |
| --- | --- |
| `schema_version` | DAL client response schema; currently `1`. |
| `ok` | `true` when the requested snapshot or field was read. |
| `board` | `"obc"`. |
| `sample_time` | `obc_timestamp` in Unix seconds, or `null` if unavailable. |
| `age_s` | Whole seconds since `sample_time`, or `null` if it cannot be calculated. |
| `stale` | `true` if the age is unknown, in the future, or exceeds `--max-age`. |
| `quality` | Source validity flags for temperature, BME, gas, U6, and U2. |
| `data` | The selected fields or sensor values. |

These examples illustrate the response shape; the numbers are **not board
measurements**.

```sh
dal get obc_sensor ntc1
```

```json
{"schema_version":1,"ok":true,"board":"obc","sample_time":1790000000,"age_s":2,"stale":false,"quality":{"temp_valid":1,"bme_valid":1,"bme_gas_valid":0,"pac_u6_valid":1,"pac_u2_valid":1},"data":{"sensor":"ntc1","value_c":24.5}}
```

```sh
dal get obc_sensor bme_gas
```

```json
{"schema_version":1,"ok":true,"board":"obc","sample_time":1790000000,"age_s":2,"stale":false,"quality":{"temp_valid":1,"bme_valid":1,"bme_gas_valid":0,"pac_u6_valid":1,"pac_u2_valid":1},"data":{"bme_gas_valid":0,"bme_gas_resistance_ohm":null}}
```

`dal get obc_sensor all` places every field from `obc_data.json` inside `data`.
The `temperature`, `environment`, `power`, and `status` groups return only their
respective fields. `pac_u6` and `pac_u2` each return four matching arrays:
`rail_names`, `voltage_v`, and `current_a` (four entries per array).

Check `stale` and the matching validity flag before using a value. An invalid
gas reading is `null`. An NTC that the service identifies as not populated may
be the string `"N/A"`; it is not a number. A successful query can still have
`stale: true` because the file was readable but its measurement is old.

### Discovery

`dal discovery obc` returns `available`, `source`, `groups`, `selectors`, and
`fields`. `available` means the snapshot file exists and parses; it does **not**
guarantee that its measurements are fresh. The `fields` list comes from the
current snapshot. If the file is missing or malformed, discovery still exits
successfully with `available: false` and an empty `fields` list.

### Errors and exit status

Data query errors return a one-line JSON object and a nonzero exit status:

```json
{"schema_version":1,"ok":false,"board":"obc","error":"SNAPSHOT_UNAVAILABLE"}
```

| Error | Meaning |
| --- | --- |
| `SNAPSHOT_UNAVAILABLE` | Snapshot file could not be opened. |
| `SNAPSHOT_INVALID` | File is empty, oversized, or malformed JSON. |
| `SENSOR_UNAVAILABLE` | Requested individual selector needs fields missing from the snapshot. |
| `UNKNOWN_SENSOR` | Group, selector, or field name is unknown. |
| `UNSUPPORTED_BOARD` | Requested board is not OBC; the reply lists `supported_boards`. |

Snapshot and sensor-unavailable errors exit with status `1`; unknown selectors
and unsupported boards exit with status `2`. Invalid CLI syntax prints usage
to standard error and exits with status `2`.

The client currently reports `OUT_OF_MEMORY` with status `1` if it cannot
allocate its snapshot buffer.
