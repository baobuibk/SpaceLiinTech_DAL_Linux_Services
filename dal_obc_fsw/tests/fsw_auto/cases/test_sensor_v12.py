"""Native sensor config and status envelopes; legacy board route is separate."""
import json
import struct
import time
from fsw_auto.fixtures import FswTestCase


class SensorModel:
    def __init__(self):
        self.bme = [2, 4, 1, 3]
        self.imu = [5, 3, 5, 3]
        self.fail = False

    def __call__(self, command):
        p = command.split()
        if command == "bme688_cfg_get":
            t, pr, h, f = self.bme
            return f"BME688 CFG source=SENSOR_READBACK t_os={t}x p_os={pr}x h_os={h}x filter={f} gas=0 heater_temp_c=320 heater_duration_ms=100"
        if command == "lsm6d_conf_get":
            a, s, g, gs = self.imu
            return f"config_valid=1\r\nWAKE_THS=0x14 WAKE_DUR=0x40 FREE_FALL=0xA3\r\neffective xl={a}/{s} gyro={g}/{gs} mode=0 event=0"
        if p and p[0] == "bme688_cfg_set":
            assert p[5:] == ["0", "320", "100"], p
            if not self.fail:
                self.bme = list(map(int, p[1:5]))
            return "BME688 CFG QUEUED id=17"
        if p and p[0] == "lsm6d_conf_accel":
            if not self.fail:
                self.imu[:2] = map(int, p[1:3])
            return "LSM6DSOX QUEUED id=18"
        if p and p[0] in ("bme688_result", "lsm6d_result"):
            return f"RESULT {'FAILED' if self.fail else 'DONE'} id={p[1]}"
        return "OK"


class SensorV12(FswTestCase):
    exp_responder = SensorModel()

    def setUp(self):
        super().setUp()
        self.exp_responder.__init__()

    def test_sensor_01_get_and_console_parity(self):
        expected = bytes.fromhex("5a11bd0394142300")
        wire = self.link.txrx(1,5,timeout=10).expect_ack(1,5,8)
        self.assertEqual(wire, expected)
        console = json.loads(self.fsw.console.run("--json", "sen_cfg", "get"))
        self.assertTrue(console["ok"])
        self.assertEqual(bytes.fromhex(console["payload_hex"]), wire)

    def test_sensor_02_set_one_sensor_and_verify_coefficients(self):
        payload = bytes([1,0,5,4,3,7,0,0])
        self.link.txrx(1,5,payload,timeout=10).expect_ack(1,5,8)
        self.assertEqual(self.exp_responder.bme, [16,8,4,127])
        self.assertEqual(self.exp_responder.imu, [5,3,5,3])
        self.link.txrx(1,5,bytes([2,0,4,2,0,0,0,0]),timeout=10).expect_ack(1,5,8)
        self.assertEqual(self.exp_responder.bme, [16,8,4,127])
        self.assertEqual(self.exp_responder.imu, [4,2,5,3])

    def test_sensor_03_reject_combined_mask_reserved_and_wrong_lengths(self):
        for payload in (bytes([3,0,1,1,1,0,0,0]), bytes([1,0,1,1,1,0,0,1]),
                        bytes([1,0,0,1,1,0,0,0]), bytes(1), bytes(7), bytes(9)):
            self.link.txrx(1,5,payload).expect_err(0)
        for payload in (b"", b"\x05", b"\x02\x00"):
            self.link.txrx(1,6,payload).expect_err(0)

    def test_sensor_04_failed_apply_is_not_successful_ack(self):
        self.exp_responder.fail = True
        self.link.txrx(1,5,bytes([1,0,2,3,1,2,0,0]),timeout=10).expect_err(1)

    def test_sensor_05_native_status_data_and_missing_sample(self):
        snapshot = self.fsw.tmp / "exp_data.json"
        snapshot.write_text(json.dumps({"online":1,"data_valid":1,"obc_timestamp":int(time.time()),
            "sensor_valid_mask":1023,"sensor_age_ms":[100]*10,"sensor_status":[2]*10,
            "bme_temp_c":26.5,"bme_press_pa":101325,"bme_hum_rh":44.5,
            "imu_mask":7,"imu_accel_mg":[-10,20,1000],"imu_gyro_dps":[-20.5,30.5,40.5],"imu_temp_c":27.0}))
        self.assertEqual(self.link.txrx(1,6,b"\x02").expect_ack(1,6,8),struct.pack("<4h",7,2650,10132,4450))
        self.assertEqual(self.link.txrx(1,6,b"\x03").expect_ack(1,6,16),struct.pack("<8h",127,2700,-10,20,1000,-20,30,40))
        snapshot.unlink()
        self.assertEqual(self.link.txrx(1,6,b"\x02").expect_ack(1,6,8),bytes.fromhex("0000008000800080"))
