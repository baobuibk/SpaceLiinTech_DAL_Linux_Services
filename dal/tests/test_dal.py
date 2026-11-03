import json
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest


PROGRAM = pathlib.Path(sys.argv.pop(1)).resolve()


class DalCliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.source = pathlib.Path(self.tmp.name) / "obc_data.json"
        self.snapshot = {
            "board": "obc",
            "schema_version": 1,
            "online": 1,
            "obc_timestamp": int(time.time()),
            "temp_valid": 1,
            "temp_sample_unix_s": int(time.time()),
            "temp_onboard_c": 24.5,
            "temp_ntc_c": [20.0, 21.0, 22.0, "N/A"],
            "bme_valid": 1,
            "bme_sample_unix_s": int(time.time()),
            "bme_temp_c": 23.4,
            "bme_pressure_pa": 101000.0,
            "bme_humidity_rh": 55.0,
            "bme_gas_valid": 0,
            "bme_gas_resistance_ohm": None,
            "pac_valid": 1,
            "pac_sample_unix_s": int(time.time()),
            "pac_u6_valid": 1,
            "pac_u2_valid": 1,
            "pwr_rail_names": ["5V_SAMA7", "5V_FPGA", "3V3", "5V_AI",
                               "PMIC_3V3", "PMIC_DDR_1V35", "PMIC_CORE_1V15", "PMIC_CPU_1V15"],
            "pwr_volt_v": [5.0, 5.1, 3.3, 5.0, 3.3, 1.35, 1.15, 1.15],
            "pwr_curr_a": [0.2, 0.3, 0.1, 0.2, 0.1, 0.1, 0.1, 0.1],
        }
        self.source.write_text(json.dumps(self.snapshot))

    def run_dal(self, *args):
        result = subprocess.run(
            [str(PROGRAM), "--source", str(self.source), *args],
            capture_output=True, text=True, check=False,
        )
        return result.returncode, json.loads(result.stdout)

    def test_groups_and_individual_channel(self):
        rc, reply = self.run_dal("get", "obc_sensor", "environment")
        self.assertEqual(rc, 0)
        self.assertFalse(reply["stale"])
        self.assertEqual(reply["data"]["bme_temp_c"], 23.4)
        self.assertIsNone(reply["data"]["bme_gas_resistance_ohm"])
        self.assertNotIn("pwr_volt_v", reply["data"])
        rc, reply = self.run_dal("get", "obc_sensor", "pwr_volt_v")
        self.assertEqual(rc, 0)
        self.assertEqual(reply["data"]["pwr_volt_v"][:2], [5.0, 5.1])
        self.assertEqual(reply["quality"]["pac_u6_valid"], 1)
        rc, reply = self.run_dal("get", "obc_sensor", "ntc4")
        self.assertEqual(rc, 0)
        self.assertEqual(reply["data"], {"sensor": "ntc4", "value_c": "N/A"})
        rc, reply = self.run_dal("get", "obc_sensor", "pac_u2")
        self.assertEqual(rc, 0)
        self.assertEqual(reply["data"]["rail_names"][0], "PMIC_3V3")
        self.assertEqual(len(reply["data"]["voltage_v"]), 4)

    def test_discovery_and_all(self):
        rc, reply = self.run_dal("discovery", "obc")
        self.assertEqual(rc, 0)
        self.assertTrue(reply["available"])
        self.assertIn("bme_temp_c", reply["fields"])
        rc, reply = self.run_dal("telemetry", "obc")
        self.assertEqual(rc, 0)
        self.assertEqual(reply["data"], self.snapshot)
        rc, reply = self.run_dal("get", "exp")
        self.assertEqual(rc, 2)
        self.assertEqual(reply["supported_boards"], ["obc"])

    def test_stale_missing_and_malformed(self):
        self.snapshot["obc_timestamp"] = 1
        self.source.write_text(json.dumps(self.snapshot))
        rc, reply = self.run_dal("get", "obc_sensor", "temperature")
        self.assertEqual(rc, 0)
        self.assertTrue(reply["stale"])
        rc, reply = self.run_dal("get", "obc_sensor", "not_a_sensor")
        self.assertEqual(rc, 2)
        self.assertEqual(reply["error"], "UNKNOWN_SENSOR")
        self.source.write_text('{"obc_timestamp":1,"broken":[1,]}')
        rc, reply = self.run_dal("get", "obc_sensor")
        self.assertEqual(rc, 1)
        self.assertEqual(reply["error"], "SNAPSHOT_INVALID")
        self.source.unlink()
        rc, reply = self.run_dal("get", "obc_sensor")
        self.assertEqual(rc, 1)
        self.assertEqual(reply["error"], "SNAPSHOT_UNAVAILABLE")


if __name__ == "__main__":
    unittest.main()
