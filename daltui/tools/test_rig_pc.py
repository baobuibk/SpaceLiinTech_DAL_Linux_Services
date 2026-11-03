import unittest
from types import SimpleNamespace

from rig_pc import PcModel


class PcRigTests(unittest.TestCase):
    def setUp(self):
        self.pc = PcModel()
        self.exp = SimpleNamespace(
            act=SimpleNamespace(pump_type=0),
            power={"sln_hd": False},
            publish_power=lambda: None,
        )

    def test_valve_mapping_returns_mcu_ack_and_rejects_duplicate(self):
        self.assertEqual(self.pc.handle("pc_valve_conf_channel 3 2 1 0", self.exp),
                         "OK pc_valve_conf_channel")
        self.assertEqual(self.pc.valves, [3, 2, 1, 0])
        self.assertIn("ERROR", self.pc.handle("pc_valve_conf_channel 0 0 2 3", self.exp))

    def test_activation_and_run_status(self):
        self.assertIn("ERROR", self.pc.handle("pc_activate", self.exp))
        self.exp.act.pump_type = 1
        self.assertEqual(self.pc.handle("pc_activate", self.exp), "OK pc_activate")
        self.assertEqual(self.pc.handle("pc_test a", self.exp), "pc test sent")
        self.assertIn("state      : TEST", self.pc.handle("pc_status", self.exp))
        self.assertEqual(self.pc.handle("pc_stop", self.exp), "pc stop sent")
        self.assertIn("state      : STANDBY", self.pc.handle("pc_status", self.exp))

    def test_profile_and_datalog_readback(self):
        self.assertEqual(self.pc.handle("pc_profile_set global run_mode volume", self.exp),
                         "OK pc_profile_set")
        self.assertEqual(self.pc.handle("pc_datalog_enable", self.exp), "OK pc_datalog: ON")
        self.assertIn("run_mode : VOLUME", self.pc.handle("pc_profile_show", self.exp))
        self.assertIn("datalog/resume : ON/OFF", self.pc.handle("pc_profile_show", self.exp))


if __name__ == "__main__":
    unittest.main()
