"""T-MIP: MIPI camera path (tier A). `dal_obc_fsw --gui-hw mipi-*` and the unmodified capture_ar2020.sh
run against stub `media-ctl`, `v4l2-ctl` and `uname`, fake LED-class files and plain files for the two sysfs switches.

The route command sleeps one second for the switch to settle, so a route costs about 1.1 s.
"""
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import threading
import time
import unittest

import fcntl

from fsw_auto import fixtures
from fsw_auto.fixtures import FSW, Fsw

SCRIPT = (fixtures.ROOT.parents[1] / "sources/meta-src-dalobcv110/recipes-core/custom-tools/files/"
          "rootfs-overlay/home/root/tools/camera_tools/capture_ar2020.sh")
FRAME_BYTES = 5120 * 3840 * 2
LEDS = ("pow_onoff_ai", "pow_onoff_fpga", "usb_1_pwr_en", "usb_2_pwr_en", "usb_3_pwr_en", "usb_4_pwr_en",
        "pow_mipicam_1", "pow_mipicam_2", "en_i2c_mipicam_1", "en_i2c_mipicam_2", "sel_mipicam_on2_off1")

MEDIA_STUB = """#!/bin/sh
echo "media-ctl $*" >> "$STUB_LOG"
[ -n "$STUB_FAIL_MEDIA" ] && exit 1
exit 0
"""
V4L2_STUB = """#!/bin/sh
echo "v4l2-ctl $*" >> "$STUB_LOG"
[ -n "$STUB_FAIL_V4L2" ] && exit 1
out=
for a in "$@"; do case "$a" in --stream-to=*) out="${a#--stream-to=}";; esac; done
if [ -n "$out" ]; then
    if [ -n "$STUB_SHORT" ]; then head -c "$STUB_SHORT" "$STUB_FRAME" > "$out"; exit 0; fi
    cp "$STUB_FRAME" "$out"
fi
exit 0
"""
UNAME_STUB = """#!/bin/sh
echo "$STUB_UNAME"
"""
PNG_STUB = """#!/usr/bin/env python3
import os, pathlib, sys
with open(os.environ['STUB_LOG'], 'a') as log:
    log.write('raw_png ' + ' '.join(sys.argv[1:]) + '\\n')
if os.environ.get('STUB_FAIL_PNG'):
    sys.exit(1)
base = pathlib.Path(sys.argv[sys.argv.index('-o') + 1]).with_suffix('')
pathlib.Path(str(base) + '_high.png').write_bytes(bytes.fromhex('89504e470d0a1a0a') + b'png')
pathlib.Path(str(base) + '_low.png').write_bytes(bytes.fromhex('89504e470d0a1a0a') + b'png')
"""


def make_frame(path):
    pattern = b"".join((i % 1024).to_bytes(2, "little") for i in range(1024))      # 10-bit values
    reps = FRAME_BYTES // len(pattern)
    with open(path, "wb") as fh:
        for _ in range(reps):
            fh.write(pattern)
        fh.write(pattern[:FRAME_BYTES - reps * len(pattern)])


@unittest.skipUnless(SCRIPT.exists(), "capture_ar2020.sh not found in the dalobcv110 overlay")
class MipiBase(unittest.TestCase):
    frame = None

    @classmethod
    def setUpClass(cls):
        if not FSW.exists():
            raise unittest.SkipTest(f"{FSW} missing")
        cls.shared = pathlib.Path(tempfile.mkdtemp(prefix="mipi-frame-"))
        cls.frame = cls.shared / "frame.raw"
        make_frame(cls.frame)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.shared, ignore_errors=True)

    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="mipi-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.leds = self.tmp / "leds"
        for name in LEDS:
            (self.leds / name).mkdir(parents=True)
            (self.leds / name / "brightness").write_text("0\n")
        self.lane, self.sensor = self.tmp / "lane", self.tmp / "sensor"
        self.lane.write_text("0\n")
        self.sensor.write_text("0\n")
        self.data = self.tmp / "images"
        self.stubs = self.tmp / "stubs"
        self.stubs.mkdir()
        for name, body in (("media-ctl", MEDIA_STUB), ("v4l2-ctl", V4L2_STUB),
                           ("uname", UNAME_STUB), ("raw_png", PNG_STUB)):
            (self.stubs / name).write_text(body)
            (self.stubs / name).chmod(0o755)
        self.log = self.tmp / "calls.log"
        self.log.write_text("")
        self.env = dict(os.environ)
        self.env.update({
            "PATH": f"{self.stubs}:{os.environ['PATH']}",
            "DAL_GUI_LED_ROOT": str(self.leds),
            "DAL_GUI_MIPI_LANE": str(self.lane),
            "DAL_GUI_MIPI_SENSOR": str(self.sensor),
            "DAL_GUI_MIPI_DATA_DIR": str(self.data),
            "DAL_GUI_MIPI_LOCK": str(self.tmp / "hw.lock"),
            "DAL_GUI_MIPI_CAPTURE": str(SCRIPT),
            "DAL_GUI_MIPI_PNG": str(self.stubs / "raw_png"),
            "DAL_GUI_CAMERA_LED_PWM": str(self.tmp / "pwm" / "pwm3"),
            "STUB_LOG": str(self.log),
            "STUB_FRAME": str(self.frame),
            "STUB_UNAME": "6.1.0-linux4microchip+fpga",
        })

    # -- helpers ---------------------------------------------------------------------------
    def hw(self, *args, timeout=60):
        return subprocess.run([str(FSW), "--gui-hw", *args], env=self.env, capture_output=True,
                              text=True, timeout=timeout)

    def led(self, name):
        return int((self.leds / name / "brightness").read_text())

    def calls(self, tool=None):
        lines = self.log.read_text().splitlines()
        return [l for l in lines if tool is None or l.startswith(tool + " ")]

    def script(self, *args, **env):
        e = dict(self.env)
        e.update(env)
        return subprocess.run(["bash", str(SCRIPT), *args], env=e, capture_output=True, text=True, timeout=60)


class CaptureScript(MipiBase):
    def test_mip_01_no_argument_prints_usage(self):
        r = self.script()
        self.assertEqual(r.returncode, 1)
        self.assertIn("Usage:", r.stdout)
        self.assertEqual(self.calls(), [])

    def test_mip_02_media_ctl_command_lines(self):
        out = self.tmp / "o.raw"
        self.assertEqual(self.script(str(out)).returncode, 0)
        fmt = "[fmt:SRGGB10_1X10/5120x3840]"
        self.assertEqual(self.calls("media-ctl"), [
            f'media-ctl -d /dev/media0 --set-v4l2 "ar2020 1-0036":0{fmt}',
            f'media-ctl -d /dev/media0 --set-v4l2 "dw-csi.0":0{fmt}',
            f'media-ctl -d /dev/media0 --set-v4l2 "csi2dc":0{fmt}',
            f'media-ctl -d /dev/media0 --set-v4l2 "microchip_isc_scaler":0{fmt}'])

    def test_mip_03_scaler_follows_the_kernel_release(self):
        want = {"5.15.0": "atmel_isc_scaler", "5.15.100-linux4microchip": "atmel_isc_scaler",
                "6.1.0": "atmel_isc_scaler", "6.1.55": "atmel_isc_scaler",
                "6.1.0-linux4microchip+fpga": "microchip_isc_scaler", "6.1.55-linux4microchip": "microchip_isc_scaler",
                "6.2.0": "microchip_isc_scaler", "6.6.12": "microchip_isc_scaler", "6.10.1": "microchip_isc_scaler"}
        for release, scaler in want.items():
            with self.subTest(release):
                self.log.write_text("")
                self.script(str(self.tmp / "o.raw"), STUB_UNAME=release)
                self.assertIn(f'"{scaler}":0', self.calls("media-ctl")[3])

    def test_mip_04_v4l2_command_lines(self):
        out = self.tmp / "o.raw"
        self.script(str(out))
        self.assertEqual(self.calls("v4l2-ctl"), [
            "v4l2-ctl --device=/dev/video0 --set-fmt-video=width=5120,height=3840,pixelformat=BG10",
            f"v4l2-ctl --device=/dev/video0 --stream-mmap --stream-count=1 --stream-to={out} --verbose"])

    def test_mip_05_output_file(self):
        out = self.tmp / "o.raw"
        self.script(str(out))
        data = out.read_bytes()
        self.assertEqual(len(data), FRAME_BYTES)
        values = [int.from_bytes(data[i:i + 2], "little") for i in range(0, 4096, 2)]
        self.assertLessEqual(max(values), 1023)
        self.assertGreater(len(set(values)), 100)

    def test_mip_13a_script_always_exits_zero_even_when_both_tools_fail(self):
        """Characterisation of finding D13: no `set -e`, last line is an echo."""
        out = self.tmp / "o.raw"
        r = self.script(str(out), STUB_FAIL_MEDIA="1", STUB_FAIL_V4L2="1")
        self.assertEqual(r.returncode, 0)
        self.assertFalse(out.exists())
        self.assertIn("Capture sequence complete", r.stdout)


class HardwarePath(MipiBase):
    def test_mip_10_power_on_off(self):
        r = self.hw("mipi-power", "1", "on")
        self.assertEqual((r.returncode, r.stdout.strip()), (0, "MIPI camera 1 power ON"))
        self.assertEqual((self.led("pow_mipicam_1"), self.led("pow_mipicam_2")), (1, 0))
        (self.leds / "en_i2c_mipicam_1/brightness").write_text("1\n")
        r = self.hw("mipi-power", "1", "off")
        self.assertEqual(r.stdout.strip(), "MIPI camera 1 power OFF")
        self.assertEqual((self.led("pow_mipicam_1"), self.led("en_i2c_mipicam_1")), (0, 0))
        self.assertEqual(self.hw("mipi-power", "2", "on").returncode, 0)
        self.assertEqual(self.led("pow_mipicam_2"), 1)

    def test_mip_11_route_every_board_and_sensor(self):
        for board in (1, 2):
            for sensor in range(4):
                with self.subTest(board=board, sensor=sensor):
                    r = self.hw("mipi-route", str(board), str(sensor))
                    self.assertEqual((r.returncode, r.stdout.strip()), (0, f"MIPI camera {board} sensor {sensor} selected"), r.stderr)
                    self.assertEqual(self.led(f"pow_mipicam_{board}"), 1)
                    self.assertEqual(self.led(f"en_i2c_mipicam_{board}"), 1)
                    self.assertEqual(self.led(f"en_i2c_mipicam_{3 - board}"), 0)
                    self.assertEqual(self.led("sel_mipicam_on2_off1"), 1 if board == 2 else 0)
                    self.assertEqual((self.lane.read_text().strip(), self.sensor.read_text().strip()),
                                     (str(sensor), str(sensor)))
                    self.assertEqual(self.calls(), [])                  # route alone never touches the camera tools

    def test_mip_12_capture_success_and_file_naming(self):
        r = self.hw("mipi-capture", "2", "3")
        first = self.data / "MIPI1.3_H_000001.raw"
        png = first.with_suffix(".png")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.strip().splitlines()[-1], f"Saved {png} ({png.stat().st_size} bytes)")
        self.assertEqual(first.stat().st_size, FRAME_BYTES)
        self.assertTrue(png.read_bytes().startswith(bytes.fromhex("89504e470d0a1a0a")))
        self.assertEqual(list(self.data.glob("*_low.png")), [])
        self.hw("mipi-capture", "2", "3")
        self.assertTrue((self.data / "MIPI1.3_H_000002.raw").exists())      # the sequence number advances
        self.assertTrue((self.data / "MIPI1.3_H_000002.png").exists())
        self.hw("mipi-capture", "1", "0")
        self.assertTrue((self.data / "MIPI0.0_H_000001.raw").exists())      # board 1 is named MIPI0 (board - 1)

    def test_mip_13b_failures(self):
        self.env["STUB_FAIL_V4L2"] = "1"
        r = self.hw("mipi-capture", "1", "0")
        self.assertEqual(r.returncode, 1)
        self.assertIn("MIPI capture failed or incomplete RAW frame", r.stderr)
        self.assertEqual(list(self.data.glob("*.raw")), [])
        self.env.pop("STUB_FAIL_V4L2")

    def test_mip_13c_short_frame_is_rejected_before_png_conversion(self):
        self.env["STUB_SHORT"] = "4096"
        r = self.hw("mipi-capture", "1", "1")
        self.assertEqual(r.returncode, 1)
        self.assertIn("incomplete RAW frame", r.stderr)
        self.assertEqual(list(self.data.glob("*")), [])
        self.assertEqual(self.calls("raw_png"), [])

    def test_mip_13d_png_failure_retains_raw(self):
        self.env["STUB_FAIL_PNG"] = "1"
        r = self.hw("mipi-capture", "1", "1")
        self.assertEqual(r.returncode, 1)
        self.assertIn("RAW retained", r.stderr)
        self.assertEqual((self.data / "MIPI0.1_H_000001.raw").stat().st_size, FRAME_BYTES)
        self.assertEqual(list(self.data.glob("*.png")), [])

    @unittest.skipIf(os.geteuid() == 0, "root ignores file permissions")
    def test_mip_14_route_readback_mismatch_stops_before_capture(self):
        self.sensor.write_text("9\n")
        self.sensor.chmod(0o444)
        r = self.hw("mipi-capture", "1", "2")
        self.assertEqual(r.returncode, 1)
        self.assertIn("MIPI route readback mismatch", r.stderr)
        self.assertEqual(self.calls(), [])

    def test_mip_15_argument_range(self):
        bad = [("mipi-route", "0", "0"), ("mipi-route", "3", "0"), ("mipi-route", "1", "4"),
               ("mipi-route", "x", "0"), ("mipi-route", "1", "-1"), ("mipi-capture", "1"),
               ("mipi-power", "0", "on"), ("mipi-power", "3", "on"), ("mipi-power", "1", "dim"),
               ("camera-led", "101"), ("camera-led", "x"), ("camera-led",), ("nonsense",)]
        for args in bad:
            with self.subTest(args):
                r = self.hw(*args)
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
                self.assertIn("Usage:", r.stderr)
        self.assertEqual([self.led(n) for n in LEDS], [0] * len(LEDS))

    def test_mip_16_hardware_lock(self):
        with open(self.tmp / "hw.lock", "w") as held:
            fcntl.flock(held, fcntl.LOCK_EX)
            for args in (("mipi-power", "1", "on"), ("mipi-route", "1", "0")):
                r = self.hw(*args)
                self.assertEqual(r.returncode, 1)
                self.assertIn("MIPI hardware busy", r.stderr)
            self.assertEqual(self.led("pow_mipicam_1"), 0)
        self.assertEqual(self.hw("mipi-power", "1", "on").returncode, 0)    # released with the holder

    def test_mip_17_capture_script_override(self):
        probe = self.tmp / "probe.sh"
        probe.write_text('echo "args=$#" > "$(dirname "$1")/probe.out"; echo "$1" >> "$(dirname "$1")/probe.out"; '
                         'cp "$STUB_FRAME" "$1"\n')
        self.env["DAL_GUI_MIPI_CAPTURE"] = str(probe)
        r = self.hw("mipi-capture", "1", "0")
        self.assertEqual(r.returncode, 0, r.stderr)
        want = self.data / "MIPI0.0_H_000001.raw"
        self.assertEqual((self.data / "probe.out").read_text().splitlines(), ["args=1", str(want)])
        self.assertEqual(self.calls("media-ctl") + self.calls("v4l2-ctl"), [])
        self.assertEqual(len(self.calls("raw_png")), 1)

    def test_mip_18_switching_boards_leaves_the_first_camera_powered(self):
        """Characterisation (finding D20): a route to board 2 turns board 1's I2C switch off but
        leaves its power switch on; only `mipi-power N off` removes power."""
        self.assertEqual(self.hw("mipi-route", "1", "0").returncode, 0)
        self.assertEqual(self.hw("mipi-route", "2", "1").returncode, 0)
        self.assertEqual((self.led("en_i2c_mipicam_1"), self.led("en_i2c_mipicam_2")), (0, 1))
        self.assertEqual((self.led("pow_mipicam_1"), self.led("pow_mipicam_2")), (1, 1))
        self.assertEqual(self.led("sel_mipicam_on2_off1"), 1)
        self.hw("mipi-power", "1", "off")
        self.assertEqual(self.led("pow_mipicam_1"), 0)

    def test_mip_19_camera_led(self):
        pwm = self.tmp / "pwm"
        pwm.mkdir()
        (pwm / "export").write_text("")
        (pwm / "pwm3").mkdir()
        for name, text in (("period", "1000000\n"), ("duty_cycle", "0\n"), ("enable", "0\n")):
            (pwm / "pwm3" / name).write_text(text)
        for duty, want in ((0, (0, 0)), (50, (500000, 1)), (100, (1000000, 1)), (1, (10000, 1))):
            with self.subTest(duty):
                for name in ("duty_cycle", "enable"):          # sysfs truncates on write; plain files do not
                    (pwm / "pwm3" / name).write_text("0\n")
                r = self.hw("camera-led", str(duty))
                self.assertEqual((r.returncode, r.stdout.strip()), (0, f"CAMERA_LED duty set to {duty}%"))
                self.assertEqual((int((pwm / "pwm3/duty_cycle").read_text()), int((pwm / "pwm3/enable").read_text())), want)
                self.assertEqual(int((pwm / "pwm3/period").read_text()), 1000000)

    def test_mip_19b_missing_pwm_channel_is_exported(self):
        pwm = self.tmp / "pwm"
        pwm.mkdir()
        (pwm / "export").write_text("")
        r = self.hw("camera-led", "10")                       # pwm3 directory does not exist yet: export, then fail
        self.assertEqual(r.returncode, 1)
        self.assertEqual((pwm / "export").read_text(), "3")


class ThroughTheConsole(MipiBase):
    def start(self, capture=None):
        env = {k: v for k, v in self.env.items() if k.startswith(("DAL_GUI", "STUB")) or k == "PATH"}
        if capture:
            env["DAL_GUI_MIPI_CAPTURE"] = str(capture)
        fsw = Fsw(extra_env=env).start()
        self.addCleanup(fsw.stop)
        return fsw

    def test_mip_20_gui_shell_returns_the_result_as_json(self):
        fsw = self.start()
        r = json.loads(fsw.console.run("--json", "gui_shell", "90", f"{FSW} --gui-hw mipi-capture 1 2", timeout=100))
        self.assertTrue(r["ok"], r)
        self.assertEqual((r["exit"], r["timeout"]), (0, False))
        self.assertIn("Saved", r["text"])
        self.assertEqual((self.data / "MIPI0.2_H_000001.raw").stat().st_size, FRAME_BYTES)
        self.assertTrue((self.data / "MIPI0.2_H_000001.png").exists())

    def test_mip_20b_gui_shell_timeout_kills_a_stalled_capture(self):
        slow = self.tmp / "slow.sh"
        slow.write_text('sleep 30\n')
        fsw = self.start(slow)
        started = time.monotonic()
        r = json.loads(fsw.console.run("--json", "gui_shell", "3", f"{FSW} --gui-hw mipi-capture 1 0", timeout=30))
        self.assertTrue(r["timeout"], r)
        self.assertFalse(r["ok"])
        self.assertLess(time.monotonic() - started, 12)
        self.assertEqual(self.hw("mipi-power", "1", "on").returncode, 0)    # the lock died with the process

    def test_mip_21_the_daemon_stays_responsive_during_a_capture(self):
        slow = self.tmp / "slow.sh"
        slow.write_text('sleep 4; cp "$STUB_FRAME" "$1"\n')
        fsw = self.start(slow)
        result = {}
        worker = threading.Thread(target=lambda: result.update(
            r=json.loads(fsw.console.run("--json", "gui_shell", "60", f"{FSW} --gui-hw mipi-capture 2 1", timeout=70))))
        worker.start()
        time.sleep(1.5)                                       # the route is done, the stalled script is running
        self.assertTrue(worker.is_alive())
        pings = [fsw.link.txrx(0, 0).ms for _ in range(5)]
        self.assertLess(max(pings), 500, pings)
        self.assertIn("gui_state", fsw.console.run("--json", "gui_state") + "gui_state")
        worker.join(30)
        self.assertTrue(result["r"]["ok"], result)
