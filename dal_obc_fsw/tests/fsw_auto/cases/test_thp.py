"""T-THP: thermal profile configuration (02/03 thp_cfg) against a faithful EXP model (tier A).

The EXP side (`ThermalProfiles` in fake_exp.py) applies the same validation as the firmware
(cli_temperature.c, profile_state.c, app_ir_temperature.c), so these cases check the OBC's
translation, its own range checks, and its handling of EXP refusals and bad echoes.
"""
import json
import math
import struct

from fsw_auto.fake_exp import ExpModel
from fsw_auto.fixtures import FswTestCase

THP = 0x03


def f32(x):
    return struct.pack("<f", x)


def heater_set(profile=0, main=0, sec=1, target=3000, delta=500, cutoff=8000, timeout=5000,
               resume=0, up=10, down=10, kp=1.5, ki=0.25, kd=0.0):
    p = bytearray(32)
    p[0:3] = bytes((1, 0, profile))
    p[3:5] = bytes((main, sec))
    struct.pack_into("<hhhH", p, 5, target, delta, cutoff, timeout)
    p[13:16] = bytes((resume, up, down))
    p[16:28] = f32(kp) + f32(ki) + f32(kd)
    return bytes(p)


def ir_set(led=0, cam=0, x=4, y=4, w=8, h=8, target=3000, cutoff=6000, rise=500, period=500,
           timeout=2000, duty=20, slew=50, kp=1.0, ki=0.1, kd=0.0):
    p = bytearray(32)
    p[0:3] = bytes((1, 1, led))
    p[3:8] = bytes((cam, x, y, w, h))
    struct.pack_into("<hhhHH", p, 8, target, cutoff, rise, period, timeout)
    p[18:20] = bytes((duty, slew))
    p[20:32] = f32(kp) + f32(ki) + f32(kd)
    return bytes(p)


def get(kind, idx):
    return bytes((0, kind, idx)) + bytes(29)


class Thp(FswTestCase):
    exp_responder = ExpModel()

    def setUp(self):
        super().setUp()
        model = self.exp_responder
        model.thermal.__init__()
        self.seen = len(self.fsw.exp.commands)

    def sent(self):
        return self.fsw.exp.commands[self.seen:]

    def j(self, *cmd):
        return json.loads(self.fsw.console.run("--json", *cmd))

    def tx(self, payload, **kw):
        return self.link.txrx(2, 0x10 if len(payload) == 32 and payload[0] == 0 else THP, payload, **kw)

    # -- wire level ----------------------------------------------------------------
    def test_thp_01_heater_get_returns_the_live_record(self):
        p = self.tx(get(0, 2)).expect_ack(2, 0x10, 32)
        self.assertEqual(p[0:3], bytes((0, 0, 2)))
        self.assertEqual(struct.unpack_from("<hhhH", p, 5), (3000, 500, 8000, 5000))
        self.assertEqual(struct.unpack_from("<fff", p, 16), (1.5, 0.25, 0.0))
        self.assertEqual(self.sent(), ["thp_wire_cfg " + get(0, 2).hex()])

    def test_thp_02_heater_set_is_applied_and_echoed(self):
        req = heater_set(profile=3, main=2, sec=3, target=4500, delta=300, cutoff=9000,
                         timeout=8000, resume=1, up=20, down=30, kp=2.0, ki=0.5, kd=0.125)
        self.assertEqual(self.tx(req).expect_ack(2, THP, 32), req)
        self.assertEqual(self.sent(), ["thp_wire_cfg " + req.hex()])
        again = self.tx(get(0, 3)).expect_ack(2, 0x10, 32)
        self.assertEqual(again[3:], req[3:])                       # persisted in the model
        self.assertEqual(self.tx(get(0, 0)).payload[5:7], struct.pack("<h", 3000))  # others untouched

    def test_thp_03_ir_requires_configuration_before_get(self):
        self.tx(get(1, 0)).expect_err(1)                            # EXP: unconfigured -> refused
        req = ir_set(led=1)
        self.assertEqual(self.tx(req).expect_ack(2, THP, 32), req)
        self.assertEqual(self.tx(get(1, 1)).payload, bytes((0, 1, 1)) + req[3:])

    def test_thp_04_wire_shape_rules_are_enforced_by_the_obc(self):
        before = len(self.fsw.exp.commands)
        bad = [bytes((2, 0, 0)) + bytes(29),                        # action 2
               bytes((0, 2, 0)) + bytes(29),                        # kind 2
               bytes((0, 0, 4)) + bytes(29),                        # heater 4
               bytes((0, 1, 2)) + bytes(29),                        # IR led 2
               bytes((0, 0, 0)) + b"\x01" + bytes(28),              # get with non-zero tail
               bytes((0, 0, 0)) + bytes(28) + b"\x01",
               heater_set()[:28] + b"\x00\x00\x00\x01"]             # heater set with bytes 28-31 used
        for payload in bad:
            with self.subTest(payload.hex()):
                self.tx(payload).expect_err(1)
        self.assertEqual(self.fsw.exp.commands[before:], [])       # none reached the EXP

    def test_thp_05_length_must_be_zero_or_32(self):
        for n in (1, 3, 31, 33, 64):
            self.tx(bytes(n)).expect_err(0)

    def test_thp_06_exp_refusal_is_a_data_error(self):
        self.exp_responder.thermal.running.add(("heater", 1))
        self.tx(heater_set(profile=1)).expect_err(1)                # profile running
        self.tx(heater_set(profile=1, cutoff=2000)).expect_err(1)   # cutoff below set point
        self.tx(heater_set(profile=0, delta=0)).expect_err(1)
        self.tx(heater_set(profile=0, up=0)).expect_err(1)
        self.tx(heater_set(profile=0, timeout=0)).expect_err(1)
        self.tx(heater_set(profile=0, main=4)).expect_err(1)
        self.tx(ir_set(w=0)).expect_err(1)
        self.tx(ir_set(x=30, w=8)).expect_err(1)                    # leaves the 32x24 array
        self.tx(ir_set(duty=26)).expect_err(1)                      # above the 25 % hard cap
        self.tx(ir_set(period=100)).expect_err(1)
        self.tx(ir_set(period=500, timeout=500)).expect_err(1)
        self.tx(heater_set(profile=0, kp=-1.0)).expect_err(1)
        self.tx(heater_set(profile=0, kp=math.inf)).expect_err(1)

    def test_thp_07_mismatched_echo_is_rejected(self):
        original = self.exp_responder.thermal.handle
        self.exp_responder.extra.clear()
        self.exp_responder.thermal.handle = lambda h: "THP_CFG " + (b"\x00\x00\x00" + bytes(29)).hex()
        try:
            self.tx(heater_set()).expect_err(1)                     # echoed action 0, requested 1
        finally:
            self.exp_responder.thermal.handle = original

    def test_thp_08_unparsable_exp_replies(self):
        original = self.exp_responder.thermal.handle
        try:
            for reply in ("", "ERROR THP_CFG refused", "THP_CFG", "THP_CFG 00",
                          "THP_CFG " + "zz" * 32, "THP_CFG " + ("00" * 31 + "g")):
                self.exp_responder.thermal.handle = lambda h, r=reply: r
                with self.subTest(reply):
                    self.tx(get(0, 0)).expect_err(1)
        finally:
            self.exp_responder.thermal.handle = original

    # -- console parser ---------------------------------------------------------------
    def test_thp_10_console_heater_set_builds_the_expected_frame(self):
        r = self.j("thp_cfg", "heater", "set", "2", "1", "3", "4500", "300", "9000", "8000",
                   "1", "20", "30", "2", "0.5", "0.125")
        want = heater_set(profile=2, main=1, sec=3, target=4500, delta=300, cutoff=9000,
                          timeout=8000, resume=1, up=20, down=30, kp=2.0, ki=0.5, kd=0.125)
        self.assertTrue(r["ok"], r)
        self.assertEqual(bytes.fromhex(r["payload_hex"]), want)
        self.assertEqual(self.sent(), ["thp_wire_cfg " + want.hex()])

    def test_thp_11_console_ir_set_and_get(self):
        r = self.j("thp_cfg", "ir", "set", "1", "1", "2", "3", "10", "12", "3200", "6400", "600",
                   "400", "3000", "15", "40", "1.5", "0.2", "0.05")
        want = ir_set(led=1, cam=1, x=2, y=3, w=10, h=12, target=3200, cutoff=6400, rise=600,
                      period=400, timeout=3000, duty=15, slew=40, kp=1.5, ki=0.2, kd=0.05)
        self.assertTrue(r["ok"], r)
        self.assertEqual(bytes.fromhex(r["payload_hex"]), want)
        g = self.j("thp_cfg", "ir", "get", "1")
        self.assertEqual(bytes.fromhex(g["payload_hex"])[3:], want[3:])

    def test_thp_12_console_rejects_out_of_range_before_the_wire(self):
        # base indexes: 3 profile, 4 main_ntc, 5 sec_ntc, 6 target, 7 delta, 8 cutoff, 9 timeout,
        # 10 resume, 11 slew_up, 12 slew_down, 13 kp, 14 ki, 15 kd
        base = ["thp_cfg", "heater", "set", "0", "0", "1", "3000", "500", "8000", "5000", "0", "10", "10",
                "1", "0.1", "0"]

        def with_(index, value):
            return base[:index] + [value] + base[index + 1:]
        cases = {
            "heater 4": ["thp_cfg", "heater", "get", "4"],
            "ir 2": ["thp_cfg", "ir", "get", "2"],
            "kind": ["thp_cfg", "fan", "get", "0"],
            "action": ["thp_cfg", "heater", "put", "0"],
            "get extra arg": ["thp_cfg", "heater", "get", "0", "1"],
            "profile 4": with_(3, "4"),
            "ntc 4": with_(4, "4"),
            "sec ntc 4": with_(5, "4"),
            "resume 2": with_(10, "2"),
            "slew up 101": with_(11, "101"),
            "slew down 101": with_(12, "101"),
            "target low": with_(6, "-10001"),
            "target high": with_(6, "20001"),
            "delta 0": with_(7, "0"),
            "cutoff 20001": with_(8, "20001"),
            "timeout 0": with_(9, "0"),
            "timeout 60001": with_(9, "60001"),
            "negative kp": with_(13, "-1"),
            "negative ki": with_(14, "-1"),
            "nan kd": with_(15, "nan"),
            "inf kp": with_(13, "inf"),
            "text gain": with_(13, "fast"),
            "short": base[:-1],
            "long": base + ["0"],
            "ir x 32": ["thp_cfg", "ir", "set", "0", "0", "32", "0", "1", "1", "0", "100", "10", "500",
                        "1000", "10", "10", "1", "0", "0"],
            "ir duty 101": ["thp_cfg", "ir", "set", "0", "0", "0", "0", "1", "1", "0", "100", "10", "500",
                            "1000", "101", "10", "1", "0", "0"],
        }
        before = len(self.fsw.exp.commands)
        for name, cmd in cases.items():
            with self.subTest(name):
                self.assertEqual(self.j(*cmd), {"ok": False, "error": "invalid command or arguments"})
        self.assertEqual(self.fsw.exp.commands[before:], [])

    def test_thp_13_hex_is_accepted_for_unsigned_but_not_signed_fields(self):
        """Characterisation (finding D18): the docs say every numeric argument takes decimal or 0x
        hex, but signed fields (temperatures in centi-degrees, motor steps) are parsed base 10 only."""
        ok = self.j("thp_cfg", "heater", "set", "0x0", "0x1", "0x2", "3000", "500", "8000", "0x1388",
                    "0", "10", "10", "2", "1", "0")
        self.assertTrue(ok["ok"], ok)
        self.assertEqual(struct.unpack_from("<H", bytes.fromhex(ok["payload_hex"]), 11)[0], 5000)
        bad = self.j("thp_cfg", "heater", "set", "0", "0", "1", "0x0BB8", "500", "8000", "5000",
                     "0", "10", "10", "2", "1", "0")
        self.assertEqual(bad, {"ok": False, "error": "invalid command or arguments"})

    def test_thp_14_gains_are_ieee754_little_endian(self):
        r = self.j("thp_cfg", "heater", "set", "0", "0", "1", "3000", "500", "8000", "5000", "0", "10",
                   "10", "0.1", "2.5", "100")
        payload = bytes.fromhex(r["payload_hex"])
        self.assertEqual(payload[16:28], struct.pack("<fff", 0.1, 2.5, 100.0))


CTL = 0x04


def sel(kind, idx, action):
    return bytes((idx | kind << 2 | action << 3,))


class ThpCtl(FswTestCase):
    """T-THP-20..: 02/04 thp_ctl, one selector byte in, one status byte out (thp_wire_ctl)."""
    exp_responder = ExpModel()

    def setUp(self):
        super().setUp()
        model = self.exp_responder
        model.thermal.__init__()
        model.power["exp12v"] = True
        self.seen = len(self.fsw.exp.commands)

    def sent(self):
        return self.fsw.exp.commands[self.seen:]

    def ctl(self, payload, **kw):
        return self.link.txrx(2, CTL, payload, **kw)

    def j(self, *cmd):
        return json.loads(self.fsw.console.run("--json", *cmd))

    def test_thp_20_heater_status_is_disabled_and_configured(self):
        p = self.ctl(sel(0, 1, 0)).expect_ack(2, CTL, 1)
        self.assertEqual(p, bytes((0x40,)))                         # phase 0, configured
        self.assertEqual(self.sent(), ["thp_wire_ctl 01"])

    def test_thp_21_heater_start_stop_cycle(self):
        self.assertEqual(self.ctl(sel(0, 2, 1)).expect_ack(2, CTL, 1), bytes((0xC1,)))  # SOAK, running
        self.assertEqual(self.ctl(sel(0, 2, 0)).expect_ack(2, CTL, 1), bytes((0xC1,)))
        self.assertEqual(self.ctl(sel(0, 2, 2)).expect_ack(2, CTL, 1), bytes((0x40,)))
        self.assertEqual(self.sent(), ["thp_wire_ctl 0a", "thp_wire_ctl 02", "thp_wire_ctl 12"])

    def test_thp_22_running_profile_refuses_reconfiguration(self):
        self.ctl(sel(0, 0, 1)).expect_ack(2, CTL, 1)
        self.link.txrx(2, THP, heater_set(profile=0)).expect_err(0x01)
        self.ctl(sel(0, 0, 2)).expect_ack(2, CTL, 1)
        self.link.txrx(2, THP, heater_set(profile=0)).expect_ack(2, THP, 32)

    def test_thp_23_resume_needs_an_error_profile(self):
        self.ctl(sel(0, 3, 3)).expect_err(0x01)
        self.assertEqual(self.sent(), ["thp_wire_ctl 1b"])
        m = self.exp_responder.thermal
        m.heater_phase[3], m.heater_reason[3] = 2, 3                  # ERROR, over-temperature
        self.assertEqual(self.ctl(sel(0, 3, 0)).expect_ack(2, CTL, 1), bytes((0x5A,)))
        self.assertEqual(self.ctl(sel(0, 3, 3)).expect_ack(2, CTL, 1), bytes((0xC1,)))

    def test_thp_24_heater_start_refused_without_power_or_with_bad_sensors(self):
        self.exp_responder.power["exp12v"] = False
        self.ctl(sel(0, 0, 1)).expect_err(0x01)
        self.exp_responder.power["exp12v"] = True
        self.exp_responder.thermal.sensor_fault.add(0)
        self.ctl(sel(0, 0, 1)).expect_err(0x01)
        self.assertEqual(self.ctl(sel(0, 0, 0)).expect_ack(2, CTL, 1), bytes((0x40,)))

    def test_thp_25_ir_needs_a_configured_profile(self):
        self.ctl(sel(1, 0, 1)).expect_err(0x01)                       # unconfigured: start refused
        self.link.txrx(2, THP, ir_set(led=0)).expect_ack(2, THP, 32)
        p = self.ctl(sel(1, 0, 1)).expect_ack(2, CTL, 1)
        self.assertEqual(p, bytes((0xC1,)))                           # STARTING, configured, running
        self.assertEqual(self.ctl(sel(1, 0, 2)).expect_ack(2, CTL, 1), bytes((0x40,)))
        self.ctl(sel(1, 0, 2)).expect_err(0x01)                       # stop when idle is refused

    def test_thp_26_only_one_ir_channel_runs_at_a_time(self):
        for led in (0, 1):
            self.link.txrx(2, THP, ir_set(led=led)).expect_ack(2, THP, 32)
        self.ctl(sel(1, 0, 1)).expect_ack(2, CTL, 1)
        self.ctl(sel(1, 1, 1)).expect_err(0x01)
        self.ctl(sel(1, 0, 2)).expect_ack(2, CTL, 1)
        self.ctl(sel(1, 1, 1)).expect_ack(2, CTL, 1)

    def test_thp_27_selector_rules_are_checked_before_the_wire(self):
        bad = {"reserved bit 5": 0x20, "reserved bit 7": 0x80,
               "ir channel 2": 0x04 | 2, "ir channel 3": 0x04 | 3,
               "ir resume": 0x04 | 3 << 3}
        for name, b in bad.items():
            with self.subTest(name):
                self.ctl(bytes((b,))).expect_err(0x01)
        self.assertEqual(self.sent(), [])

    def test_thp_28_length_must_be_zero_or_one(self):
        for n in (2, 4):
            with self.subTest(n):
                self.ctl(bytes(n)).expect_err(0x00)
        self.assertEqual(self.sent(), [])

    def test_thp_29_bad_mcu_replies_are_rejected(self):
        m = self.exp_responder
        for name, reply in {"no prefix": "OK",
                            "heater phase 3": "THP_CTL 03",
                            "running bit disagrees": "THP_CTL 01",   # SOAK but not running
                            "idle but running": "THP_CTL 80"}.items():
            with self.subTest(name):
                m.extra["thp_wire_ctl 00"] = reply
                self.ctl(sel(0, 0, 0)).expect_err(0x01)
        m.extra["thp_wire_ctl 00"] = "ERROR THP_CTL refused"
        self.ctl(sel(0, 0, 0)).expect_err(0x01)

    def test_thp_30_console_builds_selector_and_decodes_status(self):
        for args, byte in {("heater", "2", "start"): 0x0A, ("heater", "3", "resume"): 0x1B,
                           ("ir", "1", "status"): 0x05}.items():
            self.exp_responder.thermal.heater_phase[2] = 0
            r = self.j("thp_ctl", *args)
            self.assertIn("payload_hex", r)
            self.assertEqual(self.sent()[-1], "thp_wire_ctl %02x" % byte, (args, r))

    def test_thp_31_console_rejects_bad_selectors_before_the_wire(self):
        for cmd in (["heater", "4", "start"], ["ir", "2", "start"], ["ir", "0", "resume"],
                    ["fan", "0", "start"], ["heater", "0", "pause"], ["heater", "0"],
                    ["heater", "0", "start", "x"], []):
            with self.subTest(cmd):
                self.assertEqual(self.j("thp_ctl", *cmd),
                                 {"ok": False, "error": "invalid command or arguments"})
        self.assertEqual(self.sent(), [])
