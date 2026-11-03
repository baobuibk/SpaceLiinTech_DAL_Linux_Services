"""T-TM / T-PING: housekeeping telemetry against a fixture snapshot (tier A)."""
import struct
import time

from fsw_auto import bdal
from fsw_auto.fixtures import FswTestCase, base_snapshot, expected_obc_wire


class Ping(FswTestCase):
    def test_ping_01_uptime_monotonic(self):
        first = struct.unpack("<I", self.link.txrx(0, 0).expect_ack(0, 0, 4))[0]
        time.sleep(1.1)
        second = struct.unpack("<I", self.link.txrx(0, 0).expect_ack(0, 0, 4))[0]
        self.assertGreater(second, first)

    def test_ping_02_payload_ignored(self):
        self.link.txrx(0, 0, b"12345").expect_ack(0, 0, 4)


class ObcTelemetry(FswTestCase):
    def setUp(self):
        super().setUp()
        self.fsw.write_snapshot()
        self.fsw.wait_tm_obc()

    def check(self, **overrides):
        snap = self.fsw.write_snapshot(**overrides)
        deadline = time.monotonic() + 5
        want = expected_obc_wire(snap)
        while True:  # the cache refreshes once per second
            tm = self.link.txrx(4, 2)
            if not tm.is_err and tm.payload == want:
                return tm
            if time.monotonic() > deadline:
                got = "" if tm.is_err else tm.payload.hex(" ")
                self.fail(f"tm_obc_get mismatch\n want {want.hex(' ')}\n got  {got} ({tm!r})")
            time.sleep(0.2)

    def test_tm_01_all_valid(self):
        tm = self.check()
        self.assertEqual(struct.unpack_from("<H", tm.payload)[0], 0x01FF)

    def test_tm_02_partial_validity_zeroes_invalid_fields(self):
        tm = self.check(temp_ntc_c=[12.34, "N/A", 30, -45.0], bme_gas_valid=0,
                        bme_gas_resistance_ohm=0, pac_u2_valid=0)
        # temp, NTC1, NTC3, BME, U6 valid; NTC2 N/A, NTC4 < -40, gas and U2 invalid
        self.assertEqual(struct.unpack_from("<H", tm.payload)[0], 0x00AB)

    def test_tm_03_cache_retained_when_file_disappears(self):
        # hk_service keeps the last complete sample through transient read failures.
        self.fsw.remove_snapshot()
        deadline = time.monotonic() + 2.5
        while time.monotonic() < deadline:
            self.link.txrx(4, 2).expect_ack(4, 2, 64)
            time.sleep(0.3)

    def test_tm_03b_cached_sample_expires_after_30_s(self):
        now = int(time.time())
        self.fsw.write_snapshot(obc_timestamp=now - 27)  # still acceptable when read
        deadline = time.monotonic() + 5
        while struct.unpack_from("<I", self.link.txrx(4, 2).payload, 2)[0] != now - 27:
            self.assertLess(time.monotonic(), deadline, "cache never picked up the aged sample")
            time.sleep(0.2)
        self.fsw.remove_snapshot()
        self.fsw.wait_tm_obc(want_ack=False, timeout=8).expect_err(1)

    def test_tm_04_length_must_be_zero(self):
        self.link.txrx(4, 2, b"\x00").expect_err(0)

    def test_tm_05_tm_get_legacy(self):
        snap = self.fsw.write_snapshot(boot_count=9)
        time.sleep(1.5)
        payload = self.link.txrx(4, 0).expect_ack(4, 0, 29)
        epoch, _status, boot, onboard, baro_t, press, hum = struct.unpack_from("<IbIhhHH", payload)
        self.assertEqual((epoch, boot, onboard, baro_t), (snap["obc_timestamp"], 9, 2525, 2400))
        self.assertEqual((press, hum), (10133, 485))  # Pa/10, %RH x10 (u16 fits)

    def test_tm_06_tm_cfg_not_implemented(self):
        self.link.txrx(4, 1).expect_err(3)


class ObcTelemetryFromColdStart(FswTestCase):
    """No usable snapshot at boot: every rejection reason must give data error 0x01."""
    with_snapshot = False

    def test_tm_08_rejected_snapshots_give_data_error(self):
        now = int(time.time())
        self.link.txrx(4, 0).expect_err(1)
        self.link.txrx(4, 2).expect_err(1)
        for why, snap in (
                ("older than 30 s", base_snapshot(now - 40, obc_timestamp=now - 40)),
                ("timestamp in the future", base_snapshot(now + 60, obc_timestamp=now + 60)),
                ("schema_version 2", base_snapshot(schema_version=2)),
                ("offline", base_snapshot(online=0)),
                ("wrong board", base_snapshot(board="exp")),
                ("rail names mismatch", base_snapshot(pwr_rail_names=["x"] * 8))):
            self.fsw.write_snapshot(snap)
            time.sleep(2.2)
            with self.subTest(why):
                self.link.txrx(4, 2).expect_err(1)
                self.link.txrx(4, 0).expect_err(1)
        self.fsw.write_snapshot()
        self.fsw.wait_tm_obc()
