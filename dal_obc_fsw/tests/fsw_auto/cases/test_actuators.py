"""T-THM / T-FLD / T-RBA: manual thermal, fluidic and motor commands against the scripted EXP (tier A).

The EXP side is `Actuators` (fake_actuators.py), which answers in the firmware's reply formats.
"""
import struct

from fsw_auto.fake_exp import ExpModel
from fsw_auto.fixtures import FswTestCase

THM_CFG, THM_CTL, THM_STAT = 0x13, 0x14, 0x15
FLD_CFG, FLD_CTL, FLD_STAT = 0x16, 0x17, 0x18
RBA_CTL, RBA_STAT = 0x1A, 0x1B


def tc8(a, b, c, d, value=0):
    return bytes((a, b, c, d)) + struct.pack("<I", value & 0xFFFFFFFF)


class ActuatorCase(FswTestCase):
    exp_responder = ExpModel()

    def setUp(self):
        super().setUp()
        m = self.exp_responder
        m.thermal.__init__()
        m.act.reset()
        m.power.update(exp12v=True, sln_hd=True, motor=True)
        self.seen = len(self.fsw.exp.commands)

    @property
    def act(self):
        return self.exp_responder.act

    def sent(self):
        return self.fsw.exp.commands[self.seen:]

    def tx(self, addr, payload=b"", **kw):
        return self.link.txrx(1, addr, payload, **kw)


class Thermal(ActuatorCase):
    # -- thm_cfg ---------------------------------------------------------------------------
    def test_thm_01_get_returns_the_limit_record(self):
        p = self.tx(THM_CFG).expect_ack(1, THM_CFG, 32)
        self.assertEqual(p[:2], bytes((1, 0x3F)))
        self.assertEqual(self.sent(), ["thm_cfg"])

    def test_thm_02_set_merges_only_the_selected_records(self):
        req = bytearray(32)
        req[0], req[1] = 1, 0b010010                      # heater 1 and IR 0
        struct.pack_into("<BBH", req, 6, 50, 10, 3000)    # heater 1: duty 50, slew 10, on 3000 ms
        struct.pack_into("<BBIB", req, 18, 20, 40, 5000, 3)
        out = self.tx(THM_CFG, bytes(req)).expect_ack(1, THM_CFG, 32)
        self.assertEqual(struct.unpack_from("<BBH", out, 6), (50, 10, 3000))
        self.assertEqual(struct.unpack_from("<BBIB", out, 18), (20, 40, 5000, 3))
        self.assertEqual(struct.unpack_from("<BBH", out, 2), (80, 0, 10000))     # heater 0 unchanged
        self.assertEqual(out[1], 0x3F)                                            # reply mask is all six

    def test_thm_03_range_checks_happen_before_the_wire(self):
        def rec(mask, **kw):
            p = bytearray(32)
            p[0], p[1] = 1, mask
            struct.pack_into("<BBH", p, 2, kw.get("duty", 50), kw.get("slew", 0), kw.get("on", 1000))
            struct.pack_into("<BBIB", p, 18, kw.get("ir", 10), kw.get("irms", 10), kw.get("hz", 1000), 0)
            return bytes(p)
        bad = {"mask 0": rec(0), "mask bit 6": rec(0x40 | 1), "duty 101": rec(1, duty=101),
               "slew 101": rec(1, slew=101), "on 0": rec(1, on=0), "on 60001": rec(1, on=60001),
               "ir duty 26": rec(0x10, ir=26), "ir on 0": rec(0x10, irms=0),
               "ir on 101": rec(0x10, irms=101), "hz 99": rec(0x10, hz=99), "hz 100001": rec(0x10, hz=100001),
               "version 2": bytes((2,)) + rec(1)[1:]}
        for name, payload in bad.items():
            with self.subTest(name):
                self.tx(THM_CFG, payload).expect_err(0x01)
        self.assertEqual(self.sent(), [])

    def test_thm_04_lengths(self):
        for n in (1, 31, 33):
            with self.subTest(n):
                self.tx(THM_CFG, bytes(n)).expect_err(0x00)

    def test_thm_05_exp_refusal_is_a_data_error(self):
        req = bytearray(32)
        req[0], req[1] = 1, 1
        struct.pack_into("<BBH", req, 2, 50, 0, 1005)     # on time not a multiple of 10 ms: EXP refuses
        self.tx(THM_CFG, bytes(req)).expect_err(0x01)
        self.assertEqual(len(self.sent()), 1)

    def test_thm_06_a_short_or_masked_reply_is_rejected(self):
        self.exp_responder.extra["thm_cfg"] = "THM_CFG " + (bytes((1, 0x0F)) + bytes(30)).hex()
        self.tx(THM_CFG).expect_err(0x01)
        self.exp_responder.extra["thm_cfg"] = "THM_CFG 0102"
        self.tx(THM_CFG).expect_err(0x01)

    # -- thm_ctl / thm_stat ----------------------------------------------------------------
    def test_thm_10_heater_manual_set_and_off(self):
        self.tx(THM_CTL, tc8(0, 2, 1, 40, 5000)).expect_ack(1, THM_CTL, 4)
        self.assertEqual(self.sent(), ["thm_wire_ctl 0 2 1 40 5000"])
        stat = self.tx(THM_STAT).expect_ack(1, THM_STAT, 6)
        self.assertEqual(stat, bytes((0, 0, 0x80 | 40, 0, 0, 0)))
        self.tx(THM_CTL, tc8(0, 2, 0, 0, 0)).expect_ack(1, THM_CTL, 4)
        self.assertEqual(self.tx(THM_STAT).payload, bytes(6))

    def test_thm_11_ir_manual_and_stop(self):
        self.tx(THM_CTL, tc8(1, 1, 1, 10)).expect_ack(1, THM_CTL, 4)
        self.assertEqual(self.tx(THM_STAT).payload[5], 0x80 | 10)
        self.tx(THM_CTL, tc8(1, 1, 0, 0)).expect_ack(1, THM_CTL, 4)
        self.assertEqual(self.tx(THM_STAT).payload[5], 0)

    def test_thm_12_request_shape_is_validated_before_the_wire(self):
        bad = {"kind 2": tc8(2, 0, 0, 0), "heater 4": tc8(0, 4, 1, 10, 1000), "ir 2": tc8(1, 2, 1, 10),
               "action 3": tc8(0, 0, 3, 0), "duty 101": tc8(0, 0, 1, 101, 1000),
               "off with duty": tc8(0, 0, 0, 5), "off with duration": tc8(0, 0, 0, 0, 5),
               "heater no duty": tc8(0, 0, 1, 0, 1000), "heater no duration": tc8(0, 0, 1, 10, 0),
               "heater 600001 ms": tc8(0, 0, 1, 10, 600001), "ir with duration": tc8(1, 0, 1, 10, 5),
               "ir duty 26": tc8(1, 0, 1, 26), "heater feedback": tc8(0, 0, 2, 0),
               "feedback with duty": tc8(1, 0, 2, 5)}
        for name, payload in bad.items():
            with self.subTest(name):
                self.tx(THM_CTL, payload).expect_err(0x01)
        self.assertEqual(self.sent(), [])
        self.tx(THM_CTL, bytes(7)).expect_err(0x00)

    def test_thm_13_exp_refusals(self):
        self.exp_responder.power["exp12v"] = False
        self.tx(THM_CTL, tc8(0, 0, 1, 10, 1000)).expect_err(0x01)
        self.exp_responder.power["exp12v"] = True
        self.tx(THM_CTL, tc8(0, 0, 1, 90, 1000)).expect_err(0x01)                  # above the 80 % ceiling
        self.tx(THM_CTL, tc8(0, 0, 1, 10, 20000)).expect_err(0x01)                 # longer than max on time
        self.assertEqual(self.tx(THM_STAT).payload, bytes(6))

    def test_thm_13b_profile_owner_refuses_manual_commands(self):
        self.assertEqual(self.exp_responder.thermal.ctl("08", True), "THP_CTL c1")
        self.tx(THM_CTL, tc8(0, 0, 1, 10, 1000)).expect_err(0x01)
        self.tx(THM_CTL, tc8(0, 0, 0, 0)).expect_err(0x01)
        self.assertEqual(self.act.heater_duty[0], 0)

    def test_thm_14_stat_rejects_inconsistent_replies(self):
        for name, reply in {"duty over 100": "THM_STAT6 65" + "00" * 5,
                            "flag without duty": "THM_STAT6 80" + "00" * 5,
                            "short": "THM_STAT6 0000", "no prefix": "OK",
                            "unavailable": "ERROR THM_STAT UNAVAILABLE"}.items():
            with self.subTest(name):
                self.exp_responder.extra["thm_wire_stat all"] = reply
                self.tx(THM_STAT).expect_err(0x01)
        self.tx(THM_STAT, b"x").expect_err(0x00)

    def test_thm_15_silent_exp_returns_the_uart_error(self):
        self.exp_responder.extra["thm_wire_stat all"] = ""
        self.tx(THM_STAT, timeout=20).expect_err(0x01)


class Fluidic(ActuatorCase):
    def test_fld_01_valve_mask_maps_to_onground_control(self):
        self.tx(FLD_CTL, tc8(0, 0, 0, 0b0101)).expect_ack(1, FLD_CTL, 0)
        self.assertEqual(self.sent(), ["pc_onground_control 1 0 1 0"])
        st = self.tx(FLD_STAT).expect_ack(1, FLD_STAT, 16)
        self.assertEqual(st[0], 0b0101)

    def test_fld_02_pump_needs_a_configured_type(self):
        self.tx(FLD_CTL, tc8(1, 1, 0, 0)).expect_err(0x01)                          # "Pump type unconfigured"
        self.tx(FLD_CFG, self.cfg(8, 1)).expect_ack(1, FLD_CFG, 32)
        self.assertEqual(self.sent()[-1], "pump_type_set highdriver")
        p = self.tx(FLD_CTL, tc8(1, 1, 0, 0)).expect_ack(1, FLD_CTL, 4)             # queued: returns the id
        self.assertGreater(struct.unpack("<I", p)[0], 0)

    @staticmethod
    def cfg(sub, value=0, a=0, b=0):
        p = bytearray(32)
        p[0], p[1], p[2], p[3] = 1, sub, a, b
        struct.pack_into("<I", p, 4, value)
        return bytes(p)

    def test_fld_03_pump_values_and_ranges(self):
        self.tx(FLD_CFG, self.cfg(8, 0)).expect_ack(1, FLD_CFG, 32)                 # lowdriver
        ok = {(1, 2, 0, 100): "pump_set_freq 100", (1, 3, 0, 120): "pump_set_volt 120",
              (2, 2, 0, 400): "pump4_set_freq 400", (2, 3, 2, 200): "pump4_set_volt 2 200",
              (2, 1, 0, 0): "pump4_enable", (1, 0, 0, 0): "pump_disable"}
        for (pump, op, ch, value), want in ok.items():
            with self.subTest(want):
                self.tx(FLD_CTL, tc8(pump, op, ch, 0, value)).expect_ack(1, FLD_CTL, 4)
                self.assertEqual(self.sent()[-1], want)
        bad = {"u11 freq 7": (1, 2, 0, 7), "u11 freq 1993": (1, 2, 0, 1993), "u11 volt 151": (1, 3, 0, 151),
               "u15 freq 49": (2, 2, 0, 49), "u15 freq 801": (2, 2, 0, 801), "u15 volt 251": (2, 3, 0, 251),
               "u15 channel 4": (2, 3, 4, 10), "u11 channel": (1, 3, 1, 10), "op 4": (1, 4, 0, 0),
               "on with value": (1, 1, 0, 5), "pump 3": (3, 1, 0, 0)}
        before = len(self.sent())
        for name, (pump, op, ch, value) in bad.items():
            with self.subTest(name):
                self.tx(FLD_CTL, tc8(pump, op, ch, 0, value)).expect_err(0x01)
        self.assertEqual(len(self.sent()), before)
        self.tx(FLD_CTL, bytes(7)).expect_err(0x00)

    def test_fld_04_valve_request_shape(self):
        for payload in (tc8(0, 0, 0, 16), tc8(0, 1, 0, 1), tc8(0, 0, 1, 1), tc8(0, 0, 0, 1, 1), tc8(9, 0, 0, 0)):
            with self.subTest(payload.hex()):
                self.tx(FLD_CTL, payload).expect_err(0x01)
        self.assertEqual(self.sent(), [])

    def test_fld_05_stat_layout(self):
        self.tx(FLD_CFG, self.cfg(8, 1)).expect_ack(1, FLD_CFG, 32)
        self.tx(FLD_CTL, tc8(1, 1, 0, 0)).expect_ack(1, FLD_CTL, 4)
        self.tx(FLD_CTL, tc8(1, 2, 0, 0, 250)).expect_ack(1, FLD_CTL, 4)
        self.tx(FLD_CTL, tc8(2, 3, 1, 0, 90)).expect_ack(1, FLD_CTL, 4)
        st = self.tx(FLD_STAT).expect_ack(1, FLD_STAT, 16)
        self.assertEqual(st[3], 2)                                                   # high driver
        self.assertEqual(struct.unpack_from("<H", st, 4)[0], 250)
        self.assertTrue(st[2] & 4)                                                   # U11 enabled
        self.assertEqual(st[9:13], bytes((0, 90, 0, 0)))

    def test_fld_06_stat_rejects_malformed_replies(self):
        good = bytearray(16)
        cases = {"reserved bit in byte 0": bytes((0x10,)) + good[1:],
                 "reserved bit in byte 1": bytes((0, 0x10)) + good[2:],
                 "pump type 3": bytes(3) + bytes((3,)) + good[4:],
                 "byte 13 set": bytes(13) + bytes((1, 0, 0)),
                 "byte 15 set": bytes(15) + bytes((1,))}
        for name, raw in cases.items():
            with self.subTest(name):
                self.exp_responder.extra["fld_wire_stat"] = "FLD_STAT " + raw.hex()
                self.tx(FLD_STAT).expect_err(0x01)
        self.exp_responder.extra["fld_wire_stat"] = "FLD_STAT " + bytes(17).hex()      # 17 bytes: too long
        self.tx(FLD_STAT).expect_err(0x01)
        self.exp_responder.extra["fld_wire_stat"] = "ERROR fld_wire_stat NOT_READY"
        self.tx(FLD_STAT).expect_err(0x01)
        self.tx(FLD_STAT, b"x").expect_err(0x00)

    def test_fld_07_pump_detect_and_result_tickets(self):
        self.tx(FLD_CFG, self.cfg(8, 0)).expect_ack(1, FLD_CFG, 32)
        out = self.tx(FLD_CFG, self.cfg(9, 0, a=0, b=0)).expect_ack(1, FLD_CFG, 32)    # queue detect
        ticket = struct.unpack_from("<I", out, 4)[0]
        self.assertEqual(out[8], 0)                                                     # queued
        self.assertEqual(self.sent()[-1], "pump_detect")
        res = self.tx(FLD_CFG, self.cfg(9, ticket, a=1, b=0xFF)).expect_ack(1, FLD_CFG, 32)
        self.assertEqual(res[8], 2)                                                     # done
        # D22: the EXP now says "Pump result replaced or unknown"; the OBC still looks for "expired or unknown"
        # (bee_dal_tmtc_service.c), so a stale ticket is a data error instead of status 11.
        self.tx(FLD_CFG, self.cfg(9, 9999, a=1, b=0xFF)).expect_err(0x01)

    def test_fld_08_pump_detect_without_a_type_reports_not_ready(self):
        out = self.tx(FLD_CFG, self.cfg(9)).expect_ack(1, FLD_CFG, 32)
        self.assertEqual(out[8], 9)


class Motors(ActuatorCase):
    def mv(self, op, motor=0, speed=0, value=0):
        return tc8(op, motor, speed, 0, value)

    def init(self):
        self.tx(RBA_CTL, self.mv(1, 2)).expect_ack(1, RBA_CTL, 4)

    def test_rba_01_init_move_goto_and_result(self):
        self.init()
        self.assertEqual(self.sent(), ["motor5_init"])
        mv = self.tx(RBA_CTL, self.mv(2, 0, 0, 500)).expect_ack(1, RBA_CTL, 4)
        self.assertEqual(self.sent()[-1], "motor5_move 0 500")
        self.tx(RBA_CTL, self.mv(2, 0, 50, -200)).expect_ack(1, RBA_CTL, 4)
        self.assertEqual(self.sent()[-1], "motor5_move 0 -200 50")
        self.tx(RBA_CTL, self.mv(3, 1, 0, 1234)).expect_ack(1, RBA_CTL, 4)
        self.assertEqual(self.sent()[-1], "motor5_goto 1 1234")
        res = self.tx(RBA_CTL, self.mv(8, 2, 0, struct.unpack("<I", mv)[0])).expect_ack(1, RBA_CTL, 12)
        self.assertEqual(struct.unpack_from("<I", res, 0)[0], struct.unpack("<I", mv)[0])
        self.assertEqual(res[4], 2)                                                     # DONE
        self.assertEqual(struct.unpack_from("<i", res, 8)[0], 500)

    def test_rba_02_run_stop_disable_and_home(self):
        self.init()
        self.tx(RBA_CTL, self.mv(4, 1, 30, 1)).expect_ack(1, RBA_CTL, 4)
        self.assertEqual(self.sent()[-1], "motor5_run 1 cw 30")
        self.assertEqual(self.tx(RBA_STAT).expect_ack(1, RBA_STAT, 16)[5], 2)           # RUNNING
        self.tx(RBA_CTL, self.mv(4, 0, 0, 0xFFFFFFFF)).expect_ack(1, RBA_CTL, 4)
        self.assertEqual(self.sent()[-1], "motor5_run 0 ccw 0")
        self.tx(RBA_CTL, self.mv(9, 1)).expect_ack(1, RBA_CTL, 0)
        self.assertEqual(self.sent()[-1], "motor5_stop 1 hold")
        self.assertEqual(self.tx(RBA_STAT).expect_ack(1, RBA_STAT, 16)[5], 4)
        self.tx(RBA_CTL, self.mv(6, 2)).expect_ack(1, RBA_CTL, 0)
        self.assertEqual(self.sent()[-1], "motor5_stop all")
        self.tx(RBA_CTL, self.mv(5, 0)).expect_ack(1, RBA_CTL, 4)
        self.assertEqual(self.sent()[-1], "motor5_home 0")
        self.tx(RBA_CTL, self.mv(7, 2)).expect_ack(1, RBA_CTL, 0)
        self.assertEqual(self.sent()[-1], "motor5_disable")

    def test_rba_03_motion_before_init_is_refused(self):
        self.tx(RBA_CTL, self.mv(2, 0, 0, 10)).expect_err(0x01)                          # ERROR NOT_READY
        self.init()
        self.tx(RBA_CTL, self.mv(7, 2)).expect_ack(1, RBA_CTL, 0)
        self.tx(RBA_CTL, self.mv(2, 0, 0, 10)).expect_err(0x01)                          # asleep again

    def test_rba_04_exp_error_states_map_to_data_errors(self):
        self.init()
        for state in ("REFUSED", "BUSY", "NOT_HOMED", "LIMIT_REACHED", "FAULT"):
            with self.subTest(state):
                self.act.refuse_motor = state
                self.tx(RBA_CTL, self.mv(2, 0, 0, 10)).expect_err(0x01)

    def test_rba_05_request_shape_is_validated_before_the_wire(self):
        bad = {"op 0": self.mv(0), "op 10": self.mv(10), "init motor 0": self.mv(1, 0),
               "init speed": self.mv(1, 2, 5), "move motor 2": self.mv(2, 2, 0, 5),
               "move zero": self.mv(2, 0, 0, 0), "move INT32_MIN": self.mv(2, 0, 0, -2 ** 31),
               "speed 201": self.mv(2, 0, 201, 5), "reserved byte": tc8(2, 0, 0, 1, 5),
               "run value 2": self.mv(4, 0, 10, 2), "home with speed": self.mv(5, 0, 10),
               "home motor 2": self.mv(5, 2), "stop motor 3": self.mv(6, 3),
               "stop value": self.mv(6, 0, 0, 1), "hold all": self.mv(9, 2),
               "hold speed": self.mv(9, 0, 10), "hold value": self.mv(9, 0, 0, 1),
               "disable motor 0": self.mv(7, 0),
               "result zero": self.mv(8, 2), "init with value": self.mv(1, 2, 0, 1)}
        for name, payload in bad.items():
            with self.subTest(name):
                self.tx(RBA_CTL, payload).expect_err(0x01)
        self.assertEqual(self.sent(), [])
        self.tx(RBA_CTL, bytes(7)).expect_err(0x00)

    def test_rba_06_unknown_result_id(self):
        self.init()
        res = self.tx(RBA_CTL, self.mv(8, 2, 0, 424242)).expect_ack(1, RBA_CTL, 12)
        self.assertEqual(res[4], 13)                  # NOT_FOUND is reported as a state, not an error

    def test_rba_10_stat_layout(self):
        self.init()
        self.tx(RBA_CTL, self.mv(2, 1, 0, -300)).expect_ack(1, RBA_CTL, 4)
        self.tx(RBA_CTL, self.mv(5, 0)).expect_ack(1, RBA_CTL, 4)
        st = self.tx(RBA_STAT).expect_ack(1, RBA_STAT, 16)
        self.assertEqual(st[0] & 3, 1)                                                    # initialised, awake
        self.assertEqual(struct.unpack_from("<ii", st, 6), (0, -300))
        self.assertEqual(st[3] & 3, 1)                                                    # motor 0 reference valid
        self.assertEqual(st[4:6], bytes((0, 4)))                                          # homed = stopped, move = HOLDING

    def test_rba_13_finished_move_holds_and_can_move_again(self):
        self.init()
        self.tx(RBA_CTL, self.mv(2, 0, 0, 40)).expect_ack(1, RBA_CTL, 4)
        self.assertEqual(self.tx(RBA_STAT).expect_ack(1, RBA_STAT, 16)[4], 4)             # HOLDING after the move
        self.tx(RBA_CTL, self.mv(2, 0, 0, -10)).expect_ack(1, RBA_CTL, 4)                # no release needed (ddc3ae2)
        self.assertEqual(struct.unpack_from("<i", self.tx(RBA_STAT).expect_ack(1, RBA_STAT, 16), 6)[0], 30)
        self.tx(RBA_CTL, self.mv(6, 0)).expect_ack(1, RBA_CTL, 0)
        self.assertEqual(self.tx(RBA_STAT).expect_ack(1, RBA_STAT, 16)[4], 0)             # stop releases the hold

    def test_rba_14_speed_cap_is_200_although_the_exp_now_takes_1000(self):
        """D21: the OBC rejects 201..255 and rba_stat rejects a speed byte above 200; the EXP allows 1000 and clamps to 255."""
        self.init()
        self.tx(RBA_CTL, self.mv(2, 0, 200, 5)).expect_ack(1, RBA_CTL, 4)
        sent = len(self.sent())
        self.tx(RBA_CTL, self.mv(2, 0, 201, 5)).expect_err(0x01)
        self.assertEqual(len(self.sent()), sent)
        self.act.speed[0] = 1000                                                        # what the firmware can now run
        self.assertEqual(self.act.c_rba_wire_stat([], None).split()[1][28:30], "FF")     # clamped to one byte
        self.tx(RBA_STAT).expect_err(0x01)

    def test_rba_11_stat_rejects_malformed_replies(self):
        ok = bytearray(16)
        cases = {"reserved bits byte 0": bytes((0x40,)) + ok[1:],
                 "active without valid": bytes((0, 1, 0)) + ok[3:],
                 "reserved bits byte 1": bytes((0, 0x10, 0x10)) + ok[3:],
                 "reserved bits byte 3": bytes(3) + bytes((0x10,)) + ok[4:],
                 "state 6": bytes(4) + bytes((6,)) + ok[5:],
                 "speed 201": bytes(14) + bytes((201, 0)),
                 "speed 201 motor 1": bytes(15) + bytes((201,)),
                 "speed 255 (clamped 1000 sps)": bytes(14) + bytes((255, 0))}
        for name, raw in cases.items():
            with self.subTest(name):
                self.exp_responder.extra["rba_wire_stat"] = "RBA_STAT " + raw.hex().upper()
                self.tx(RBA_STAT).expect_err(0x01)
        self.exp_responder.extra["rba_wire_stat"] = "NOT_READY"
        self.tx(RBA_STAT).expect_err(0x01)
        self.tx(RBA_STAT, b"x").expect_err(0x00)
        self.exp_responder.extra.pop("rba_wire_stat")             # the responder outlives the test

    def test_rba_12_reserved_rba_cfg_is_unknown(self):
        self.tx(0x19, bytes(32)).expect_err(0x03)
