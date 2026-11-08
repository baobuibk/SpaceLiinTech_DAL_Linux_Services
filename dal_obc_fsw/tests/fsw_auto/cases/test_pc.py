"""T-PC: protein-crystallisation control (02/02 pc_ctl) and pc_cfg (02/01) via a scripted EXP (tier A)."""
import struct

from fsw_auto.fixtures import FswTestCase

STAT, CFG, CTL = 0x00, 0x01, 0x02

# opcode, selector -> (EXP command the daemon must send, text the EXP answers with)
GOOD = {
    (1, 0): ("pc_activate", "OK pc_activate"),
    (2, 0): ("pc_deactivate", "OK pc_deactivate"),
    (3, 0): ("pc_test both", "pc test sent"), (3, 1): ("pc_test a", "pc test sent"),
    (3, 2): ("pc_test b", "pc test sent"),
    (4, 0): ("pc_replace both", "pc replace sent"), (4, 1): ("pc_replace a", "pc replace sent"),
    (4, 2): ("pc_replace b", "pc replace sent"),
    (5, 0): ("pc_circulation both", "pc circulation sent"),
    (5, 1): ("pc_circulation a", "pc circulation sent"),
    (5, 2): ("pc_circulation b", "pc circulation sent"),
    (6, 0): ("pc_onground protein_a", "pc onground preset sent"),
    (6, 1): ("pc_onground protein_b", "pc onground preset sent"),
    (6, 2): ("pc_onground precip_respol", "pc onground preset sent"),
    (6, 3): ("pc_onground precip_rsvb", "pc onground preset sent"),
    (7, 0): ("pc_standby", "pc standby sent"),
    (8, 0): ("pc_stop", "pc stop sent"),
    (9, 0): ("pc_reset_error", "pc reset_error sent"),
}
ANSWERS = {cmd: text for cmd, text in GOOD.values()}


def respond(command):
    return ANSWERS.get(command, "ERROR unknown command")


def tc(op, sel=0, rest=b"\0" * 6):
    return bytes((op, sel)) + rest


class PcControl(FswTestCase):
    exp_responder = staticmethod(respond)

    def setUp(self):
        super().setUp()
        self.seen = len(self.fsw.exp.commands)

    def sent(self):
        return self.fsw.exp.commands[self.seen:]

    def test_pc_01_every_valid_request_maps_to_the_exp_command(self):
        for (op, sel), (command, _) in GOOD.items():
            with self.subTest(op=op, sel=sel):
                self.seen = len(self.fsw.exp.commands)
                self.link.txrx(2, CTL, tc(op, sel)).expect_ack(2, CTL, 4)
                self.assertEqual(self.sent(), [command])

    def test_pc_02_invalid_requests_are_rejected_without_touching_the_exp(self):
        bad = [tc(0), tc(10), tc(0xFF), tc(1, 1), tc(2, 1), tc(7, 1), tc(8, 1), tc(9, 1),
               tc(3, 3), tc(4, 3), tc(5, 3), tc(6, 4), tc(3, 0, b"\x01" + b"\0" * 5),
               tc(1, 0, b"\0" * 5 + b"\x01")]
        for payload in bad:
            with self.subTest(payload.hex()):
                self.link.txrx(2, CTL, payload).expect_err(1)
        self.assertEqual(self.sent(), [])

    def test_pc_03_length_must_be_8(self):
        for n in (0, 1, 7, 9, 32):
            self.link.txrx(2, CTL, bytes(n)).expect_err(0)
        self.assertEqual(self.sent(), [])

    def test_pc_04_exp_refusal_is_a_data_error(self):
        self.fsw.exp.responder = lambda command: "ERROR pc busy"
        try:
            self.link.txrx(2, CTL, tc(3, 0)).expect_err(1)
        finally:
            self.fsw.exp.responder = respond

    def test_pc_05_already_active_states_are_acknowledged(self):
        self.fsw.exp.responder = lambda c: "pc already activated" if "activate" in c and "de" not in c \
            else "pc already deactivated"
        try:
            self.link.txrx(2, CTL, tc(1)).expect_ack(2, CTL, 4)
            self.link.txrx(2, CTL, tc(2)).expect_ack(2, CTL, 4)
        finally:
            self.fsw.exp.responder = respond


class PcConfig(FswTestCase):
    exp_responder = staticmethod(lambda command: "OK")

    def test_pc_10_pump_subops_are_not_available_on_pc_cfg(self):
        before = len(self.fsw.exp.commands)
        for sub in (8, 9):
            payload = bytes((1, sub)) + bytes(30)
            self.link.txrx(2, CFG, payload).expect_err(1)
        self.assertEqual(self.fsw.exp.commands[before:], [])

    def test_pc_11_length_rules(self):
        for n in (1, 2, 16, 31, 33):
            self.link.txrx(2, CFG, bytes(n)).expect_err(0)

    def test_pc_12_same_handler_as_fld_cfg_for_other_subops(self):
        # valve pulse 40 ms: both addresses must produce the same EXP command and echo
        payload = bytes((1, 2, 0, 0)) + struct.pack("<I", 40) + bytes(24)
        for type_, addr in ((2, CFG), (1, 0x16)):
            self.fsw.exp.responder = lambda c: "OK pc_valve_conf_pulse 40"
            before = len(self.fsw.exp.commands)
            self.assertEqual(self.link.txrx(type_, addr, payload).expect_ack(type_, addr, 32), payload)
            self.assertEqual(self.fsw.exp.commands[before:], ["pc_valve_conf_pulse 40"])


class PcStatus(FswTestCase):
    """02/00 pc_stat: one status byte from `pc_wire_stat` (bits 0-2 state, 5 and 6 qualifiers)."""
    answer = ["PC_STAT 00"]

    @classmethod
    def respond(cls, command):
        return cls.answer[0] if command == "pc_wire_stat" else "OK"

    exp_responder = respond

    def set(self, text):
        PcStatus.answer[0] = text

    def test_pc_20_valid_status_bytes_are_passed_through(self):
        for value in (0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x21, 0x22, 0x23, 0x43, 0x63):
            with self.subTest(hex(value)):
                self.set(f"PC_STAT {value:02x}")
                self.assertEqual(self.link.txrx(2, STAT).expect_ack(2, STAT, 1), bytes((value,)))

    def test_pc_21_impossible_status_bytes_are_data_errors(self):
        for value in (0x07, 0x27, 0x20, 0x24, 0x25, 0x26, 0x41, 0x42, 0x44, 0x60, 0x61):
            with self.subTest(hex(value)):
                self.set(f"PC_STAT {value:02x}")
                self.link.txrx(2, STAT).expect_err(1)

    def test_pc_22_malformed_exp_replies(self):
        for text in ("", "OK", "PC_STAT", "PC_STAT zz", "PC_STAT 0", "PC_STAT 03x", "PC_STAT 030"):
            with self.subTest(text):
                self.set(text)
                self.link.txrx(2, STAT).expect_err(1)

    def test_pc_23_trailing_text_after_the_byte_is_allowed(self):
        self.set("PC_STAT 03 extra")
        self.link.txrx(2, STAT).expect_ack(2, STAT, 1)

    def test_pc_24_no_payload_allowed_and_command_sent(self):
        self.set("PC_STAT 00")
        before = len(self.fsw.exp.commands)
        self.link.txrx(2, STAT, b"\0").expect_err(0)
        self.assertEqual(self.fsw.exp.commands[before:], [])
        self.link.txrx(2, STAT).expect_ack(2, STAT, 1)
        self.assertEqual(self.fsw.exp.commands[before:], ["pc_wire_stat"])
