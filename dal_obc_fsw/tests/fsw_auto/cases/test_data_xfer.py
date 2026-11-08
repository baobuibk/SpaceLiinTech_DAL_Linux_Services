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
            "logs/hk/HK_SMALL.csv": 100,
            "logs/hk/HK1_20250101.csv": 0,          # zero-byte file has zero chunks
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
    def lst(self, mark=0xFF):
        payload = bytes((mark,))
        return struct.unpack("<III", self.link.txrx(3, LST, payload).expect_ack(3, LST, 12))

    def sel(self, idx, chunk, type_=0):
        tm = self.link.txrx(3, SEL, struct.pack("<BIH", type_, idx, chunk))
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

    def test_dat_02_mark_filter_and_strict_payload_length(self):
        i = self.index_of("HK0_20250101.csv")
        self.sel(i, 1024)
        self.link.txrx(3, MARK, b"\x07").expect_ack(3, MARK, 4)
        try:
            self.assertEqual(self.lst(7), (1, 0, 0))
            self.assertEqual(self.lst(9), (0, 0, 0))
            self.assertEqual(self.lst(), (5, 0, 0))
            self.assertEqual(self.lst(0), (4, 0, 0))
            self.assertEqual(self.lst(0xFF), (5, 0, 0))
            self.sel(i, 1024)
            for bad in (b"", b"\xff\x00", b"\xff\x00\x00"):
                self.link.txrx(3, LST, bad).expect_err(0)
                # Invalid list frames must not discard the current selection.
                self.assertEqual(self.get(0).expect_ack(3, GET), self.files[self.names[i]][1][:1024])
        finally:
            self.sel(i, 1024)
            self.link.txrx(3, MARK, b"\x00").expect_ack(3, MARK)

    def test_dat_03_select_metadata_layout(self):
        for idx, name in enumerate(self.names):
            path, data = self.files[name]
            meta = self.sel(idx, 1024)
            self.assertEqual(meta, dict(type=0, index=idx, chunk_size=1024, name=name,
                                        size=len(data), crc=bdal.crc32(data),
                                        chunks=-(-len(data) // 1024)), name)
        raw = self.link.txrx(3, SEL, struct.pack("<BIH", 0, 0, 16)).payload
        self.assertEqual(raw[9:29], self.names[0].encode().ljust(20, b"\0"))

    def test_dat_04_full_download_matches_for_every_file(self):
        for idx, name in enumerate(self.names):
            with self.subTest(name):
                meta, data = self.download(idx, 1024)
                self.assertEqual(data, self.files[name][1])
                self.assertEqual(bdal.crc32(data), meta["crc"])
                self.link.txrx(3, DES).expect_ack(3, DES, 4)

    def test_dat_04b_zero_byte_file_has_zero_chunks(self):
        i = self.index_of("HK1_20250101.csv")
        meta = self.sel(i, 64)
        self.assertEqual((meta["size"], meta["chunks"], meta["crc"]), (0, 0, 0))
        self.get(0).expect_err(1)
        self.get(1).expect_err(1)

    def test_dat_05_chunk_size_limits(self):
        big = self.index_of("HK0_20250102.csv")
        for size in (1, 255, 1024, 32768):
            with self.subTest(chunk=size):
                if size == 1:  # 70000 one-byte chunks is slow; use the 100-byte file
                    meta, data = self.download(self.index_of("HK_SMALL.csv"), 1)
                    self.assertEqual(meta["chunks"], 100)
                else:
                    meta, data = self.download(big, size)
                    self.assertEqual(len(data), 70000)
                self.assertEqual(bdal.crc32(data), meta["crc"])
        for size in (0, 32769, 65535):
            with self.subTest(bad_chunk=size):
                self.link.txrx(3, SEL, struct.pack("<BIH", 0, big, size)).expect_err(1)

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
        self.link.txrx(3, SEL, struct.pack("<BIH", 3, 0, 64)).expect_err(0)   # bad type
        self.link.txrx(3, SEL, struct.pack("<BIH", 0, 99, 64)).expect_err(1)  # bad index
        self.link.txrx(3, SEL, struct.pack("<BIH", 0, 0xFFFF, 64)).expect_err(1)
        self.link.txrx(3, SEL, b"\x00\x00\x00\x40").expect_err(0)              # 4 bytes
        for idx in (65536, 0xFFFFFFFF):
            self.link.txrx(3, SEL, struct.pack("<BIH", 0, idx, 64)).expect_err(1)
        self.link.txrx(3, SEL, struct.pack("<BHH", 0, 0, 64)).expect_err(0)
        for sci in (1, 2):                                                      # no SCI board yet
            self.link.txrx(3, SEL, struct.pack("<BIH", sci, 0, 64)).expect_err(1)

    def test_dat_09_length_rules_are_exact(self):
        self.link.txrx(3, DES, b"\x00").expect_err(0)
        self.link.txrx(3, GET, b"\x00\x00\x00").expect_err(0)
        self.link.txrx(3, DEL, b"\x00\x00").expect_err(0)
        self.link.txrx(3, MARK, b"").expect_err(0)
        self.link.txrx(3, SEL, struct.pack("<BIH", 0, 0, 64) + b"\xee").expect_err(0)
        self.link.txrx(3, GET, struct.pack("<I", 0) + b"\xee").expect_err(0)
        self.link.txrx(3, MARK, b"\x00\xee").expect_err(0)

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

    def test_dat_13_changed_size_requires_relist(self):
        name = "HK0_20250101.csv"
        path, data = self.files[name]
        with path.open("ab") as f:
            f.write(b"appended-after-scan")
        self.link.txrx(3, SEL, struct.pack("<BIH", 0, self.index_of(name), 1024)).expect_err(1)
        self.lst()
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

    def test_dat_16_console_v12_layout_and_max_chunk(self):
        import json
        idx = self.index_of("HK0_20250102.csv")
        def console(*args):
            return json.loads(self.fsw.console.run("--json", *args))
        reply = console("dat_sel", "0", str(idx), "32768")
        self.assertTrue(reply["ok"])
        self.assertEqual(len(bytes.fromhex(reply["payload_hex"])), 41)
        chunk = console("dat_get", "0")
        self.assertTrue(chunk["ok"])
        self.assertEqual(bytes.fromhex(chunk["payload_hex"]), self.files[self.names[idx]][1][:32768])
        self.assertTrue(console("dat_mrk", "7")["ok"])
        self.assertTrue(console("dat_mark", "0")["ok"])
        # Large U32 indices reach the backend and return DATA, rather than wrapping to 0.
        for index in (65535, 65536, 0xFFFFFFFF):
            reply = console("dat_sel", "0", str(index), "64")
            self.assertFalse(reply["ok"])
            self.assertEqual(reply["code"], 1)

    def test_dat_17_categories_hk_images_lwl_pc(self):
        import json
        for rel, data in [("logs/lwl/LWL.bin", b"lwl"), ("logs/pc/PC.bin", b"pc"),
                          ("images/exp/IR.bin", b"image"), ("data/other/old.bin", b"excluded")]:
            path = self.fsw.data_dir / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.assertEqual(self.lst(), (5, 1, 2))
        for index, name, content in [(0, "LWL.bin", b"lwl"), (1, "PC.bin", b"pc")]:
            meta = self.sel(index, 32, type_=2)
            self.assertEqual(meta["name"], name)
            self.assertEqual(self.get(0).expect_ack(3, GET), content)
        for alias, kind in [("hk", 0), ("images", 1), ("logs", 2), ("sci2", 2)]:
            reply = json.loads(self.fsw.console.run("--json", "dat_sel", alias, "0", "32"))
            self.assertTrue(reply["ok"])
            self.assertEqual(bytes.fromhex(reply["payload_hex"])[0], kind)
