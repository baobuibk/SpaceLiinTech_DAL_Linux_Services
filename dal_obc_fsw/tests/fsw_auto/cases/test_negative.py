"""T-NEG: unimplemented entries, malformed lengths and random frames (tier A)."""
import os
import random

from fsw_auto import bdal
from fsw_auto.fixtures import FswTestCase
from fsw_auto.inventory import IMPLEMENTED, NEEDS_PAYLOAD, unimplemented_error


class Negative(FswTestCase):
    def test_neg_01_unimplemented_entries_return_documented_error(self):
        failures = []
        for type_ in range(0, 6):
            for addr in range(256):
                if (type_, addr) in IMPLEMENTED:
                    continue
                tm = self.link.txrx(type_, addr, timeout=2)
                want = unimplemented_error(type_, addr)
                if not (tm.is_err and tm.code == want and not tm.payload):
                    failures.append(f"{type_:02X}/{addr:02X}: want ERR {want:#04x}, got {tm!r}")
        self.assertFalse(failures, "\n".join(failures[:20]))

    def test_neg_01b_unknown_types_and_debug_range(self):
        for type_, addr in [(t, a) for t in (6, 7, 0x10, 0x80, 0xFE) for a in (0, 1, 0x80, 0xFF)] + \
                           [(0xFF, a) for a in (2, 3, 0x80, 0xFF)]:
            self.link.txrx(type_, addr).expect_err(0x00)

    def test_neg_02_empty_payload_where_one_is_required(self):
        for type_, addr in sorted(NEEDS_PAYLOAD):
            with self.subTest(f"{type_:02X}/{addr:02X} {IMPLEMENTED[(type_, addr)]}"):
                self.link.txrx(type_, addr).expect_err(0x00)

    def test_neg_03_max_length_garbage_payload_never_wedges_the_link(self):
        # 0xFF fill: type/board/index fields are out of range for every handler that
        # parses them, so nothing reaches hardware (sys_rtc and dat_del are length/type-rejected).
        blob = b"\xff" * bdal.MAX_PAYLOAD
        for type_, addr in sorted(IMPLEMENTED):
            if type_ == 0xFF:
                continue
            with self.subTest(f"{type_:02X}/{addr:02X} {IMPLEMENTED[(type_, addr)]}"):
                tm = self.link.txrx(type_, addr, blob, timeout=20)
                self.assertIn(tm.ack, (type_, bdal.ERR), repr(tm))
                if (type_, addr) in {(0x01, 0x01), (0x01, 0x00), (0x01, 0x03), (0x01, 0x04),
                                     (0x01, 0x07), (0x01, 0x14), (0x01, 0x17), (0x01, 0x1A),
                                     (0x02, 0x00), (0x02, 0x02), (0x02, 0x03), (0x02, 0x04), (0x02, 0x0A), (0x03, 0x04)}:
                    tm.expect_err(0x00)  # strict-length handlers
        self.link.txrx(0, 0).expect_ack(0, 0)

    def test_neg_04_random_frames_seeded(self):
        seed = int(os.environ.get("FSW_FUZZ_SEED", "20261028"))
        count = int(os.environ.get("FSW_FUZZ_N", "1000"))
        rng = random.Random(seed)
        # Excluded: sys_rtc (sets the clock), dat_del (deletes files), PDU/EXP-bound
        # handlers (slow timeouts on silent fakes) and the debug range.
        skip = {(0x01, 0x01), (0x03, 0x04), (0x01, 0x00), (0x01, 0x02)} | \
               {k for k in IMPLEMENTED if k[0] in (0x02, 0xFF) or k in
                {(0x01, a) for a in (0x05, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1A, 0x1B)}}
        for i in range(count):
            type_, addr = rng.choice([0, 1, 3, 4, 5, 6, 0x80]), rng.randrange(256)
            if (type_, addr) in skip or type_ == 0xFF:
                continue
            payload = bytes(rng.randrange(256) for _ in range(rng.choice([0, 1, 3, 4, 8, 32, 64])))
            tm = self.link.txrx(type_, addr, payload, timeout=5)
            self.assertIn(tm.ack, (type_, bdal.ERR), f"seed={seed} i={i} {tm!r}")
        self.link.txrx(0, 0).expect_ack(0, 0)
