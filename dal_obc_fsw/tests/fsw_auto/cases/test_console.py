"""T-CON: local socket text/JSON behaviour and parity with the RS-422 wire (tier A)."""
import json
import socket
import threading

from fsw_auto.fixtures import FswTestCase

JSON_KEYS = {"ok", "command", "type", "address", "ack", "code", "length", "text", "payload_hex"}


class Console(FswTestCase):
    def j(self, *cmd):
        return json.loads(self.fsw.console.run("--json", *cmd))

    def test_con_02_bad_syntax_is_rejected(self):
        for cmd in (["ping", "extra"], ["nonsense"], ["dat_sel", "9", "0", "1"],
                    ["pdu_vout", "set", "799"], ["pdu_vout", "set", "20001"],
                    ["dat_mark", "255"], ["exp_tlm", "x" * 20], ["shell"]):
            with self.subTest(cmd):
                self.assertEqual(self.j(*cmd), {"ok": False, "error": "invalid command or arguments"})

    def test_con_03_json_schema_and_error_encoding(self):
        reply = self.j("ping")
        self.assertEqual(set(reply), JSON_KEYS)
        self.assertEqual((reply["ok"], reply["type"], reply["address"], reply["length"]),
                         (True, 0, 0, 4))
        err = self.j("raw", "6", "0")
        self.assertEqual((err["ok"], err["ack"], err["code"], err["length"]), (False, 255, 0, 0))

    def test_con_04_parity_with_wire(self):
        self.fsw.write_snapshot()
        self.fsw.wait_tm_obc()
        wire = self.link.txrx(4, 2).expect_ack(4, 2, 64)
        self.assertEqual(self.j("tm_obc_get")["payload_hex"], wire.hex())
        for type_, addr, cmd in ((6, 0, ["raw", "6", "0"]), (5, 0, ["raw", "5", "0"]),
                                 (4, 1, ["raw", "4", "1"])):
            tm = self.link.txrx(type_, addr)
            reply = self.j(*cmd)
            self.assertEqual((reply["ack"], reply["code"], reply["length"]),
                             (tm.ack, tm.code, len(tm.payload)))

    def test_con_05_raw_reaches_the_same_dispatcher(self):
        reply = self.j("raw", "0", "0")
        self.assertTrue(reply["ok"])
        self.assertEqual(reply["length"], 4)

    def test_con_06_text_reply_starts_ok_or_error(self):
        self.assertTrue(self.fsw.console.run("ping").startswith("OK ping:"))
        self.assertTrue(self.fsw.console.run("raw", "6", "0").startswith("ERROR"))

    def test_con_07_request_limit(self):
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(str(self.fsw.socket_path))
            s.sendall(b"x" * 5000)
            s.shutdown(socket.SHUT_WR)
            self.assertIn(b"too long", s.recv(200))

    def test_con_09_socket_mode_is_0600(self):
        self.assertEqual(self.fsw.socket_path.stat().st_mode & 0o777, 0o600)

    def test_con_10_simultaneous_rs422_and_console_load(self):
        errors = []

        def hammer():
            for _ in range(40):
                if '"ok":true' not in self.fsw.console.run("--json", "ping").replace(" ", ""):
                    errors.append("console ping failed")
        threads = [threading.Thread(target=hammer) for _ in range(3)]
        for t in threads:
            t.start()
        for _ in range(300):
            self.link.txrx(0, 0).expect_ack(0, 0, 4)
        for t in threads:
            t.join()
        self.assertEqual(errors, [])

    def test_con_11_leading_zero_numbers_are_octal(self):
        """Characterisation (finding D19): unsigned arguments use strtoul base 0, so `010` is 8,
        not 10, and `09` is rejected. Easy to trip over when an operator zero-pads a value."""
        reply = self.j("pwr_stat", "PDU", "get", "mask", "010")       # mask 8 -> INPUT_5V only
        self.assertTrue(reply["ok"], reply)
        self.assertEqual(bytes.fromhex(reply["payload_hex"])[1], 1)    # one record
        self.assertEqual(self.j("pwr_stat", "PDU", "get", "mask", "09"),
                         {"ok": False, "error": "invalid command or arguments"})
