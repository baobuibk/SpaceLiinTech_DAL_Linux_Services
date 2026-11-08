"""PDF v1.2 envelopes and DAL payload definitions, using PTYs and fake devices.

Native ACKs must contain uptime, never a job ID. Rich job queries use separate
extension addresses. No clock-setting requests or production files are used.
"""
import json
import struct
import time

from fsw_auto.fake_exp import ExpModel
from fsw_auto.fixtures import FswTestCase


def control(action, channel=0, value=0):
    return struct.pack('<BBBBI', action, channel, 0, 0, value)


class IcdV12Contracts(FswTestCase):
    exp_responder = ExpModel()

    def setUp(self):
        super().setUp()
        self.exp_responder.__init__()
        self.exp_responder.power.update(exp12v=True, sln_hd=True, motor=True)
        self.exp_responder.extra['pc_activate'] = 'OK pc_activate'

    def ack_uptime(self, ty, addr, payload):
        before = struct.unpack('<I', self.link.txrx(0, 0).expect_ack(0, 0, 4))[0]
        tm = self.link.txrx(ty, addr, payload)
        uptime = struct.unpack('<I', tm.expect_ack(ty, addr, 4))[0]
        self.assertEqual(len(tm.raw), 11)
        self.assertGreaterEqual(uptime, before)
        self.assertLessEqual(uptime, before + 10)

    def test_icd12_sys_info_self_test(self):
        p = self.link.txrx(1, 0, b'\x01').expect_ack(1, 0, 96)
        self.assertEqual(p[3], 0x80 | p[2])
        self.link.txrx(1, 0, b'\x00').expect_err(0)
        self.link.txrx(1, 0, b'\x01\x00').expect_err(0)

    def test_icd12_adjustable_power_has_eight_byte_set(self):
        self.link.txrx(1, 2).expect_ack(1, 2, 8)
        self.link.txrx(1, 2, struct.pack('<H6x', 12000)).expect_ack(1, 2, 8)
        self.link.txrx(1, 2, struct.pack('<H', 12000)).expect_err(0)
        self.link.txrx(1, 2, struct.pack('<HH4x', 12000, 1)).expect_err(1)
        self.assertEqual(self.fsw.pdu.setpoint, 12000)

    def test_icd12_power_mask_changes_only_selected_rails(self):
        self.link.txrx(1, 3, struct.pack('<H', 0x2001)).expect_ack(1, 3, 2)
        self.assertEqual((self.fsw.leds / 'pow_onoff_ai/brightness').read_text(), '1\n')
        self.assertEqual((self.fsw.leds / 'pow_onoff_fpga/brightness').read_text(), '0\n')
        p = self.link.txrx(1, 3).expect_ack(1, 3, 2)
        word, = struct.unpack('<H', p)
        self.assertTrue(word & 1)
        self.assertTrue(word & (1 << 13))  # OBC board readback valid
        self.link.txrx(1, 3, struct.pack('<H', 1)).expect_ack(1, 3, 2)
        self.link.txrx(1, 3, b'\x03').expect_err(0)
        self.link.txrx(1, 3, bytes(6)).expect_err(0)
        self.link.txrx(1, 3, bytes(2)).expect_err(1)

    def test_icd12_all_sensor_selectors_and_invalid_sentinels(self):
        for selector, size in [(0, 58), (1, 24), (2, 8), (3, 16), (4, 10)]:
            with self.subTest(selector=selector):
                p = self.link.txrx(1, 6, bytes([selector])).expect_ack(1, 6, size)
                self.assertLessEqual(len(p), 200)
                if selector in (2, 3, 4):
                    # No EXP snapshot: unknown temperatures are not encoded as 0 C.
                    self.assertEqual(p[:2], b'\0\0')
                    self.assertEqual(p[2:], b'\0\x80' * ((size - 2) // 2))
        self.link.txrx(1, 6, b'\x05').expect_err(0)

    def test_icd12_camera_empty_get_and_aggregate_status(self):
        p = self.link.txrx(1, 0x10).expect_ack(1, 0x10, 32)
        self.assertEqual(p[0], 1)
        self.link.txrx(1, 0x10, b'\x02').expect_err(0)
        self.link.txrx(1, 0x12, bytes(5)).expect_err(0)
        request_id = 78120
        self.ack_uptime(1, 0x11, control(2, 1, request_id))
        deadline = time.monotonic() + 3
        while True:
            p = self.link.txrx(1, 0x12).expect_ack(1, 0x12, 6)
            states, errors, known = struct.unpack('<HHH', p)
            if (states >> 4) & 15 == 3:
                break
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.02)
        self.assertEqual(errors, 0)
        self.assertTrue(known & 2)
        self.assertTrue((self.fsw.tmp / f'uvc/USB1_{request_id:08}.jpg').exists())
        detail = self.link.txrx(1, 0x31, struct.pack('<BI', 2, request_id)).expect_ack(1, 0x31, 8)
        self.assertEqual(detail[1], 3)

    def test_icd12_fluid_motor_and_experiment_ack_are_uptime(self):
        self.ack_uptime(1, 0x17, bytes([0, 0, 0, 5, 0, 0, 0, 0]))
        self.ack_uptime(1, 0x1a, control(1, 2))
        self.ack_uptime(1, 0x1a, control(6, 2))
        self.ack_uptime(2, 2, control(1))
        self.link.txrx(1, 0x1a, control(8, 2, 100)).expect_err(1)

    def test_icd12_profile_native_get_and_selected_extension(self):
        p = self.link.txrx(2, 3).expect_ack(2, 3, 32)
        self.assertEqual(p[:3], b'\0\0\0')
        self.link.txrx(2, 3, bytes(32)).expect_err(1)
        p = self.link.txrx(2, 0x10, bytes([0, 0, 2]) + bytes(29)).expect_ack(2, 0x10, 32)
        self.assertEqual(p[:3], b'\0\0\2')
        self.link.txrx(2, 4).expect_ack(2, 4, 1)

    def test_icd12_console_native_and_extension_routes(self):
        for command, ty, addr, length in [
            ('pwr_en get', 1, 3, 2),
            ('pwr_en OBC get', 1, 0x32, 7),
            ('uvc_cfg get', 1, 0x10, 32),
            ('uvc_cfg 2 get', 1, 0x30, 32),
            ('uvc_stat', 1, 0x12, 6),
            ('thp_cfg get', 2, 3, 32),
            ('thp_cfg heater get 2', 2, 0x10, 32),
            ('rba_ctl init', 1, 0x1a, 4),
            ('rba_job init', 1, 0x33, 4),
        ]:
            with self.subTest(command=command):
                reply = json.loads(self.fsw.console.run('--json', *command.split()))
                self.assertTrue(reply['ok'], reply)
                self.assertEqual((reply['type'], reply['address'], reply['length']), (ty, addr, length))

    def test_icd12_rejects_non_pdf_lengths_for_every_native_handler(self):
        addresses = {0: [0], 1: [0, 1, 2, 3, 4, 5, 6, 0x10, 0x11, 0x12,
                                 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1a, 0x1b],
                     2: list(range(5)), 3: list(range(6)), 4: [0]}
        # No native TC in these PDF tables has LENGTH 3.
        for ty, commands in addresses.items():
            for addr in commands:
                with self.subTest(type=ty, addr=addr):
                    self.link.txrx(ty, addr, bytes(3)).expect_err(0)
