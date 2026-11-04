"""End-to-end table-6 notification and UART data collection."""
import pathlib
import json
import time
import unittest
import zlib

from fsw_auto.fake_exp import ExpModel
from fsw_auto.fixtures import Fsw
from test_exp_csp import ICD_ROWS, INVENTORY_ROWS, ROWS, THERMAL_ROWS


BOOT, OBJECT = 9, 17
PAYLOAD = bytes(range(96))
CRC = zlib.crc32(PAYLOAD)


class DataExp(ExpModel):
    def __init__(self, ack_path, corrupt=False, noisy=False):
        super().__init__()
        self.ack_path = ack_path
        self.corrupt = corrupt
        self.noisy = noisy
        self.reads = {}

    def __call__(self, command):
        if command == f"data_ready_collect {OBJECT}":
            return (f"DATA boot={BOOT} object={OBJECT} type=3 source=1 encoding=2 "
                    f"width=32 height=24 length={len(PAYLOAD)} crc32=0x{CRC:08X} "
                    "run=5 seq=11 ms=100000")
        if command.startswith("data_read "):
            _, boot, obj, offset, length = command.split()
            if (int(boot), int(obj)) != (BOOT, OBJECT):
                return "DATA result=5"
            start, count = int(offset), int(length)
            self.reads[start] = self.reads.get(start, 0) + 1
            if self.noisy and start == 32 and self.reads[start] == 1:
                return "[async] no hex response this time"
            data = PAYLOAD[start:start + count]
            if self.corrupt and start == 32:
                data = bytes([data[0] ^ 0xFF]) + data[1:]
            hex_line = " ".join(f"{v:02X}" for v in data)
            if self.noisy:
                return ("[async] background log\r\n" * 40) + hex_line
            return hex_line
        if command == f"data_ready_done {BOOT} {OBJECT} {len(PAYLOAD)} {CRC}":
            self.ack_path.write_text("acked")
            return "DATA result=0"
        return super().__call__(command)


def make_client(path, ack_path):
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        f"ROWS={ROWS!r}\nTHERMAL={THERMAL_ROWS!r}\nICD={ICD_ROWS!r}\n"
        f"INVENTORY={INVENTORY_ROWS!r}\nACK=pathlib.Path({str(ack_path)!r})\n"
        f"CRC={CRC}\nLENGTH={len(PAYLOAD)}\n"
        "verb=sys.argv[2]\n"
        "if verb == 'ping': print('reply in 1 ms')\n"
        "elif verb == 'getall':\n"
        "  table=sys.argv[4]\n"
        "  if table == '2': print(ROWS)\n"
        "  elif table == '3': print(THERMAL)\n"
        "  elif table == '12': print(ICD)\n"
        "  elif table == '6':\n"
        "    rows=[]\n"
        "    values={'pending': 0 if ACK.exists() else 1, 'ready': 0 if ACK.exists() else 1,\n"
        "      'object_id': 0 if ACK.exists() else 17, 'length': 0 if ACK.exists() else LENGTH,\n"
        "      'crc32': 0 if ACK.exists() else CRC, 'type_count': '0 0 0' if ACK.exists() else '0 0 1'}\n"
        "    for row in INVENTORY.splitlines():\n"
        "      p=row.split()\n"
        "      if len(p)>2 and p[1] in values: row=' '.join(p[:3]+[str(values[p[1]])])\n"
        "      rows.append(row)\n"
        "    print('\\n'.join(rows))\n"
        "  else: print('OK')\n"
        "else: print('OK')\n"
    )
    path.chmod(0o755)


class ExpDataCollection(unittest.TestCase):
    def run_case(self, corrupt, noisy=False):
        fsw = Fsw()
        ack = fsw.tmp / "ack"
        client = fsw.tmp / "lite-client"
        make_client(client, ack)
        fsw.exp_responder = DataExp(ack, corrupt, noisy)
        fsw.extra_env = {"DAL_EXP_CSP_ENABLE": "1", "DAL_EXP_CSP_CLIENT": str(client),
                         "DAL_EXP_DATA_ENABLE": "1", "DAL_EXP_DATA_DIR": str(fsw.data_dir)}
        path = fsw.data_dir / "images/exp" / f"EXP_C1_B{BOOT:08X}_O{OBJECT:08X}_E2.bin"
        try:
            fsw.start()
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline and not (ack.exists() if not corrupt else path.exists()):
                time.sleep(0.1)
            if corrupt:
                self.assertFalse(ack.exists(), fsw.log_tail())
                self.assertFalse(path.exists(), fsw.log_tail())
                self.assertIn("[EXP-DATA] CRC mismatch:", fsw.log_tail())
            else:
                self.assertTrue(ack.exists(), fsw.log_tail())
                self.assertEqual(path.read_bytes(), PAYLOAD)
                if noisy:
                    self.assertEqual(fsw.exp_responder.reads, {0: 1, 32: 2, 64: 1})
                    self.assertIn("retry read:", fsw.log_tail())
                self.assertIn("data_ready_done", " ".join(fsw.exp.commands))
                reply = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "6"))
                snapshot = json.loads(reply["text"])
                self.assertEqual(snapshot["download"]["object_id"], OBJECT)
                self.assertEqual(snapshot["download"]["file"],
                                 f"images/exp/{path.name}")
        finally:
            fsw.stop()
            if fsw.proc and fsw.proc.stdout:
                fsw.proc.stdout.close()

    def test_download_verifies_crc_then_acknowledges(self):
        self.run_case(False)

    def test_noisy_uart_retries_only_failed_chunk(self):
        self.run_case(False, noisy=True)

    def test_corrupt_uart_data_remains_on_mcu(self):
        self.run_case(True)


if __name__ == "__main__":
    unittest.main()
