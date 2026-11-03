"""Exercise the CSP worker with a one-shot lite-client stand-in."""

import json
import pathlib
import time

from fsw_auto.fixtures import Fsw


ROWS = """\
0x0000 schema U16 6
0x0002 seq U32 42
0x0006 tick_ms U32 123456
0x000A supported U16 1023
0x000C valid U16 1023
0x000E status U8 2 2 2 2 2 2 2 2 2 2
0x0018 age_ms U32 1 1 1 1 1 1 1 1 1 1
0x0040 ntc_mC I32 25000 26000 27000 28000
0x0050 board_mC I32 30000
0x0054 bme_mC I32 23000
0x0058 bme_Pa U32 100000
0x005C bme_mRH U32 45000
0x0060 accel_mg I32 1 2 3
0x006C gyro_mdps I32 1000 2000 3000
0x0078 imu_mC I32 22000
0x007C flow_x100 I32 1234 5678
0x0084 flow_mC I32 10000 20000
0x008C voltage_mV U32 12000 12100 5000 5100
0x009C current_mA I32 1 2 3 4
0x00AC power_mW I32 12 24 15 20
0x00BC rail_enabled U8 1 0 1
0x00BF sample_seq U32 10 11 12 13 14 15 16 17 18 19
0x00E7 last_error U8 0 0 0 0 0 0 0 0 0 0
0x00F1 desired U8 1 1 1 1 1 1 1 1 1 1
0x00FB applied U8 1 1 1 1 1 1 1 1 1 1
0x0105 imu_mask U8 7
0x0106 bme_gas_ohm U32 0
0x010A bme_gas_valid U8 0
"""

THERMAL_ROWS = """\
0x0000 schema U16 6
0x0002 seq U32 53
0x0006 tick_ms U32 124200
0x000A state U8 5 0 8 0
0x000E enabled U8 1 0 0 0
0x0012 fault U8 0 0 3 0
0x0016 configured U8 1 1 1 1
0x001A sensors_valid U8 1 1 0 1
0x001E main_ntc U8 0 0 1 2
0x0022 sec_ntc U8 1 1 2 3
0x0026 main_mC I32 24500 25000 0 26000
0x0036 sec_mC I32 24700 25200 0 26200
0x0046 setpoint_mC I32 30000 30000 30000 30000
0x0056 request_pct U8 20 0 0 0
0x005A applied_pct U8 18 0 0 0
0x005E limit_pct U8 80 80 80 80
0x0062 main_age_ms U32 30 30 4294967295 30
0x0072 sec_age_ms U32 30 30 4294967295 30
0x0082 owner_kind U8 2 0 0 0
0x0086 owner_profile U8 0 255 255 255
0x008A owner_valid U8 1 1 1 1
0x008E pid_demand F32 20.5 0.0 0.0 0.0
0x009E output_ms U32 124000 124000 124000 124000
0x00AE config_gen U32 1 1 1 1
"""

INVENTORY_ROWS = """\
0x0000 schema U16 6
0x0002 seq U32 51
0x0006 tick_ms U32 124000
0x000A boot_id U32 9
0x000E ready U8 1
0x000F result U8 0
0x0010 pending U32 2
0x0014 published U32 7
0x0018 acknowledged U32 3
0x001C discarded U32 1
0x0020 dropped U32 0
0x0024 type_count U8 0 0 2
0x0027 capacity U8 8
0x0028 max_read U16 192
0x002A object_id U32 17
0x002E length U32 1000
0x0032 crc32 U32 305419896
0x0036 type U8 3
0x0037 source U8 1
0x0038 encoding U8 2
0x0039 width U16 32
0x003B height U16 24
0x003D run_id U32 5
0x0041 object_seq U32 11
0x0045 created_ms U32 100000
0x0049 image_count U8 0 2
"""

ICD_ROWS = """\
0x0000 schema U16 6
0x0002 seq U32 52
0x0006 tick_ms U32 124100
0x000A boot_id U32 9
0x000E layout_ver U8 1
0x000F valid_mask U8 31
0x0010 group_tick_ms U32 124000 124000 124000 124000
0x0020 thm_stat U8 10 20 30 40 50 60
0x0026 fld_stat U8 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0
0x0036 rba_stat U8 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0
0x0046 pc_stat U8 1
0x0047 build_id STR "20261101abc"
"""

IDENTITY_ROWS = """\
0x0000 schema U16 6
0x000E board STR "DAL_EXP"
0x001E firmware STR "1.0.0"
0x002E hardware STR "1.1"
0x0067 boot_id U32 9
"""


def main():
    fsw = Fsw()
    client = fsw.tmp / "fake-lite-client"
    calls = fsw.tmp / "csp_calls"
    polls = fsw.tmp / "csp_polls"
    bad_inventory = fsw.tmp / "bad_inventory"
    bad_icd = fsw.tmp / "bad_icd"
    bad_thermal = fsw.tmp / "bad_thermal"
    identity_file = fsw.tmp / "identity_rows"
    boot_file = fsw.tmp / "boot_id"
    client.write_text("#!/usr/bin/env python3\n"
                      "import sys, time\n"
                      f"ROWS = {ROWS!r}\n"
                      f"INVENTORY_ROWS = {INVENTORY_ROWS!r}\n"
                      f"THERMAL_ROWS = {THERMAL_ROWS!r}\n"
                      f"ICD_ROWS = {ICD_ROWS!r}\n"
                      f"IDENTITY_ROWS = {IDENTITY_ROWS!r}\n"
                      f"IDENTITY_FILE = {str(identity_file)!r}\n"
                      f"BOOT_FILE = {str(boot_file)!r}\n"
                      f"CALLS = {str(calls)!r}\n"
                      f"POLLS = {str(polls)!r}\n"
                      f"BAD_INVENTORY = {str(bad_inventory)!r}\n"
                      f"BAD_ICD = {str(bad_icd)!r}\n"
                      f"BAD_THERMAL = {str(bad_thermal)!r}\n"
                      "if sys.argv[2] == 'setval': open(CALLS, 'w').write(' '.join(sys.argv[3:]))\n"
                      "verb = sys.argv[2]\n"
                      "if verb == 'ping': print('reply in 1 ms')\n"
                      "elif verb == 'getall':\n"
                      "    with open(POLLS, 'a') as f: f.write(sys.argv[4] + ' ' + str(time.monotonic()) + '\\n')\n"
                      "    table = sys.argv[4]\n"
                      "    if table == '0': print(open(IDENTITY_FILE).read() if __import__('os').path.exists(IDENTITY_FILE) else IDENTITY_ROWS)\n"
                      "    elif table == '2': print(ROWS)\n"
                      "    elif table == '3': print('bad' if __import__('os').path.exists(BAD_THERMAL) else THERMAL_ROWS)\n"
                      "    elif table == '12':\n"
                      "        tick = int(time.monotonic() * 1000) & 0xffffffff\n"
                      "        rows = ICD_ROWS.splitlines()\n"
                      "        boot = open(BOOT_FILE).read().strip() if __import__('os').path.exists(BOOT_FILE) else '9'\n"
                      "        for i, row in enumerate(rows):\n"
                      "            if row.startswith('0x000A boot_id '): rows[i] = f'0x000A boot_id U32 {boot}'\n"
                      "            if row.startswith('0x0002 seq '): rows[i] = f'0x0002 seq U32 {tick}'\n"
                      "            elif row.startswith('0x0006 tick_ms '): rows[i] = f'0x0006 tick_ms U32 {tick}'\n"
                      "            elif row.startswith('0x0010 group_tick_ms '): rows[i] = '0x0010 group_tick_ms U32 ' + ' '.join([str(tick)] * 4)\n"
                      "        print('bad' if __import__('os').path.exists(BAD_ICD) else '\\n'.join(rows))\n"
                      "    elif table == '6': print('bad' if __import__('os').path.exists(BAD_INVENTORY) else INVENTORY_ROWS)\n"
                      "    else: sys.exit(1)\n"
                      "elif verb == 'setval': time.sleep(2); print('set accepted')\n"
                      "elif verb == 'getval': print('value 128')\n"
                      "elif verb == 'run': print('command accepted')\n"
                      "else: sys.exit(1)\n")
    client.chmod(0o755)
    fsw.extra_env = {"DAL_EXP_CSP_ENABLE": "1", "DAL_EXP_CSP_CLIENT": str(client)}
    try:
        fsw.start()
        snapshot = fsw.tmp / "exp_data.json"
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if snapshot.exists():
                data = json.loads(snapshot.read_text())
                if data.get("data_valid") == 1:
                    break
            time.sleep(0.05)
        else:
            raise AssertionError("CSP worker did not publish schema-6 EXP data")
        assert data["rparam_schema"] == 6 and data["sensor_status"][9] == 2
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            cached = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "6"))
            body = json.loads(cached["text"])
            if body["fresh"]:
                break
            time.sleep(0.05)
        assert cached["ok"] and body["data"]["object_id"] == 17, cached
        assert body["data"]["type_count"] == [0, 0, 2], body
        sensor_cache = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "2"))
        sensor_body = json.loads(sensor_cache["text"])
        assert sensor_body["data"]["sensor_seq"] == 42
        assert sensor_body["data"]["sensor_sample_seq"][9] == 19
        thermal = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "3"))
        thermal_body = json.loads(thermal["text"])
        assert thermal_body["fresh"] and thermal_body["data"]["state"] == [5, 0, 8, 0], thermal
        assert thermal_body["data"]["applied_pct"] == [18, 0, 0, 0], thermal
        assert thermal_body["data"]["main_mC"][0] == 24500, thermal
        tm_thermal = fsw.link.txrx(2, 0x0f, b"\x03")
        assert not tm_thermal.is_err and json.loads(tm_thermal.payload)["fresh"]
        icd = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "12"))
        icd_body = json.loads(icd["text"])
        assert icd_body["fresh"] and icd_body["data"]["build_id"] == "20261101abc", icd
        identity = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "0"))
        identity_body = json.loads(identity["text"])
        assert identity_body["fresh"] and identity_body["data"]["firmware"] == "1.0.0", identity
        info = fsw.console.run("sys_info")
        assert "EXP online hardware=1.1.0 firmware=1.0.0 build=20261101abc" in info, info
        assert "version" not in fsw.exp.commands, fsw.exp.commands
        for command, expected in (("thm_stat", bytes([10,20,30,40,50,60])),
                                  ("fld_stat", bytes([1,2]+[0]*14)),
                                  ("rba_stat", bytes([1,1,1]+[0]*13)),
                                  ("pc_stat", bytes([1]))):
            reply = json.loads(fsw.console.run("--json", command))
            assert reply["ok"] and bytes.fromhex(reply["payload_hex"]) == expected, reply
        before_uart = fsw.exp.commands.count("power_all_get")
        rail = json.loads(fsw.console.run("--json", "pwr_en", "EXP", "get"))
        assert rail["ok"] and bytes.fromhex(rail["payload_hex"]) == \
            bytes.fromhex("04 05 00 07 00 00 00"), rail
        power = json.loads(fsw.console.run("--json", "pwr_stat", "EXP", "get"))
        assert power["ok"] and power["length"] == 90, power
        records = bytes.fromhex(power["payload_hex"])[6:]
        assert [records[i * 12 + 1] for i in (4, 5, 6)] == [0x13, 0x11, 0x13], power
        assert fsw.exp.commands.count("power_all_get") == before_uart
        tm = fsw.link.txrx(2, 0x0f, b"\x06")
        assert not tm.is_err and json.loads(tm.payload)["data"]["boot_id"] == 9
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            entries = [line.split()[0] for line in polls.read_text().splitlines()]
            if entries.count("2") >= 2 and entries.count("3") >= 2 and entries.count("6") >= 2 and entries.count("12") >= 2:
                break
            time.sleep(0.05)
        assert entries.count("2") >= 2 and entries.count("3") >= 2 and entries.count("6") >= 2 and entries.count("12") >= 2, entries
        assert entries.count("0") == 1, entries
        identity_file.write_text(IDENTITY_ROWS.replace('firmware STR "1.0.0"',
                                                       'firmware STR "2.3.4"'))
        request = json.loads(fsw.console.run("--json", "exp_csp_refresh", "0"))
        assert request["ok"], request
        refresh_ticket = int.from_bytes(bytes.fromhex(request["payload_hex"]), "little")
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            result = json.loads(fsw.console.run("--json", "exp_csp_result", str(refresh_ticket)))
            if bytes.fromhex(result["payload_hex"])[0] == 3:
                break
            time.sleep(0.05)
        assert result["ok"] and "identity refreshed" in result["text"], result
        assert "firmware=2.3.4" in fsw.console.run("sys_info")
        identity_file.write_text(identity_file.read_text().replace("boot_id U32 9", "boot_id U32 10"))
        boot_file.write_text("10")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            body = json.loads(json.loads(fsw.console.run("--json", "exp_csp_snapshot", "0"))["text"])
            if body["fresh"] and body["data"]["boot_id"] == 10:
                break
            time.sleep(0.05)
        assert body["fresh"] and body["data"]["boot_id"] == 10, body
        assert "firmware=2.3.4" in fsw.console.run("sys_info")
        assert [line.split()[0] for line in polls.read_text().splitlines()].count("0") >= 3
        bad_thermal.touch()
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            body = json.loads(json.loads(fsw.console.run("--json", "exp_csp_snapshot", "3"))["text"])
            if not body["fresh"]:
                break
            time.sleep(0.05)
        assert not body["fresh"] and body["data"]["applied_pct"][0] == 18, body
        bad_thermal.unlink()
        bad_icd.touch()
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            body = json.loads(json.loads(fsw.console.run("--json", "exp_csp_snapshot", "12"))["text"])
            if not body["fresh"]:
                break
            time.sleep(0.05)
        assert not body["fresh"] and body["data"]["build_id"] == "20261101abc", body
        stale = json.loads(fsw.console.run("--json", "thm_stat"))
        assert not stale["ok"], stale
        bad_icd.unlink()
        bad_inventory.touch()
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            cached = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "6"))
            body = json.loads(cached["text"])
            if not body["fresh"]:
                break
            time.sleep(0.05)
        assert not body["fresh"] and body["data"]["object_id"] == 17, body
        bad_inventory.unlink()
        csvs = list((fsw.data_dir / "logs" / "hk").glob("HK1_*.csv"))
        assert len(csvs) == 1 and "sensor_seq" in csvs[0].read_text()

        submitted = json.loads(fsw.console.run("--json", "exp_csp_set", "5",
                                              "apply_profile", "128"))
        assert submitted["ok"] and submitted["address"] == 13, submitted
        ticket = int.from_bytes(bytes.fromhex(submitted["payload_hex"]), "little")
        start = time.monotonic()
        ping = fsw.link.txrx(0, 0)
        assert not ping.is_err and time.monotonic() - start < 1, ping

        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            result = json.loads(fsw.console.run("--json", "exp_csp_result", str(ticket)))
            if bytes.fromhex(result["payload_hex"])[0] == 3:
                assert result["ok"] and "set accepted" in result["text"], result
                break
            time.sleep(0.05)
        else:
            raise AssertionError("CSP write did not complete")
        submitted = json.loads(fsw.console.run("--json", "exp_csp_set", "5",
                                              "setpoint_mC", "25000", "25000", "25000", "25000"))
        assert submitted["ok"], submitted
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not calls.read_text().endswith(
                "setpoint_mC 25000 25000 25000 25000"):
            time.sleep(0.05)
        assert calls.read_text().endswith("setpoint_mC 25000 25000 25000 25000")
        sensor = json.loads(fsw.console.run("--json", "sen_stat", "EXP", "get", "bme"))
        assert sensor["ok"] and sensor["length"] > 10, sensor
        print("CSP worker, schema-6 snapshot, and nonblocking dispatch passed")
    finally:
        fsw.stop()


if __name__ == "__main__":
    main()
