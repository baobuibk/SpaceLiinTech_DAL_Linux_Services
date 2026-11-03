#!/usr/bin/env python3
"""CLI validation tests. Pass the host binary path; no SPI device is opened."""
import pathlib
import tempfile
import subprocess
import sys
import unittest

BINARY = sys.argv.pop(1) if len(sys.argv) > 1 else '/tmp/spi_flash_s25'


class CommandLineTests(unittest.TestCase):
    def test_help(self):
        result = subprocess.run([BINARY, '--help'], capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b'/dev/spidev2.0', result.stdout)

    def test_reject_before_hardware_access(self):
        cases = [
            ['write', '-f', 'missing'],
            ['erase', '-s', '4096'],
            ['read', '-a', '0xffffff', '-s', '2'],
            ['read', '-s', '0'],
            ['read', '-s', '-1'],
            ['read', '-s', '4294967296'],
            ['id', '--speed', '10000001'],
            ['id', '--select-mpu', '--no-mux'],
            ['verify'], ['unknown'],
            ['enable'], ['--sw', 'bad'], ['--sw', 'mpu', 'id'],
            ['--sw', 'fpga', 'read', '-s', '256'],
            ['--sw', 'mpu', '--sw', 'fpga'], ['--sw', 'mpu', '-s', '1'],
            ['write', '-f', 'missing', '-s', '256', '--yes'],
            ['erase', '-a', '0xffffffff', '-s', '4096', '--yes'],
        ]
        for args in cases:
            with self.subTest(args=args):
                result = subprocess.run([BINARY, *args], capture_output=True)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertNotIn(b'Cannot lock', result.stderr)
                self.assertNotIn(b'SPI configuration', result.stderr)


class SwitchTests(unittest.TestCase):
    def test_switch_gpio_without_spi(self):
        source = pathlib.Path(__file__).resolve().parents[1] / 'files/spi_flash_s25.c'
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            mux, lock, binary = root / 'brightness', root / 'lock', root / 'tool'
            subprocess.run([
                'gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                f'-DMUX_PATH="{mux}"', f'-DLOCK_PATH="{lock}"',
                str(source), '-o', str(binary),
            ], check=True)
            mux.write_text('1\n')
            args = [str(binary), '--device', str(root / 'nonexistent-spi'), '--sw', 'mpu']
            for _ in range(2):
                result = subprocess.run(args, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(mux.read_text(), '0\n')
                self.assertIn(b'switch: OK', result.stderr)
            result = subprocess.run(args[:-1] + ['fpga'], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(mux.read_text(), '1\n')
            mux.unlink()
            result = subprocess.run(args, capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertNotIn(b'switch: OK', result.stderr)


if __name__ == '__main__':
    unittest.main()
