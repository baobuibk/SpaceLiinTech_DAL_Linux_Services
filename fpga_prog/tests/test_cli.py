import pathlib
import subprocess
import tempfile
import unittest

BIN = pathlib.Path(__file__).resolve().parents[1] / 'build-host/fpga_prog'

class CliTests(unittest.TestCase):
    def test_no_hardware_dry_run(self):
        result = subprocess.run([str(BIN), '--dry-run', 'id'], capture_output=True)
        self.assertEqual(result.returncode, 0)

    def test_image_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / 'image.bin'
            for payload, valid in [
                (b'', False), (b'not a bitstream', False),
                (b'\xff'*32 + bytes.fromhex('aa995566') + b'\0'*256, True),
                (b'\xff'*32 + bytes.fromhex('000000bb11220044') + b'\xff'*8 + bytes.fromhex('aa995566'), True),
                (b'FMOD' + b'\xff'*32 + bytes.fromhex('aa995566'), False),
            ]:
                image.write_bytes(payload)
                result = subprocess.run([str(BIN), '--dry-run', 'program', str(image)], capture_output=True)
                self.assertEqual(result.returncode == 0, valid, result.stderr)
            with image.open('wb') as f:
                f.truncate(16*1024*1024+1)
            result = subprocess.run([str(BIN), '--dry-run', 'program', str(image)], capture_output=True)
            self.assertEqual(result.returncode, 2)

    def test_bad_arguments(self):
        for args in [[], ['program'], ['erase'], ['id', 'extra'], ['--unknown']]:
            self.assertEqual(subprocess.run([str(BIN), *args], capture_output=True).returncode, 2)

if __name__ == '__main__':
    unittest.main()
