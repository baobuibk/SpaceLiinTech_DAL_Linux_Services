"""T-DAT: ICD file listing and chunk transfer against fixture files in $FSW_DATA_DIR (tier A)."""
import random
import shutil
import struct
import time

from fsw_auto import bdal
from fsw_auto.fixtures import FswTestCase

LST, SEL, GET, DES, DEL, MARK = range(6)


def today():
    return time.strftime("%Y%m%d", time.gmtime())


class DataTransfer(FswTestCase):
    """Each case starts from the same five files (see populate()), indexed by sorted name."""

    def populate(self):
        rng = random.Random(7)
        day = today()
        layout = {  # relative path -> size
            "logs/lwl/LWL_00000001.bin": 100,
            "logs/hk/HK1_20250101.csv": 0,          # zero-byte file still has one chunk
            "logs/hk/HK0_20250101.csv": 3000,
            "logs/hk/HK0_20250102.csv": 70000,      # three 32 KB chunks
            f"logs/hk/HK0_{day}.csv": 500,             # active day: must not be deletable
        }
        self.files = {}
        root = self.fsw.data_dir
        for entry in list(root.iterdir()):
            if entry.is_dir():
                shutil.rmtree(entry)
            else:
                entry.unlink()
        for rel, size in layout.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            data = bytes(rng.randrange(256) for _ in range(size))
            path.write_bytes(data)
            self.files[path.name] = (path, data)
        self.names = sorted(self.files)
        self.active = f"HK0_{day}.csv"

    def setUp(self):
        super().setUp()
        self.populate()
        self.assertEqual(self.lst(), (5, 0, 0))

    # -- helpers -----------------------------------------------------------
    def lst(self, mark=None):
        payload = b"" if mark is None else bytes((mark,))
        return struct.unpack("<III", self.link.txrx(3, LST, payload).expect_ack(3, LST, 12))

    def sel(self, idx, chunk, type_=0):
        tm = self.link.txrx(3, SEL, struct.pack("<BHH", type_, idx, chunk))
        payload = tm.expect_ack(3, SEL, 41)
        t, index, csize, name, size, crc, chunks = struct.unpack("<BII20sIII", payload)
        return dict(type=t, index=index, chunk_size=csize, name=name.rstrip(b"\0").decode(),
                    size=size, crc=crc, chunks=chunks)

    def get(self, chunk):
        return self.link.txrx(3, GET, struct.pack("<I", chunk))

    def download(self, idx, chunk_size):
        meta = self.sel(idx, chunk_size)
        data = b"".join(self.get(i).expect_ack(3, GET) for i in range(meta["chunks"]))
        return meta, data

    def index_of(self, name):
        return self.names.index(name)

    # -- cases -------------------------------------------------------------
    def test_dat_01_list_counts(self):
        self.assertEqual(self.lst(), (5, 0, 0))

    def test_dat_02_mark_filter_and_tolerated_extra_bytes(self):
        i = self.index_of("HK0_20250101.csv")
        self.sel(i, 1024)
        self.link.txrx(3, MARK, b"\x07").expect_ack(3, MARK, 4)
        try:
            self.assertEqual(self.lst(7), (1, 0, 0))
            self.assertEqual(self.lst(9), (0, 0, 0))
            self.assertEqual(self.lst(), (5, 0, 0))
            self.assertEqual(struct.unpack("<III", self.link.txrx(3, LST, b"\xff\x00\x00")
                                           .expect_ack(3, LST, 12)), (5, 0, 0))
        finally:
            self.sel(i, 1024)
            self.link.txrx(3, MARK, b"\x00").expect_ack(3, MARK)

    def test_dat_03_select_metadata_layout(self):
        for idx, name in enumerate(self.names):
            path, data = self.files[name]
            meta = self.sel(idx, 1024)
            self.assertEqual(meta, dict(type=0, index=idx, chunk_size=1024, name=name,
                                        size=len(data), crc=bdal.crc32(data),
                                        chunks=max(1, -(-len(data) // 1024))), name)
        raw = self.link.txrx(3, SEL, struct.pack("<BHH", 0, 0, 16)).payload
        self.assertEqual(raw[9:29], self.names[0].encode().ljust(20, b"\0"))

    def test_dat_04_full_download_matches_for_every_file(self):
        for idx, name in enumerate(self.names):
            with self.subTest(name):
                meta, data = self.download(idx, 1024)
                self.assertEqual(data, self.files[name][1])
                self.assertEqual(bdal.crc32(data), meta["crc"])
                self.link.txrx(3, DES).expect_ack(3, DES, 4)

    def test_dat_04b_zero_byte_file_has_one_empty_chunk(self):
        i = self.index_of("HK1_20250101.csv")
        meta = self.sel(i, 64)
        self.assertEqual((meta["size"], meta["chunks"], meta["crc"]), (0, 1, 0))
        self.get(0).expect_ack(3, GET, 0)
        self.get(1).expect_err(1)

    def test_dat_05_chunk_size_limits(self):
        big = self.index_of("HK0_20250102.csv")
        for size in (1, 255, 1024, 32768):
            with self.subTest(chunk=size):
                if size == 1:  # 70000 one-byte chunks is slow; use the 100-byte file
                    meta, data = self.download(self.index_of("LWL_00000001.bin"), 1)
                    self.assertEqual(meta["chunks"], 100)
                else:
                    meta, data = self.download(big, size)
                    self.assertEqual(len(data), 70000)
                self.assertEqual(bdal.crc32(data), meta["crc"])
        for size in (0, 32769, 65535):
            with self.subTest(bad_chunk=size):
                self.link.txrx(3, SEL, struct.pack("<BHH", 0, big, size)).expect_err(1)

    def test_dat_06_get_requires_a_live_selection(self):
        self.get(0).expect_err(1)                              # nothing selected
        self.sel(0, 64)
        self.link.txrx(3, DES).expect_ack(3, DES, 4)
        self.get(0).expect_err(1)                              # released
        self.sel(0, 64)
        self.lst()
        self.get(0).expect_err(1)                              # new listing invalidates it

    def test_dat_07_chunk_index_out_of_range(self):
        meta = self.sel(self.index_of("HK0_20250101.csv"), 1024)
        self.get(meta["chunks"] - 1).expect_ack(3, GET)
        self.get(meta["chunks"]).expect_err(1)
        self.get(0xFFFFFFFF).expect_err(1)

    def test_dat_08_select_argument_errors(self):
        self.link.txrx(3, SEL, struct.pack("<BHH", 3, 0, 64)).expect_err(0)   # bad type
        self.link.txrx(3, SEL, struct.pack("<BHH", 0, 99, 64)).expect_err(1)  # bad index
        self.link.txrx(3, SEL, struct.pack("<BHH", 0, 0xFFFF, 64)).expect_err(1)
        self.link.txrx(3, SEL, b"\x00\x00\x00\x40").expect_err(0)              # 4 bytes
        for sci in (1, 2):                                                      # no SCI board yet
            self.link.txrx(3, SEL, struct.pack("<BHH", sci, 0, 64)).expect_err(1)

    def test_dat_09_length_rules_short_rejected_long_tolerated(self):
        self.link.txrx(3, GET, b"\x00\x00\x00").expect_err(0)
        self.link.txrx(3, DEL, b"\x00\x00").expect_err(0)
        self.link.txrx(3, MARK, b"").expect_err(0)
        self.link.txrx(3, SEL, struct.pack("<BHH", 0, 0, 64) + b"\xee").expect_ack(3, SEL, 41)
        self.link.txrx(3, GET, struct.pack("<I", 0) + b"\xee").expect_ack(3, GET)
        self.link.txrx(3, MARK, b"\x00\xee").expect_ack(3, MARK, 4)

    def test_dat_10_mark_rules(self):
        self.link.txrx(3, MARK, b"\x01").expect_err(1)       # nothing selected
        self.sel(0, 64)
        self.link.txrx(3, MARK, b"\xff").expect_err(0)       # reserved
        self.link.txrx(3, MARK, b"\xfe").expect_ack(3, MARK, 4)
        self.link.txrx(3, MARK, b"\x00").expect_ack(3, MARK, 4)

    def test_dat_11_delete_removes_file_and_clears_selection(self):
        name = "HK0_20250101.csv"
        path = self.files[name][0]
        self.sel(self.index_of(name), 64)
        payload = self.link.txrx(3, DEL, struct.pack("<BI", 0, self.index_of(name))).expect_ack(3, DEL, 12)
        self.assertEqual(struct.unpack("<III", payload), (4, 0, 0))
        self.assertFalse(path.exists())
        self.get(0).expect_err(1)
        self.assertEqual(self.lst(), (4, 0, 0))

    def test_dat_12_active_day_file_cannot_be_deleted(self):
        i = self.index_of(self.active)
        self.link.txrx(3, DEL, struct.pack("<BI", 0, i)).expect_err(1)
        self.assertTrue(self.files[self.active][0].exists())
        self.assertEqual(self.lst(), (5, 0, 0))

    def test_dat_12b_delete_argument_errors(self):
        self.link.txrx(3, DEL, struct.pack("<BI", 3, 0)).expect_err(0)
        self.link.txrx(3, DEL, struct.pack("<BI", 0, 99)).expect_err(1)
        self.link.txrx(3, DEL, struct.pack("<BI", 1, 0)).expect_err(1)
        self.assertEqual(self.lst(), (5, 0, 0))

    def test_dat_13_file_grown_between_list_and_select_is_inconsistent(self):
        """Characterisation (finding D11): size comes from the dat_lst scan, the CRC from a
        fresh read at dat_sel. If a file grows in between, the advertised CRC covers more
        bytes than the advertised size, so a client's whole-file CRC check fails. The docs'
        'retry if size or CRC changes' is not detectable from dat_sel alone."""
        name = "HK0_20250101.csv"
        path, data = self.files[name]
        with path.open("ab") as f:
            f.write(b"appended-after-scan")
        meta, got = self.download(self.index_of(name), 1024)
        self.assertEqual(meta["size"], len(data))           # stale size from the scan
        self.assertEqual(got, data)                         # served bytes follow that size
        self.assertNotEqual(bdal.crc32(got), meta["crc"])   # CRC was taken over the grown file
        self.lst()                                          # re-list: consistent again
        meta, got = self.download(self.index_of(name), 1024)
        self.assertEqual(bdal.crc32(got), meta["crc"])

    def test_dat_14_console_shares_the_selection(self):
        meta = self.sel(self.index_of("HK0_20250101.csv"), 100)
        wire = self.get(1).expect_ack(3, GET)
        import json
        reply = json.loads(self.fsw.console.run("--json", "dat_get", "1"))
        self.assertTrue(reply["ok"])
        self.assertEqual(bytes.fromhex(reply["payload_hex"]), wire)
        self.assertEqual(wire, self.files[meta["name"]][1][100:200])

    def test_dat_15_unreadable_file_after_selection(self):
        name = "HK0_20250101.csv"
        self.sel(self.index_of(name), 64)
        self.files[name][0].unlink()
        self.get(0).expect_err(1)
