import pathlib
import subprocess
import tempfile
import unittest

BIN = pathlib.Path(__file__).resolve().parents[1] / 'build-host/fpga_prog'

class CliTests(unittest.TestCase):
    @staticmethod
    def bit_file(payload, part=b'7a35tftg256\0'):
        header = b'\0\x09' + bytes.fromhex('0ff00ff00ff00ff000') + b'\0\x01'
        for key, value in [(b'a', b'blink_top\0'), (b'b', part),
                           (b'c', b'2026/10/25\0'), (b'd', b'08:26:36\0')]:
            header += key + len(value).to_bytes(2, 'big') + value
        return header + b'e' + len(payload).to_bytes(4, 'big') + payload

    def test_no_hardware_dry_run(self):
        for command in ('id', 'reload'):
            result = subprocess.run([str(BIN), '--dry-run', command], capture_output=True)
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
        for args in [[], ['program'], ['erase'], ['id', 'extra'],
                     ['reload', 'extra'], ['--unknown']]:
            self.assertEqual(subprocess.run([str(BIN), *args], capture_output=True).returncode, 2)

    def test_bit_container(self):
        payload = b'\xff' * 32 + bytes.fromhex('000000bb11220044') + \
                  b'\xff' * 8 + bytes.fromhex('aa995566') + b'\0' * 256
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / 'image.bit'
            for data, valid in [
                (self.bit_file(payload), True),
                (self.bit_file(payload, b'7a100tftg256\0'), False),
                (self.bit_file(b'bad payload'), False),
                (self.bit_file(payload)[:-1], False),
                (self.bit_file(payload) + b'junk', False),
            ]:
                image.write_bytes(data)
                result = subprocess.run([str(BIN), '--dry-run', 'program', str(image)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, valid, result.stderr)
                if valid:
                    self.assertIn(f'image {len(payload)} bytes', result.stdout)

if __name__ == '__main__':
    unittest.main()
