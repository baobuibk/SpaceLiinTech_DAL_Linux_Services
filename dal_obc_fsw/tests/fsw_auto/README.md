# fsw_auto — automated tests for dal_obc_fsw

Plan and rationale: `docs/flight_sw_test_plan.md`. Implemented so far: **tier A (simulation)**.

```sh
cd service/dal_obc_fsw
make fsw-sim-test                       # builds build-host/*, runs everything (~1 min)
make fsw-sim-test FSW_TEST_ARGS="-k framing --report /tmp/r.md --junit /tmp/r.xml"
python3 -W ignore -m unittest fsw_auto.cases.test_framing   # from tests/
```

Needs only Python 3 stdlib (no pytest/pyserial). Environment knobs: `FSW_SOAK_N`,
`FSW_SOAK_P99_MS`, `FSW_FUZZ_N`, `FSW_FUZZ_SEED`.

## How it works
`fixtures.Fsw` starts `build-host/dal_obc_fsw` with every path redirected to a temp dir and the
RS-422, EXP-UART and PDU ports on PTYs. A **stub `systemctl`** is first on `PATH` because
`debug_handoff` runs `systemctl start serial-getty@ttyS1.service`. `bdal.py` is an independent
implementation of the framing/CRC; do not import `tool/pc_client` or `lib/bee_dal_proto`.

## Safety rules for new cases
- Never send `01/01 sys_rtc` with a 4-byte payload: it sets the host clock.
- The file backend is redirected with `DAL_DATA_DIR` (production default stays
  `/home/bee/data`). The framework uses `$FSW_DATA_DIR`, default `/tmp/DAL_DATA_TEST_DIR`, and
  empties it at start and stop; it refuses any directory whose name does not start with
  `DAL_DATA_TEST`. `dat_del` is therefore safe inside the sim, but still not outside it.
- The PDU port is served by `fake_pdu.FakePdu` (a Modbus RTU slave following the DAL_PDU register
  map; `pdu.online = False` silences it, each unanswered request costs ~0.5 s). The EXP port is served
  by `FakeExp`; pass an `fake_exp.ExpModel` for power/version behaviour. OBC rails are LED-class files
  under `fsw.leds`; `gui_state` GPIO is read from the real `/sys/class/leds` and is therefore `null`.

## Layout
| File | Cases |
| --- | --- |
| `cases/test_framing.py` | T-FRM-01…13 |
| `cases/test_telemetry.py` | T-PING, T-TM (reference model of the 64-byte payload in `fixtures.py`) |
| `cases/test_negative.py` | T-NEG: unimplemented sweep, length rules, seeded fuzz |
| `cases/test_data_xfer.py` | T-DAT: list, select, chunk download, marks, delete, length rules |
| `cases/test_system_power.py` | T-SYS, T-RTC, T-VOUT, T-PEN, T-PST, `gui_state` (needs `FakePdu`, `ExpModel`, LED files) |
| `cases/test_pc.py` | `pc_stat` / `pc_cfg` / `pc_ctl` |
| `cases/test_actuators.py` | manual thermal (`thm_*`), fluidic (`fld_*`) and motor (`rba_*`) commands against `fake_actuators.py` |
| `cases/test_mipi.py` | MIPI path: the real `capture_ar2020.sh` and `--gui-hw mipi-*` against stub `media-ctl`/`v4l2-ctl`/`uname`, fake LED and sysfs files |
| `cases/test_thp.py` | `thp_cfg` heater/IR PID profiles and `thp_ctl` start/stop/status, wire and console parser |
| `cases/test_oracle.py` | fakes and expectations vs the EXP/PDU firmware sources in `service/ref` (skipped if absent) |
| `cases/test_debug.py` | T-SHL, T-DBG (handoff with stubbed systemctl) |
| `cases/test_console.py` | T-CON: JSON schema, parity with wire, limits, load |
| `cases/test_inventory.py` | drift detection: dispatcher source and `help` vs `inventory.py` |

Not yet written: `sen_stat` (needs an EXP snapshot writer), EXP gateway, thermal/fluid/RBA, UVC, tiers B and C.
