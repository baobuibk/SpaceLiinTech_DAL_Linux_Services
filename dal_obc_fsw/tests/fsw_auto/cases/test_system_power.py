"""T-SYS / T-VOUT / T-PEN / T-PST: sys_info, PDU VOUT, power enable and status (tier A).

Backed by FakePdu (Modbus slave following the DAL_PDU register map), ExpModel (EXP CLI) and
fake LED-class files for the OBC rails. Peripheral facts: docs/flight_sw_test_plan.md section 2.4.
"""
import json
import re
import struct
import time
import unittest

from fsw_auto import fixtures
from fsw_auto.fake_exp import ExpModel
from fsw_auto.fixtures import FswTestCase

SYS_INFO, SYS_RTC, VOUT, PEN, PST = 0x00, 0x01, 0x02, 0x03, 0x04


class RtcSandbox(unittest.TestCase):
    def test_set_and_read_without_changing_host_clock(self):
        fsw = fixtures.Fsw(extra_env={"DAL_OBC_RTC_SIMULATE": "1"}).start()
        try:
            host_before = time.time()
            epoch = 1_700_000_000
            reply = json.loads(fsw.console.run("--json", "sys_rtc", str(epoch)))
            self.assertTrue(reply["ok"], reply)
            self.assertEqual(bytes.fromhex(reply["payload_hex"]), struct.pack("<I", epoch))
            readback = fsw.link.txrx(1, SYS_RTC).expect_ack(1, SYS_RTC, 4)
            self.assertAlmostEqual(struct.unpack("<I", readback)[0], epoch, delta=2)
            self.assertAlmostEqual(time.time(), host_before, delta=5)
        finally:
            fsw.stop()
OBC, EXP, PDU = 3, 4, 5

# gui_state.h
TRANS_ON, ENABLED, SET_VALID, VOLT_VALID, CURR_VALID = 0x01, 0x02, 0x04, 0x08, 0x10
# power_stat.c
CONTROL_KNOWN, CONTROL_ON, VOLT_OK, CURR_OK, FRESH = 0x01, 0x02, 0x04, 0x08, 0x10

CONFIG = (fixtures.ROOT / "include/dal_obc_config.h").read_text()


def config(name):
    return int(re.search(rf"#define {name}\s+(\d+)", CONFIG).group(1))


def records(payload):
    return [payload[12 + 28 * i: 40 + 28 * i] for i in range(3)]


class Base(FswTestCase):
    exp_responder = ExpModel()

    def setUp(self):
        super().setUp()
        self.fsw.pdu.reset()
        self.fsw.pdu.online = True
        self.fsw.pdu.requests.clear()
        model = self.exp_responder
        model.__init__()
        for led in self.fsw.leds.iterdir():
            (led / "brightness").write_text("0\n")

    def led(self, name):
        return (self.fsw.leds / name / "brightness").read_text().strip()


class SysInfo(Base):
    def test_sys_01_layout_and_online_boards(self):
        p = self.link.txrx(1, SYS_INFO).expect_ack(1, SYS_INFO, 96)
        self.assertEqual(p[0:2], bytes((1, 3)))
        self.assertEqual(p[2], 0b111)                      # PDU, EXP, OBC online
        self.assertEqual((p[3], p[8:12]), (0, bytes(4)))   # reserved fields
        self.assertAlmostEqual(struct.unpack_from("<I", p, 4)[0], time.time(), delta=5)
        pdu, exp, obc = records(p)
        self.assertEqual(pdu[:8], bytes((1, 1, 1, 0, 0, 0, 1, 0)))     # hw 1.0.0, fw 0.1.0
        self.assertEqual(pdu[8:28].rstrip(b"\0"), b"01A2B3C4")        # 8 hex digits of the build
        self.assertEqual(exp[:8], bytes((2, 1, 1, 0, 0, 1, 2, 3)))
        self.assertEqual(exp[8:28].rstrip(b"\0"), b"4dc7bbb-dirty")
        self.assertEqual(obc[:8], bytes((3, 1, config("DAL_OBC_HW_VERSION_MAJOR"),
                                         config("DAL_OBC_HW_VERSION_MINOR"),
                                         config("DAL_OBC_HW_VERSION_PATCH"),
                                         config("DAL_OBC_VERSION_MAJOR"),
                                         config("DAL_OBC_VERSION_MINOR"),
                                         config("DAL_OBC_VERSION_PATCH"))))
        self.assertEqual(obc[8:], bytes(20))

    def test_sys_02_payload_not_allowed(self):
        self.link.txrx(1, SYS_INFO, b"\0").expect_err(0)

    def test_sys_03_pdu_offline(self):
        self.fsw.pdu.online = False
        p = self.link.txrx(1, SYS_INFO, timeout=10).expect_ack(1, SYS_INFO, 96)
        pdu = records(p)[0]
        self.assertEqual(p[2], 0b110)
        self.assertEqual(pdu[:8], bytes((1, 0, 1, 0, 0, 0xFF, 0xFF, 0xFF)))
        self.assertEqual(pdu[8:], bytes(20))

    def test_sys_04_exp_offline_or_unparsable(self):
        for line in (None, "DAL_EXP firmware v999.0.0 (hw110) build x", "garbage"):
            with self.subTest(line):
                self.exp_responder.version_line = line
                p = self.link.txrx(1, SYS_INFO, timeout=10).expect_ack(1, SYS_INFO, 96)
                exp = records(p)[1]
                self.assertEqual((p[2], exp[1], exp[5:8]), (0b101, 0, bytes((255, 255, 255))))
                self.assertEqual(exp[8:], bytes(20))

    def test_sys_05_pdu_identity_must_match(self):
        self.fsw.pdu.version, self.fsw.pdu.patch = 0x0203, 7
        p = self.link.txrx(1, SYS_INFO).expect_ack(1, SYS_INFO, 96)
        self.assertEqual(records(p)[0][5:8], bytes((2, 3, 7)))
        self.fsw.pdu.patch = 0x0100                       # patch >255 is rejected by the OBC
        p = self.link.txrx(1, SYS_INFO).expect_ack(1, SYS_INFO, 96)
        self.assertEqual((p[2] & 1, records(p)[0][1]), (0, 0))


class Rtc(Base):
    def test_rtc_01_get(self):
        p = self.link.txrx(1, SYS_RTC).expect_ack(1, SYS_RTC, 4)
        self.assertAlmostEqual(struct.unpack("<I", p)[0], time.time(), delta=3)

    def test_rtc_03_bad_lengths(self):
        # never a 4-byte payload here: that sets the host clock
        for n in (1, 2, 3, 5, 8):
            self.link.txrx(1, SYS_RTC, bytes(n)).expect_err(0)


class Vout(Base):
    def vout(self, payload=b""):
        return self.link.txrx(1, VOUT, payload)

    def fields(self, tm):
        return struct.unpack("<HHHH", tm.expect_ack(1, VOUT, 8))

    def test_vout_01_get_before_any_setpoint(self):
        self.assertEqual(self.fields(self.vout()), (0xFFFF, 0, 0, VOLT_VALID | CURR_VALID))

    def test_vout_02_set_programs_pdu_and_reads_back(self):
        setpoint, mv, ma, flags = self.fields(self.vout(struct.pack("<H", 5000)))
        self.assertEqual((setpoint, mv, ma), (5000, 0, 0))                     # VOUT output still off
        self.assertEqual(flags, TRANS_ON | SET_VALID | VOLT_VALID | CURR_VALID)  # TRANS powered first
        self.assertEqual([w for w in self.fsw.pdu.writes()], [(6, 0, 5000)])

    def test_vout_03_range(self):
        for mv, ok in ((799, False), (800, True), (20000, True), (20001, False)):
            with self.subTest(mv):
                tm = self.vout(struct.pack("<H", mv))
                tm.expect_ack(1, VOUT, 8) if ok else tm.expect_err(1)
        self.assertEqual([w[2] for w in self.fsw.pdu.writes()], [800, 20000])  # rejects never reach the PDU

    def test_vout_04_length_rules(self):
        for n in (1, 3, 4):
            self.vout(bytes(n)).expect_err(0)

    def test_vout_05_pdu_down(self):
        self.fsw.pdu.online = False
        self.link.txrx(1, VOUT, timeout=10).expect_err(3)
        self.link.txrx(1, VOUT, struct.pack("<H", 5000), timeout=10).expect_err(3)

    def test_vout_06_pdu_rejects_value(self):
        # OBC accepts 800..20000, so make the PDU the one that refuses
        import fsw_auto.fake_pdu as fp
        old = fp.SETPOINT_RANGE
        fp.SETPOINT_RANGE = (6000, 7000)
        try:
            self.link.txrx(1, VOUT, struct.pack("<H", 5000)).expect_err(3)
        finally:
            fp.SETPOINT_RANGE = old

    def test_vout_07_identity_is_checked_before_a_write(self):
        self.fsw.pdu.build = 0
        self.fsw.pdu.requests.clear()
        self.link.txrx(1, VOUT, struct.pack("<H", 5000)).expect_ack(1, VOUT, 8)
        self.assertEqual(self.fsw.pdu.requests[0], (3, 99, 2))                  # identity first


class PowerEnable(Base):
    def pen(self, board, select=None, on=0, flags=0, timeout=10):
        payload = bytes((board,)) if select is None else struct.pack("<BHHB", board, select, on, flags)
        tm = self.link.txrx(1, PEN, payload, timeout=timeout)
        return tm

    def state(self, tm):
        return struct.unpack("<BHHH", tm.expect_ack(1, PEN, 7))

    def test_pen_01_get_all_boards(self):
        self.assertEqual(self.state(self.pen(OBC)), (OBC, 0, 0xFF, 0))
        self.assertEqual(self.state(self.pen(PDU)), (PDU, 0, 3, 0))
        self.assertEqual(self.state(self.pen(EXP)), (EXP, 0, 7, 0))

    def test_pen_02_bad_boards_and_lengths(self):
        for board in (0, 1, 2, 6, 255):
            self.pen(board).expect_err(1)
        for n in (0, 2, 5, 7):
            self.link.txrx(1, PEN, bytes((OBC,)) + bytes(max(n - 1, 0)) if n else b"").expect_err(0)

    def test_pen_03_select_rules(self):
        self.pen(OBC, 0, 0).expect_err(1)                  # empty select
        self.pen(OBC, 0x100, 0).expect_err(1)              # beyond the 8 OBC outputs
        self.pen(OBC, 1, 2).expect_err(1)                  # on-bit outside select
        self.pen(OBC, 1, 1, flags=1).expect_err(1)         # force is EXP-only
        self.pen(EXP, 1, 1, flags=1).expect_err(1)         # force only with off
        self.pen(EXP, 8, 0).expect_err(1)
        self.pen(PDU, 4, 0).expect_err(1)
        self.pen(PDU, 3, 1).expect_err(1)                  # VOUT on with TRANS off is impossible
        self.assertEqual(self.fsw.pdu.writes(), [])

    def test_pen_04_every_obc_output_switches_its_led(self):
        names = ["pow_onoff_ai", "pow_onoff_fpga", "usb_1_pwr_en", "usb_2_pwr_en",
                 "usb_3_pwr_en", "usb_4_pwr_en", "pow_mipicam_1", "pow_mipicam_2"]
        for bit, name in enumerate(names):
            with self.subTest(name):
                self.assertEqual(self.state(self.pen(OBC, 1 << bit, 1 << bit)), (OBC, 1 << bit, 0xFF, 0))
                self.assertEqual(self.led(name), "1")
                self.assertEqual(sum(self.led(n) == "1" for n in names), 1)
                self.assertEqual(self.state(self.pen(OBC, 1 << bit, 0)), (OBC, 0, 0xFF, 0))
                self.assertEqual(self.led(name), "0")

    def test_pen_05_camera_off_also_disables_its_i2c_switch(self):
        (self.fsw.leds / "en_i2c_mipicam_2" / "brightness").write_text("1\n")
        self.pen(OBC, 0x80, 0x80)
        self.assertEqual(self.led("en_i2c_mipicam_2"), "1")   # power-on leaves the I2C switch alone
        self.pen(OBC, 0x80, 0)
        self.assertEqual((self.led("pow_mipicam_2"), self.led("en_i2c_mipicam_2")), ("0", "0"))

    def test_pen_06_obc_multiple_outputs_in_one_request(self):
        self.assertEqual(self.state(self.pen(OBC, 0x3F, 0x15)), (OBC, 0x15, 0xFF, 0))
        self.assertEqual(self.state(self.pen(OBC, 0xFF, 0)), (OBC, 0, 0xFF, 0))

    def test_pen_07_obc_led_missing_is_reported_as_failed(self):
        led = self.fsw.leds / "usb_3_pwr_en"            # bit 4; removing the directory, not just the
        (led / "brightness").unlink()                   # file: the OBC would re-create a bare file
        led.rmdir()
        try:
            board, on, known, failed = self.state(self.pen(OBC, 0x10, 0x10))
            self.assertEqual((known, failed), (0xFF & ~0x10, 0x10))
        finally:
            led.mkdir()
            (led / "brightness").write_text("0\n")

    def test_pen_10_pdu_vout_needs_a_setpoint(self):
        self.assertEqual(self.state(self.pen(PDU, 1, 1)), (PDU, 0, 3, 1))     # PDU refuses, bit 0 failed

    def test_pen_11_pdu_sequence(self):
        self.link.txrx(1, VOUT, struct.pack("<H", 5000)).expect_ack(1, VOUT, 8)
        self.assertEqual(self.state(self.pen(PDU, 1, 1)), (PDU, 3, 3, 0))     # VOUT on pulled TRANS on
        self.assertEqual(self.state(self.pen(PDU, 2, 0)), (PDU, 0, 3, 0))     # TRANS off dropped VOUT too
        self.assertEqual(self.state(self.pen(PDU, 3, 3)), (PDU, 3, 3, 0))
        self.assertEqual(self.state(self.pen(PDU, 1, 0)), (PDU, 2, 3, 0))     # VOUT off, TRANS stays

    def test_pen_12_pdu_down_is_a_hardware_error(self):
        self.fsw.pdu.online = False
        self.pen(PDU).expect_err(3)
        self.pen(PDU, 2, 2).expect_err(3)

    def test_pdu_fault_clear_console_and_rs422(self):
        self.fsw.pdu.latched_fault = True
        self.link.txrx(1, 0x1c, b"\x01").expect_err(0)
        self.assertTrue(self.fsw.pdu.latched_fault)
        self.fsw.pdu.live_fault = True
        self.link.txrx(1, 0x1c, b"").expect_err(3)
        self.assertTrue(self.fsw.pdu.latched_fault)
        self.fsw.pdu.live_fault = False
        reply = json.loads(self.fsw.console.run("--json", "pdu_fault_clear"))
        self.assertTrue(reply["ok"], reply)
        self.assertEqual(reply["payload_hex"], "00")
        self.assertIn((5, 2, 0xFF00), self.fsw.pdu.writes())
        self.assertFalse(self.fsw.pdu.latched_fault)
        self.assertEqual(self.link.txrx(1, 0x1c, b"").expect_ack(1, 0x1c, 1), b"\x00")

    def test_pdu_heartbeat_console_and_rs422(self):
        def state(payload):
            self.assertEqual(len(payload), 9)
            return struct.unpack("<BII", payload)

        self.assertEqual(state(self.link.txrx(1, 0x1d, b"").expect_ack(1, 0x1d, 9)),
                         (1, 1000, 1000))
        self.link.txrx(1, 0x1d, b"\x02").expect_err(0)
        self.link.txrx(1, 0x1d, struct.pack("<BI", 2, 0)).expect_err(1)
        self.assertEqual(state(self.link.txrx(1, 0x1d, struct.pack("<BI", 2, 1500))
                               .expect_ack(1, 0x1d, 9)), (1, 1500, 1000))
        response = json.loads(self.fsw.console.run("--json", "pdu_heartbeat", "off", "750"))
        self.assertTrue(response["ok"], response)
        self.assertEqual(state(bytes.fromhex(response["payload_hex"])), (1, 1500, 750))
        response = json.loads(self.fsw.console.run("--json", "pdu_heartbeat", "enable", "off"))
        self.assertTrue(response["ok"], response)
        self.assertEqual(state(bytes.fromhex(response["payload_hex"])), (0, 1500, 750))
        self.assertEqual(self.fsw.pdu.heartbeat_on_ms, 1500)
        self.assertEqual(self.fsw.pdu.heartbeat_off_ms, 750)
        self.assertIn((16, 19, 2), self.fsw.pdu.writes())
        self.assertIn((16, 21, 2), self.fsw.pdu.writes())

    def test_pdu_bootloader_accepts_key_and_then_goes_offline(self):
        self.link.txrx(1, 0x1e, b"\x01").expect_err(0)
        response = json.loads(self.fsw.console.run("--json", "pdu_bootloader", "enter"))
        self.assertTrue(response["ok"], response)
        self.assertEqual(response["payload_hex"], "01")
        self.assertIn((6, 59, 0xB007), self.fsw.pdu.writes())
        self.assertTrue(self.fsw.pdu.bootloader_requested)
        self.assertFalse(self.fsw.pdu.online)

    def test_pen_20_exp_switching_and_force(self):
        model = self.exp_responder
        self.assertEqual(self.state(self.pen(EXP, 1, 1)), (EXP, 1, 7, 0))
        self.assertEqual(self.state(self.pen(EXP, 7, 7)), (EXP, 7, 7, 0))
        model.refuse_normal_off = {"motor"}
        self.assertEqual(self.state(self.pen(EXP, 4, 0)), (EXP, 7, 7, 4))     # refused: motor stays on, bit 2 failed
        self.assertEqual(self.state(self.pen(EXP, 4, 0, flags=1)), (EXP, 3, 7, 0))
        self.assertIn("power_motor_off force", self.fsw.exp.commands)

    def test_pen_21_exp_unreachable(self):
        self.exp_responder.extra["power_all_get"] = ""
        self.pen(EXP).expect_err(3)

    def test_pen_22_exp_readback_clears_missing_uart_ack(self):
        self.assertEqual(self.state(self.pen(EXP, 4, 4)), (EXP, 4, 7, 0))
        model = self.exp_responder
        def missing_ack(command):
            if command == "power_motor_off":
                model.power["motor"] = False
                return "power_motor_off"  # command applied, expected OK line lost
            return model(command)
        self.fsw.exp.responder = missing_ack
        self.assertEqual(self.state(self.pen(EXP, 4, 0)), (EXP, 0, 7, 0))

    def test_pen_23_exp_all_uses_one_mcu_command(self):
        before = len(self.fsw.exp.commands)
        self.assertEqual(self.state(self.pen(EXP, 7, 7)), (EXP, 7, 7, 0))
        self.assertEqual(self.fsw.exp.commands[before:],
                         ["power_all_on", "power_all_get"])
        self.exp_responder.refuse_normal_off = {"motor"}
        before = len(self.fsw.exp.commands)
        self.assertEqual(self.state(self.pen(EXP, 7, 0)), (EXP, 4, 7, 4))
        self.assertEqual(self.fsw.exp.commands[before:],
                         ["power_all_off", "power_all_get"])
        before = len(self.fsw.exp.commands)
        self.assertEqual(self.state(self.pen(EXP, 7, 0, flags=1)), (EXP, 0, 7, 0))
        self.assertEqual(self.fsw.exp.commands[before:],
                         ["power_all_off force", "power_all_get"])

    def test_exp_run_text_console_omits_duplicate_hex(self):
        text = self.fsw.console.run("exp_run", "power_all_get")
        self.assertIn("Power 12V_EXP requested=OFF", text)
        self.assertNotIn("payload:", text)
        reply = json.loads(self.fsw.console.run("--json", "exp_run", "power_all_get"))
        self.assertTrue(reply["ok"], reply)
        self.assertTrue(reply["payload_hex"])


class PowerStatus(Base):
    def stat(self, board, mask=0, timeout=10):
        payload = struct.pack("<BH", board, mask)
        p = self.link.txrx(1, PST, payload, timeout=timeout).expect_ack(1, PST)
        board_id, count, epoch = struct.unpack_from("<BBI", p)
        self.assertEqual(len(p), 6 + 12 * count)
        recs = [struct.unpack_from("<BBHIi", p, 6 + 12 * i) for i in range(count)]
        return board_id, epoch, recs

    def test_pst_01_pdu_values_and_flags(self):
        pdu = self.fsw.pdu
        pdu.setpoint, pdu.vout_en, pdu.trans_en = 5000, True, True
        board, epoch, recs = self.stat(PDU)
        self.assertEqual(board, PDU)
        self.assertAlmostEqual(epoch, time.time(), delta=5)
        by = {r[0]: r for r in recs}
        self.assertEqual(sorted(by), [0, 1, 2, 3])
        self.assertEqual(by[0][1], CONTROL_KNOWN | CONTROL_ON | VOLT_OK | CURR_OK | FRESH)
        self.assertEqual((by[0][3], by[0][4]), (5000, pdu.vout_ma))
        self.assertEqual((by[1][3], by[1][4]), (4980, 300))
        self.assertEqual(by[2][1], VOLT_OK | FRESH)                      # INPUT_12V: voltage only
        self.assertEqual(by[2][3], 11900)
        self.assertEqual(by[3][3], 5010)

    def test_pst_02_unavailable_current_clears_its_flag(self):
        self.fsw.pdu.vout_ma = 0xFFFF
        by = {r[0]: r for r in self.stat(PDU)[2]}
        self.assertEqual(by[0][1] & CURR_OK, 0)
        self.assertEqual(by[0][1] & VOLT_OK, VOLT_OK)

    def test_pst_03_mask_selects_records(self):
        _, _, recs = self.stat(PDU, 0b0101)
        self.assertEqual([r[0] for r in recs], [0, 2])
        _, _, recs = self.stat(PDU, 0b1000)
        self.assertEqual([r[0] for r in recs], [3])

    def test_pst_04_invalid_board_or_mask(self):
        for board, mask in ((0, 0), (1, 0), (6, 0), (PDU, 0x10), (EXP, 0x80)):
            with self.subTest((board, mask)):
                self.link.txrx(1, PST, struct.pack("<BH", board, mask)).expect_err(1)
        for n in (0, 1, 2, 4):
            self.link.txrx(1, PST, bytes(n)).expect_err(0)

    def test_pst_05_pdu_down_reports_everything_unavailable(self):
        self.fsw.pdu.online = False
        board, epoch, recs = self.stat(PDU)
        self.assertEqual(epoch, 0)
        for rec in recs:
            self.assertEqual((rec[1], rec[2]), (0, 0xFFFF))

    def test_pst_10_obc_rails_and_controls(self):
        self.fsw.write_snapshot()
        self.fsw.wait_tm_obc()
        self.fsw.write_snapshot(pac_u2_valid=0)
        deadline = time.monotonic() + 5
        while True:
            _, _, recs = self.stat(OBC)
            if recs[4][1] == 0:
                break
            self.assertLess(time.monotonic(), deadline, "cache never saw pac_u2_valid=0")
            time.sleep(0.3)
        self.assertEqual(len(recs), 16)
        self.assertEqual(recs[0][1], VOLT_OK | CURR_OK | FRESH)
        self.assertEqual((recs[0][3], recs[0][4]), (5010, -125))
        self.assertEqual(recs[1][3:5], (5000, 200))
        for i in range(4, 8):                                    # U2 invalid: unavailable, zeroed
            self.assertEqual((recs[i][1], recs[i][2], recs[i][3], recs[i][4]), (0, 0xFFFF, 0, 0))
        for i in range(8, 16):
            self.assertEqual((recs[i][1], recs[i][2]), (CONTROL_KNOWN | FRESH, 0))
        self.link.txrx(1, PEN, struct.pack("<BHHB", OBC, 0x02, 0x02, 0)).expect_ack(1, PEN, 7)
        _, _, recs = self.stat(OBC, 1 << 9)
        self.assertEqual(recs[0][:2], (9, CONTROL_KNOWN | CONTROL_ON | FRESH))

    def test_pst_11_exp_controls_without_pac_data(self):
        self.exp_responder.power["motor"] = True
        _, epoch, recs = self.stat(EXP)
        self.assertEqual(epoch, 0)
        self.assertEqual(len(recs), 7)
        self.assertEqual(recs[6][1], CONTROL_KNOWN | CONTROL_ON | FRESH)
        self.assertEqual(recs[4][1], CONTROL_KNOWN | FRESH)
        self.assertEqual(recs[0][1], 0)

    def test_pst_12_pdu_poll_does_not_interfere(self):
        # the background gui_state poller shares the Modbus port; hammer both paths
        for _ in range(20):
            self.stat(PDU)
            self.link.txrx(1, VOUT).expect_ack(1, VOUT, 8)
        self.assertFalse([r for r in self.fsw.pdu.requests if r[0] in (5, 6)])


class GuiState(Base):
    def snapshot(self, wait_for_pdu=True):
        import json
        deadline = time.monotonic() + 9      # the PDU poller runs every 5 s
        while True:
            data = json.loads(self.fsw.console.run("gui_state"))
            if not wait_for_pdu or data["pdu"] or time.monotonic() > deadline:
                return data
            time.sleep(0.5)

    def test_con_07_gui_state_keys_and_pdu_block(self):
        self.fsw.pdu.setpoint, self.fsw.pdu.trans_en = 7000, True
        deadline = time.monotonic() + 12                  # the poller refreshes every 5 s
        while True:
            data = self.snapshot()
            if data["pdu"] and data["pdu"]["vout_set_mv"] == 7000:
                break
            self.assertLess(time.monotonic(), deadline, "gui_state never showed the new setpoint")
            time.sleep(0.5)
        self.assertEqual(set(data), {"ok", "obc", "exp", "exp_uart", "pdu", "gpio", "devices",
                                     "rtc", "mipi", "camera_led_duty_pct"})
        pdu = data["pdu"]
        self.assertEqual((pdu["board"], pdu["mode"], pdu["board_temp_c"]), ("pdu", "NORMAL", 25.1))
        self.assertEqual((pdu["vout_set_mv"], pdu["trans_on"], pdu["vout_on"]), (7000, True, False))
        self.assertEqual((pdu["input_12v_mv"], pdu["input_5v_mv"]), (11900, 5010))
        self.assertIs(data["exp"], None)                       # no EXP snapshot file in the sim
        # gpio reads the hard-coded /sys/class/leds (not DAL_GUI_LED_ROOT): unreadable here -> null
        self.assertIs(data["gpio"]["pow_onoff_ai"], None)
        self.assertEqual(len(data["gpio"]), 11)

    def test_con_07b_pdu_goes_null_when_silent(self):
        self.snapshot()
        self.fsw.pdu.online = False
        deadline = time.monotonic() + 25               # null once the last sample is >15 s old
        while time.monotonic() < deadline:
            if self.snapshot(wait_for_pdu=False)["pdu"] is None:
                return
            time.sleep(1)
        self.fail("gui_state kept reporting a PDU block after the PDU stopped answering")
