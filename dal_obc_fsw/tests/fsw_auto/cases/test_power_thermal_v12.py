"""ICD v1.2 selector framing, 200-byte cap, and eight-byte thermal TM."""
import json
import struct
import time
from fsw_auto.fixtures import FswTestCase
from fsw_auto.fake_exp import ExpModel


class PowerThermalV12(FswTestCase):
    exp_responder = ExpModel()

    def exp_power_records(self, report=0):
        payload = self.link.txrx(1, 4, bytes([report])).expect_ack(1, 4)
        records = {}
        for offset in range(6, len(payload) - 16, 6):
            descriptor, mv, ma = struct.unpack_from("<HHh", payload, offset)
            if (descriptor >> 4) & 7 == 4 and descriptor & 15 < 4:
                records[descriptor & 15] = ((descriptor >> 7) & 31, mv, ma)
        return records

    def test_exp_pac_valid_when_both_flow_sensors_fail(self):
        snapshot = self.fsw.tmp / "exp_data.json"
        snapshot.write_text(json.dumps({
            "online": 1, "data_valid": 1, "obc_timestamp": int(time.time()),
            "sensor_valid_mask": 255,
            "sensor_status": [2] * 8 + [4, 4],
            "sensor_age_ms": [52] * 7 + [1, 4294967295, 4294967295],
            "pwr_volt_mv": [11449, 11437, 5006, 4992],
            "pwr_curr_ma": [19, 10, 149, 4],
        }))
        try:
            for report in (0, 1):
                with self.subTest(report=report):
                    records = self.exp_power_records(report)
                    self.assertEqual(len(records), 4)
                    for channel, (mv, ma) in enumerate(zip(
                            [11449, 11437, 5006, 4992], [19, 10, 149, 4])):
                        flags, actual_mv, actual_ma = records[channel]
                        self.assertEqual(flags & 0x1c, 0x1c)
                        self.assertEqual((actual_mv, actual_ma), (mv, ma))
            self.assertEqual(self.exp_power_records(3), {})
        finally:
            snapshot.unlink(missing_ok=True)

    def test_exp_pac_invalid_is_not_validated_by_healthy_flow(self):
        snapshot = self.fsw.tmp / "exp_data.json"
        try:
            for case in ("missing_valid_bit", "error", "old_sensor", "old_combined_age"):
                data = {
                    "online": 1, "data_valid": 1, "obc_timestamp": int(time.time()),
                    "sensor_valid_mask": 1023, "sensor_status": [2] * 10,
                    "sensor_age_ms": [1] * 10,
                    "pwr_volt_mv": [11449, 11437, 5006, 4992],
                    "pwr_curr_ma": [19, 10, 149, 4],
                }
                if case == "missing_valid_bit":
                    data["sensor_valid_mask"] &= ~(1 << 7)
                elif case == "error":
                    data["sensor_status"][7] = 4
                elif case == "old_sensor":
                    data["sensor_age_ms"][7] = 31000
                else:
                    data["obc_timestamp"] -= 20
                    data["sensor_age_ms"][7] = 15000
                snapshot.write_text(json.dumps(data))
                with self.subTest(case=case):
                    records = self.exp_power_records()
                    self.assertEqual(len(records), 4)
                    for flags, mv, ma in records.values():
                        self.assertEqual(flags & 0x0c, 0)
                        self.assertEqual((mv, ma), (0, 0))
        finally:
            snapshot.unlink(missing_ok=True)

    def test_power_v12_reports_have_bounded_word_layout(self):
        for report,count in ((0,27),(1,16),(2,13)):
            tm=self.link.txrx(1,4,bytes([report]),timeout=10)
            p=tm.expect_ack(1,4,22+6*count)
            self.assertLessEqual(len(p),200)
            schema,n,errors=struct.unpack_from("<HHH",p)
            self.assertEqual((schema,n),(0x200|report,count))
            self.assertEqual(errors&0x8000,0x8000)
            seen=set()
            for at in range(6,len(p)-16,6):
                desc,mv,ma=struct.unpack_from("<HHh",p,at)
                board,source=desc>>4&7,desc&15
                self.assertIn(board,(3,4,5));self.assertNotIn((board,source),seen)
                seen.add((board,source));self.assertEqual(desc&0xf000,0)
            cli=json.loads(self.fsw.console.run("--json","pwr_stat",("all","monitor","enable")[report]))
            self.assertTrue(cli["ok"]);self.assertEqual(cli["length"],len(p))
        p=self.link.txrx(1,4,b"\x03").expect_ack(1,4)
        self.assertLessEqual(len(p),200)
        self.assertEqual(struct.unpack_from("<H",p)[0],0x203)

    def test_power_v12_rejects_old_or_malformed_requests(self):
        for p in (b"",b"\x04",bytes(2),bytes([3,0,0]),bytes(8)):
            self.link.txrx(1,4,p).expect_err(0)
        cli=json.loads(self.fsw.console.run("--json","pwr_stat","EXP","get"))
        self.assertFalse(cli["ok"])

    def test_thermal_v12_length_reserved_and_crc_position(self):
        tm=self.link.txrx(1,0x15,timeout=10)
        p=tm.expect_ack(1,0x15,8)
        self.assertEqual(len(tm.raw),15)
        self.assertEqual(tm.raw[:5],bytes.fromhex("a5 01 15 08 00"))
        self.assertEqual(p[6:],bytes(2))
        for value in p[:6]:
            self.assertLessEqual(value&127,100)
        self.link.txrx(1,0x15,b"\x00").expect_err(0)
