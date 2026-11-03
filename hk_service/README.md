# DAL OBC housekeeping service

This project builds `hk_service` for the DAL OBC and stages the binary and
systemd unit under `service/deploy/hk_service/`.

## OBC sensor services

| Service | Hardware/source | Live output | Role |
| --- | --- | --- | --- |
| `temp_service` | SAMA7 ADC: U12 TC1047A and NTC1–4 | `/run/dal/temp/` | Owns ADC conversion and thermistor calibration. |
| `bme_service` | U9 BME688 on I2C | `/run/dal/bme688/` | Owns environmental temperature, pressure, and humidity acquisition. |
| `hk_service` | U6/U2 PAC1934 IIO, the two source services, one EXP over CAN | `/run/dal/hk_service/` | Publishes OBC and EXP snapshots; logs history. |

The RTC is a kernel time source, and the watchdog has its own feed service;
neither needs another sensor poller. The released OBC schematic has no onboard
IMU or 12 V current shunt. The BME reader now enables its gas heater and
publishes resistance when the measurement and heater stability flags are set;
[board validation](../bme_service/GAS_CHANNEL.md) is still required. The schematic review also
flags PC22 `ADC_TEMP_1` versus `ADC_TEMP1`; confirm that connection on the
board before trusting the onboard TC1047A value.

```sh
make -C service/hk_service check
make -C service/hk_service target
make -C service/hk_service deploy
```

`target` uses the OBC SDK at `/home/buildmachine/oecore-obcv110`; set
`HK_SDK_ROOT` to use another installed SDK. `make -C service deploy-hk-service`
is the top-level staging target. Copy the staged binary to
`/home/root/tools/bin/hk_service` on the OBC and install the unit in the
systemd unit directory if the service should start automatically. Staging
alone does not install it on the board. The Yocto `hk-service` recipe is
enabled in `core-image-satellite`; refresh its source archive after edits with
`make -C service package-sensor-sources`.

The OBC PAC reader uses the kernel IIO driver on Linux I2C bus 0 by default:

| Chip | Address | Channels 1–4 |
| --- | --- | --- |
| U6 | `0x1f` | `5V_SAMA7`, `5V_FPGA`, `3V3`, `5V_AI` |
| U2 | `0x10` | `PMIC_3V3`, `PMIC_DDR_1V35`, `PMIC_CORE_1V15`, `PMIC_CPU_1V15` |

`--i2c-bus`, `--pac-addr`, and `--pac-low-addr` override these defaults.
The service also consumes `temp_service` from `/run/dal/temp` and
`bme_service` from `/run/dal/bme688`. It rejects samples older than 10 seconds.
The two PACs are sampled independently. OBC CSV and `obc_data.json` contain
eight voltage readings in volts, eight current readings in amps, plus
`pac_u6_valid` and `pac_u2_valid`. `pac_valid` is true only when both chips
were read. A failed chip's four voltage/current values are zeroed and its
validity flag is false.

`obc_data.json` includes BME and ADC sample timestamps and validity flags;
consumers must check them before displaying or using values. An EXP JSON file
is overwritten with `data_valid: 0` when its board is offline or data fetching
fails. Live snapshots are atomic replacements in `/run`, avoiding flash writes.
The CSVs under `/home/bee/data` are persistent history, sampled at least
60 seconds apart by default (`--history-interval` changes this).

The EXP firmware defines one CSP node at address `11` and places sensor and
power readings in RPARAM table `2` (schema `5`). `hk_service` reads this table
into `exp_data.json` and `exp.csv`, and writes `exp_status`. The table contains
four NTC temperatures, board temperature, BME temperature/pressure/humidity,
IMU acceleration/gyro/temperature, two flow and flow-temperature channels,
and four PAC rail readings. The PAC order is `12V_MOTOR`, `12V_EXP`,
`5V_EXP`, `5V_SLN_HD`. Consumers must check `sensor_valid_mask` and
`sensor_supported_mask`; unavailable readings can be zero. The firmware's
table `4` describes actuator state and is not decoded as sensor data.

The default power switch and data-ready input names retain the OBC's
`exp1` pin labels (`pow_onoff_5v_exp1` and `sig_input_exp1`); these are pin
names, not a second EXP. Override them with `--exp-pow-pin` and
`--exp-sig-pin` if the installed board uses different wiring. Data-ready
handling remains disabled by default pending physical wiring validation.

The U2 IIO device requires the `pmic-power-monitor@10` node added to the DAL
OBC device tree. The board must boot a kernel built with that device tree to
read U2. Check for `/sys/bus/i2c/devices/0-001f/iio:device*` and
`/sys/bus/i2c/devices/0-0010/iio:device*` on the board, then inspect
`obc_data.json` and the service journal. The host test uses a synthetic IIO
tree; physical PAC readings and the inherited EXP/CAN paths still require
board validation.
