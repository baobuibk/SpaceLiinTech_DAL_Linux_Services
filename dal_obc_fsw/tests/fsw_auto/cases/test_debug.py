"""T-SHL / T-DBG: debug shell and handoff (tier A, sandboxed)."""
import struct
import time
import unittest

from fsw_auto import bdal
from fsw_auto.fixtures import Fsw, FswTestCase

HANDOFF = b"DEBUG_HANDOFF_OK"


def shell(link, command, timeout=10):
    tm = link.txrx(0xFF, 1, command.encode() if isinstance(command, str) else command, timeout)
    payload = tm.expect_ack(bdal.DEBUG_ACK, 1)
    status, code, truncated, length = struct.unpack_from("<BBBI", payload)
    out = payload[7:]
    assert len(out) == length, f"declared {length} output bytes, got {len(out)}"
    return status, code, truncated, out


class Shell(FswTestCase):
    def test_shl_01_echo(self):
        self.assertEqual(shell(self.link, "echo hi"), (0, 0, 0, b"hi\n"))

    def test_shl_02_exit_signal_and_missing_command(self):
        self.assertEqual(shell(self.link, "exit 7")[:2], (0, 7))
        self.assertEqual(shell(self.link, "kill -9 $$")[:2], (1, 9))
        self.assertEqual(shell(self.link, "no_such_command_xyz")[:2], (0, 127))

    def test_shl_02b_stderr_is_captured(self):
        self.assertEqual(shell(self.link, "echo err >&2")[3], b"err\n")

    def test_shl_03_output_truncated_at_20480(self):
        status, code, truncated, out = shell(self.link, "head -c 25000 /dev/zero | tr '\\0' x")
        self.assertEqual((status, code, truncated, len(out)), (0, 0, 1, 20480))

    def test_shl_04_command_length_limits(self):
        self.link.txrx(0xFF, 1, b"").expect_err(0)
        ok = ("echo ok #" + "x" * 2048)[:2048]
        self.assertEqual(shell(self.link, ok)[3], b"ok\n")
        self.link.txrx(0xFF, 1, b"x" * 2049).expect_err(0)

    def test_shl_05_cd_does_not_persist(self):
        shell(self.link, "cd /")
        self.assertNotEqual(shell(self.link, "pwd")[3], b"/\n")

    def test_shl_06_binary_safe_start_byte_in_output(self):
        out = shell(self.link, "printf '\\245\\245\\000\\377'")[3]
        self.assertEqual(out, b"\xa5\xa5\x00\xff")

    def test_shl_07_long_command_delays_other_commands(self):
        t0 = time.perf_counter()
        self.link.send(0xFF, 1, b"sleep 1")
        self.link.send(0, 0)
        first, second = self.link.read_tm(5), self.link.read_tm(5)
        self.assertEqual((first.ack, second.ack), (bdal.DEBUG_ACK, 0))
        self.assertGreater(time.perf_counter() - t0, 0.9)

    def test_dbg_01_bad_handoff_frames_are_rejected(self):
        self.link.txrx(0xFF, 0, b"DEBUG_HANDOFF_NO").expect_err(0)
        self.link.txrx(0xFF, 0, HANDOFF + b"!").expect_err(0)
        self.link.txrx(0xFF, 0, b"").expect_err(0)
        self.link.txrx(0, 0).expect_ack(0, 0)


class Handoff(unittest.TestCase):
    """Destructive: the daemon exits. Own instance; systemctl is a stub (see fixtures)."""

    def test_dbg_02_handoff_acks_then_exits_and_starts_getty(self):
        fsw = Fsw().start()
        try:
            tm = fsw.link.txrx(0xFF, 0, HANDOFF)
            payload = tm.expect_ack(bdal.DEBUG_ACK, 0, length=4)
            self.assertGreater(struct.unpack("<I", payload)[0], 0)
            self.assertEqual(fsw.proc.wait(10), 0, "clean exit expected")
            self.assertIn("start serial-getty@ttyS1.service", fsw.systemctl_log.read_text())
        finally:
            fsw.stop()
