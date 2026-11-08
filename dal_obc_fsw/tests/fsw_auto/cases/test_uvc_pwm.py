"""UVC profile PWM is applied during capture through both command transports."""
import json
import pathlib
import time
import unittest

from fsw_auto.fixtures import Fsw


class UvcPwm(unittest.TestCase):
    def test_profile_pwm_lights_capture_then_turns_off(self):
        fsw = Fsw()
        profile = fsw.tmp / "profiles"
        profile.mkdir()
        pwm = fsw.tmp / "pwm3"
        pwm.mkdir()
        for name, value in (("period", "1000000"), ("duty_cycle", "0"), ("enable", "0")):
            (pwm / name).write_text(value)
        observed = fsw.tmp / "observed"
        tool = fsw.tmp / "uvc_tool"
        tool.write_text(
            "#!/usr/bin/env python3\n"
            "import os, pathlib, sys\n"
            "camera = int(sys.argv[1])\n"
            "root = pathlib.Path(os.environ['DAL_UVC_PROFILE_DIR'])\n"
            "path = root / f'camera{camera}.bin'\n"
            "if sys.argv[2] == '--uvc-set':\n"
            "    data = bytes.fromhex(sys.argv[3])\n"
            "    path.write_bytes(data)\n"
            "    print(data.hex())\n"
            "elif sys.argv[2] == '--uvc-get':\n"
            "    print(path.read_bytes().hex() if path.exists() else (bytes([camera, 1]) + bytes(30)).hex())\n"
            "else:\n"
            "    pwm = pathlib.Path(os.environ['DAL_GUI_CAMERA_LED_PWM'])\n"
            "    pathlib.Path(os.environ['DAL_TEST_PWM_OBSERVED']).write_text(\n"
            "        pwm.joinpath('enable').read_text().strip() + ',' +\n"
            "        pwm.joinpath('duty_cycle').read_text().strip())\n"
            "    if root.joinpath('fail').exists(): sys.exit(2)\n"
            "    pathlib.Path(sys.argv[3]).write_bytes(b'\\xff\\xd8test\\xff\\xd9')\n"
        )
        tool.chmod(0o755)
        fsw.extra_env.update({"DAL_UVC_PROFILE_DIR": str(profile),
                              "DAL_GUI_CAMERA_LED_PWM": str(pwm),
                              "DAL_TEST_PWM_OBSERVED": str(observed),
                              "DAL_USB_CAM_TOOL": str(tool)})
        with fsw:
            reply = json.loads(fsw.console.run("--json", "uvc_cfg", "2", "set", "led_pwm", "40"))
            self.assertTrue(reply["ok"], reply)
            self.assertEqual(bytes.fromhex(reply["payload_hex"])[31], 40)
            self.assertEqual((profile / "camera2.bin").read_bytes()[31], 40)
            request = bytes([2, 1, 0, 0, 42, 0, 0, 0])
            fsw.link.txrx(1, 0x11, request).expect_ack(1, 0x11, 4)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                status = fsw.link.txrx(1, 0x31, bytes([2, 42, 0, 0, 0])).expect_ack(1, 0x31, 8)
                if status[1] in (3, 4):
                    break
                time.sleep(0.05)
            self.assertEqual(status[1], 3, fsw.log_tail())
            self.assertEqual(observed.read_text(), "1,400000")
            self.assertEqual((pwm / "enable").read_text(), "0")
            self.assertTrue((fsw.tmp / "uvc/USB1_00000042.jpg").exists())
            (profile / "fail").touch()
            fsw.link.txrx(1, 0x11, bytes([2, 1, 0, 0, 43, 0, 0, 0])).expect_ack(1, 0x11, 4)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                status = fsw.link.txrx(1, 0x31, bytes([2, 43, 0, 0, 0])).expect_ack(1, 0x31, 8)
                if status[1] == 4:
                    break
                time.sleep(0.05)
            self.assertEqual(status[1], 4, fsw.log_tail())
            self.assertEqual((pwm / "enable").read_text(), "0")
