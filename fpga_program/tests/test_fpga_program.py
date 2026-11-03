#!/usr/bin/env python3
"""Run the actual shell coordinator with temporary GPIOs and fake flash CLI."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'files/fpga_program.sh'


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.conf = self.root / 'conf'
        self.conf.mkdir()
        self.leds = self.root / 'leds'
        for pin, value in [('fpga_program_b', '0'), ('pow_onoff_fpga', '0'),
                           ('sel_flash_spi_fpga', '1')]:
            path = self.leds / pin
            path.mkdir(parents=True)
            (path / 'brightness').write_text(value + '\n')
        self.log = self.root / 'log'
        self.mock = self.root / 'mock'
        self.mock.mkdir()
        for name, body in {
            'id': 'echo 0',
            'sleep': ':',
            'flash': '''echo "$*" >> "$LOG"
[ "$(cat "$LEDS/fpga_program_b/brightness")" = 1 ] || exit 90
[ "$1" != "$FAIL" ] || exit "${FAIL_CODE:-1}"
if [ "$FAIL" = handoff ] && [ "$1" = --sw ] && [ "$2" = fpga ]; then exit 1; fi
if [ "$1" = --sw ]; then
    [ "$2" = mpu ] && v=0 || v=1
    echo "$v" > "$LEDS/sel_flash_spi_fpga/brightness"
else
    if [ "$1" = write ]; then cp "$5" "$LOG.payload"; fi
    [ "$(cat "$LEDS/sel_flash_spi_fpga/brightness")" = 0 ] || exit 91
    [ "$(cat "$LEDS/pow_onoff_fpga/brightness")" = 1 ] || exit 92
fi''',
        }.items():
            path = self.mock / name
            path.write_text('#!/bin/sh\n' + body + '\n')
            path.chmod(0o755)
        script = SOURCE.read_text().replace(
            'FLASH=/home/root/tools/bin/spi_flash_s25', f'FLASH={self.mock}/flash'
        ).replace('LEDS=/sys/class/leds', f'LEDS={self.leds}').replace(
            'LOCK=/run/lock/dal-fpga-prog.lock', f'LOCK={self.root}/lock').replace(
            'CONF_DIR=/home/bee/fpga', f'CONF_DIR={self.conf}').replace(
            'CATALOG=/run/fpga-program.list', f'CATALOG={self.root}/catalog')
        self.script = self.root / 'fpga_program'
        self.script.write_text(script)
        self.bin = self.root / 'image with spaces.bin'
        self.bin.write_bytes(bytes(range(256)))
        self.env = dict(os.environ, PATH=f'{self.mock}:' + os.environ['PATH'],
                        LOG=str(self.log), LEDS=str(self.leds), FAIL='')

    def run_tool(self, *args):
        return subprocess.run(['sh', str(self.script), *args], env=self.env,
                              capture_output=True, text=True)

    def state(self, pin):
        return (self.leds / pin / 'brightness').read_text().strip()

    def test_check_and_flash_saved_index(self):
        (self.conf / 'b space.bin').write_bytes(b'payload B')
        (self.conf / 'a.bin').write_bytes(b'payload A')
        (self.conf / 'ignored.txt').write_text('ignored')
        (self.conf / 'directory.bin').mkdir()
        result = self.run_tool('check')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '[1]: a.bin\n[2]: b space.bin\n')
        self.assertFalse(self.log.exists())
        self.assertEqual(self.state('fpga_program_b'), '0')
        # A new file sorting first must not change the saved selection.
        (self.conf / '0-new.bin').write_bytes(b'new')
        result = self.run_tool('flash', '2')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('DONE:', result.stdout)
        self.assertEqual(Path(str(self.log) + '.payload').read_bytes(), b'payload B')

    def test_flash_requires_current_file(self):
        self.assertEqual(self.run_tool('flash', '1').returncode, 2)
        candidate = self.conf / 'test.bin'
        candidate.write_bytes(b'original')
        self.assertEqual(self.run_tool('check').returncode, 0)
        for index in ['0', '-1', '01', 'x', '2', '999999999999999999999999']:
            self.assertEqual(self.run_tool('flash', index).returncode, 2)
        candidate.write_bytes(b'modified')
        result = self.run_tool('flash', '1')
        self.assertEqual(result.returncode, 2)
        self.assertIn('changed since check', result.stderr)
        candidate.unlink()
        self.assertEqual(self.run_tool('flash', '1').returncode, 2)
        self.assertFalse(self.log.exists())
        self.assertEqual(self.state('fpga_program_b'), '0')

    def test_check_empty_and_missing_directory(self):
        result = self.run_tool('check')
        self.assertEqual(result.returncode, 0)
        self.assertIn('No .bin files', result.stdout)
        self.assertEqual(self.run_tool('flash', '1').returncode, 2)
        self.conf.rmdir()
        self.assertEqual(self.run_tool('check').returncode, 1)
        self.assertFalse((self.root / 'catalog').exists())
        self.assertFalse(self.log.exists())

    def test_check_rejects_newline_names(self):
        (self.conf / 'bad\nname.bin').write_bytes(b'data')
        self.assertEqual(self.run_tool('check').returncode, 1)
        self.assertFalse((self.root / 'catalog').exists())
        self.assertFalse(self.log.exists())

    def test_success(self):
        for extra in [[], ['--power-cycle']]:
            result = self.run_tool('--bin', str(self.bin), *extra)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.state('fpga_program_b'), '0')
            self.assertEqual(self.state('sel_flash_spi_fpga'), '1')
            self.assertEqual(self.state('pow_onoff_fpga'), '1')
        commands = self.log.read_text().splitlines()[:5]
        self.assertEqual([x.split()[0] for x in commands],
                         ['--sw', 'id', 'write', 'verify', '--sw'])
        snapshot = commands[2].split()[4]
        self.assertEqual(commands[3].split()[4], snapshot)
        self.assertFalse(Path(snapshot).exists())

    def test_failures_keep_reset(self):
        for command, expected in [('id', 255), ('write', 1), ('verify', 254)]:
            with self.subTest(command=command):
                self.env['FAIL'] = command
                self.log.write_text('')
                result = self.run_tool('--bin', str(self.bin))
                self.assertEqual(result.returncode, expected)
                self.assertNotIn('DONE:', result.stdout)
                commands = [line.split()[0] for line in self.log.read_text().splitlines()]
                self.assertEqual(commands[-1], command)
                self.assertEqual(self.state('fpga_program_b'), '1')
                self.assertEqual(self.state('sel_flash_spi_fpga'), '0')
                self.assertNotIn('--sw fpga', self.log.read_text())

    def test_write_builtin_verify_mismatch(self):
        self.env.update(FAIL='write', FAIL_CODE='254')
        result = self.run_tool('--bin', str(self.bin))
        self.assertEqual(result.returncode, 254)
        self.assertEqual(self.state('fpga_program_b'), '1')
        self.assertNotIn('--sw fpga', self.log.read_text())

    def test_failed_handoff_keeps_reset(self):
        self.env['FAIL'] = 'handoff'
        self.assertEqual(self.run_tool('--bin', str(self.bin)).returncode, 1)
        self.assertEqual(self.state('fpga_program_b'), '1')
        self.assertEqual(self.state('sel_flash_spi_fpga'), '0')

    def test_invalid_input_before_hardware(self):
        for args in [[], ['--bin'], ['--wrong'], ['--bin', '/missing']]:
            self.assertEqual(self.run_tool(*args).returncode, 2)
        self.bin.write_bytes(b'')
        self.assertEqual(self.run_tool('--bin', str(self.bin)).returncode, 2)
        self.assertFalse(self.log.exists())
        self.assertEqual(self.state('fpga_program_b'), '0')

    def test_lock_conflict(self):
        import fcntl
        with (self.root / 'lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertEqual(self.run_tool('--bin', str(self.bin)).returncode, 1)
        self.assertFalse(self.log.exists())

    def test_missing_gpio_before_reset(self):
        (self.leds / 'pow_onoff_fpga/brightness').unlink()
        self.assertEqual(self.run_tool('--bin', str(self.bin)).returncode, 1)
        self.assertEqual(self.state('fpga_program_b'), '0')
        self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
