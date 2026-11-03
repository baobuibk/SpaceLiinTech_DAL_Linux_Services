"""T-FRM: RS-422 framing, CRC and decoder resynchronisation (tier A)."""
import os
import struct
import time

from fsw_auto import bdal
from fsw_auto.fixtures import FswTestCase

# Inter-byte timeout (finding D1, resolved): the requirement is 2000 ms
# (BEE_DAL_INTER_BYTE_TIMEOUT_MS). Decoder ticks arrive every ~200 ms, so a stall is dropped
# between 2.0 and ~2.4 s. A gap of 1.6 s must survive and a gap of 2.8 s must not.
SHORT_GAP, LONG_GAP = 1.6, 2.8


def uptime():
    return float(open("/proc/uptime").read().split()[0])


class Framing(FswTestCase):
    def test_frm_01_ping_exact_bytes(self):
        self.link.write(bytes.fromhex("A5 00 00 00 00 FF FF"))
        tm = self.link.read_tm()
        self.assertIsNotNone(tm, "no reply to the canonical ping frame")
        payload = tm.expect_ack(0, 0, length=4)
        self.assertEqual(tm.raw[:5], bytes.fromhex("A5 00 00 04 00"))
        self.assertEqual(tm.raw[-2:], struct.pack("<H", bdal.crc16(payload)))
        self.assertAlmostEqual(struct.unpack("<I", payload)[0], uptime(), delta=3)

    def test_frm_02_bad_crc_discarded_and_link_recovers(self):
        good = bdal.encode_tc(0, 0)
        self.link.write(bdal.encode_tc(0, 0, crc=0x1234))
        self.link.expect_silence(1.0, "bad-CRC frame")
        self.link.txrx(0, 0).expect_ack(0, 0)
        self.assertEqual(good, bytes.fromhex("A5 00 00 00 00 FF FF"))

    def test_frm_03_garbage_before_start_is_ignored(self):
        self.link.write(bytes((0x00, 0x11, 0x22, 0x5A)) + bdal.encode_tc(0, 0))
        self.link.read_tm().expect_ack(0, 0)

    def test_frm_04_crc_over_header_is_rejected(self):
        header_crc = bdal.crc16(bytes((0x00, 0x00, 0x00, 0x00)))  # header bytes, wrong scope
        self.assertNotEqual(header_crc, 0xFFFF)
        self.link.write(bdal.encode_tc(0, 0, crc=header_crc))
        self.link.expect_silence(1.0, "frame with CRC over header")
        self.link.txrx(0, 0).expect_ack(0, 0)

    def test_frm_05_max_length_payload_accepted(self):
        tm = self.link.txrx(0, 0, bytes(range(256)) * (bdal.MAX_PAYLOAD // 256), timeout=30)
        tm.expect_ack(0, 0, length=4)

    def test_frm_06_over_max_length_resets_decoder(self):
        self.link.write(bytes((0xA5, 0x00, 0x00)) + struct.pack("<H", bdal.MAX_PAYLOAD + 1))
        self.link.write(bdal.encode_tc(0, 0))
        self.link.read_tm().expect_ack(0, 0)
        self.link.expect_silence(0.5, "oversize header")

    def _split_ping(self):
        payload = bytes.fromhex("01 02 03 04")
        frame = bdal.encode_tc(0, 0, payload)
        self.assertNotIn(0xA5, frame[7:], "split tail must not contain a START byte")
        return frame[:7], frame[7:]

    def test_frm_07_stalled_frame_is_dropped(self):
        head, tail = self._split_ping()
        self.link.write(head)
        time.sleep(LONG_GAP)
        self.link.write(tail)
        self.link.expect_silence(1.0, "frame completed after a stall")
        self.link.txrx(0, 0).expect_ack(0, 0)

    def test_frm_08_two_frames_in_one_write(self):
        self.fsw.wait_tm_obc()
        self.link.write(bdal.encode_tc(0, 0) + bdal.encode_tc(4, 2) + bdal.encode_tc(0, 0))
        first, second, third = (self.link.read_tm() for _ in range(3))
        first.expect_ack(0, 0)
        second.expect_ack(4, 2, length=64)
        third.expect_ack(0, 0)

    def test_frm_09_slow_sender_within_timeout(self):
        head, tail = self._split_ping()
        self.link.write(head)
        time.sleep(SHORT_GAP)
        self.link.write(tail)
        self.link.read_tm().expect_ack(0, 0)

    def test_frm_11_start_byte_inside_payload(self):
        tm = self.link.txrx(0, 0, bytes.fromhex("A5 A5 00 00 A5 A5"))
        tm.expect_ack(0, 0, length=4)
        self.link.expect_silence(0.3, "mis-framed payload")

    def test_frm_12_every_reply_is_well_formed(self):
        for type_, addr in ((0, 0), (4, 2), (0x7E, 0x00), (3, 3), (0, 0x55)):
            tm = self.link.txrx(type_, addr)
            self.assertEqual(tm.raw[0], bdal.START)
            self.assertIn(tm.ack, (type_, bdal.ERR))

    def test_frm_13_ping_soak(self):
        count = int(os.environ.get("FSW_SOAK_N", "1000"))
        times = sorted(self.link.txrx(0, 0).ms for _ in range(count))
        p99 = times[int(len(times) * 0.99) - 1]
        self.assertLess(p99, float(os.environ.get("FSW_SOAK_P99_MS", "50")),
                        f"p99 latency {p99:.1f} ms over {count} pings")
