import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "files" / "usb_cam.py"
spec = importlib.util.spec_from_file_location("usb_cam", SOURCE)
cam = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cam)


class CameraTests(unittest.TestCase):
    def test_camera_led_pwm_is_saved_with_profile_and_survives_readback(self):
        old = bytearray(32)
        old[0:2] = bytes((2, 1))
        old[2:4] = (1 << 12).to_bytes(2, "little")
        old[31] = 35
        update = bytearray(32)
        update[0:2] = bytes((2, 1))
        update[2:4] = (1 << 12).to_bytes(2, "little")
        update[31] = 70
        merged = cam.uvc_merge(bytes(old), bytes(update))
        self.assertEqual(merged[31], 70)
        self.assertEqual(cam.uvc_decode(merged), 1 << 12)
        with patch.object(cam, "run", side_effect=AssertionError("PWM is not V4L2")):
            cam.uvc_apply("/dev/video0", merged)
            self.assertEqual(cam.uvc_readback("/dev/video0", 2, merged), merged)
        update[31] = 101
        with self.assertRaisesRegex(ValueError, "CAMERA_LED"):
            cam.uvc_decode(update)

    def test_uvc_profile_merges_fields_and_clears_manual_exposure(self):
        old = bytearray(32)
        old[0:2] = bytes((2, 1))
        old[2:4] = ((1 << 4) | (1 << 5)).to_bytes(2, "little")
        old[16] = 1
        old[17:21] = (200).to_bytes(4, "little")
        new = bytearray(32)
        new[0:2] = bytes((2, 1))
        new[2:4] = (1 << 4).to_bytes(2, "little")
        merged = cam.uvc_merge(bytes(old), bytes(new))
        self.assertEqual(cam.uvc_decode(merged), 1 << 4)
        self.assertEqual(merged[16], 0)

    def test_switch_enable_is_active_low_and_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            leds = Path(directory)
            brightness = leds / "en_switch_usb" / "brightness"
            brightness.parent.mkdir()
            brightness.write_text("1\n")
            with patch.object(cam, "LEDS", leds):
                with self.assertRaisesRegex(RuntimeError, "OE is not active"):
                    cam.require_switch_enabled()
                cam.led("en_switch_usb", 0)
                cam.require_switch_enabled()
                self.assertEqual(brightness.read_text(), "0\n")

    def test_project_builds_standalone_deploy_command(self):
        builder = SOURCE.parents[1] / "build_bundle.py"
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "usb_cam.py").write_text(SOURCE.read_text())
            (work / "usb-cam.json").write_text(
                '{"usb_path":"1-3","cameras":{"1":[0,0],"2":null}}')

            bundled = work / "usb_cam"
            subprocess.run(["python3", str(builder), str(work / "usb_cam.py"),
                            str(work / "usb-cam.json"), str(bundled)], check=True)
            self.assertTrue(bundled.read_text().startswith("#!/bin/sh\n"))
            # No source/config files are present alongside the deployed executable.
            (work / "usb_cam.py").unlink()
            (work / "usb-cam.json").unlink()
            result = subprocess.run(["sh", str(bundled), "--help"],
                                    capture_output=True, text=True, cwd="/")
            self.assertEqual(result.returncode, 0, result.stderr)
            (work / "invalid.json").write_text(
                '{"usb_path":"1-3","cameras":{"2":[0,2]}}')
            result = subprocess.run(["sh", str(bundled), "2", "--config",
                                     str(work / "invalid.json")],
                                    capture_output=True, text=True, cwd="/")
            self.assertEqual(result.returncode, 1)
            self.assertIn("Camera 2: configure", result.stderr)
            self.assertNotIn("No such file", result.stderr)

    def test_camera_map_matches_switch_truth_table(self):
        config = json.loads((SOURCE.parent / "usb-cam.json").read_text())
        for camera, bits in enumerate(((0, 0), (1, 0), (0, 1), (1, 1)), 1):
            with self.subTest(camera=camera):
                self.assertEqual(cam.selection(config, camera),
                                 (list(bits), "1-3"))

    def test_unconfigured_camera_refused(self):
        with self.assertRaisesRegex(ValueError, "configure"):
            cam.selection({"usb_path": "1-3", "cameras": {"2": None}}, 2)

    def test_invalid_switch_bit_refused(self):
        with self.assertRaises(ValueError):
            cam.selection({"usb_path": "1-3", "cameras": {"1": [0, 2]}}, 1)

    def test_find_image_excludes_metadata_and_other_usb_port(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            videos = root / "video4linux"
            videos.mkdir()
            driver = root / "uvcvideo"
            driver.mkdir()
            for node, route, index in (("video2", "1-3", 0),
                                       ("video3", "1-3", 1),
                                       ("video4", "1-2", 0)):
                usb = root / route
                usb.mkdir(exist_ok=True)
                (usb / "idVendor").write_text("05a3")
                interface = usb / (route + ":1.0")
                interface.mkdir(exist_ok=True)
                if not (interface / "driver").exists():
                    (interface / "driver").symlink_to(driver)
                entry = videos / node
                entry.mkdir()
                (entry / "device").symlink_to(interface)
                (entry / "index").write_text(str(index))
            with patch.object(cam, "VIDEO", videos):
                self.assertEqual(cam.find_camera("1-3"), "/dev/video2")

    def test_format_substitution_refused(self):
        with patch.object(cam, "run", side_effect=[
                "", "Width/Height : 640/480\nPixel Format : 'YUYV'"]):
            with self.assertRaisesRegex(RuntimeError, "did not accept"):
                cam.configure_format("/dev/video2", 1920, 1080)

    def test_capture_output_and_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "capture.jpg"

            def stream(*args, **kwargs):
                path = next(x.split("=", 1)[1] for x in args
                            if x.startswith("--stream-to="))
                Path(path).write_bytes(b"\xff\xd8test\xff\xd9")
                return ""

            with patch.object(cam, "configure_format"), patch.object(cam, "run", stream):
                cam.capture("/dev/video2", output, 1920, 1080, 1)
                self.assertEqual(output.read_bytes(), b"\xff\xd8test\xff\xd9")
                with self.assertRaises(FileExistsError):
                    cam.capture("/dev/video2", output, 1920, 1080, 1)
                self.assertEqual(output.read_bytes(), b"\xff\xd8test\xff\xd9")
            with patch.object(cam, "configure_format"), patch.object(
                    cam, "run", side_effect=subprocess.TimeoutExpired("v4l2-ctl", 1)):
                with self.assertRaises(subprocess.TimeoutExpired):
                    cam.capture("/dev/video2", root / "failed.jpg", 1920, 1080, 1)
            self.assertFalse((root / "failed.jpg").exists())
            self.assertEqual(list(root.glob(".usb-cam-*")), [])

    def test_empty_capture_not_published(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "empty.jpg"
            with patch.object(cam, "configure_format"), patch.object(cam, "run"):
                with self.assertRaisesRegex(RuntimeError, "JPEG"):
                    cam.capture("/dev/video2", output, 1920, 1080, 1)
            self.assertFalse(output.exists())
            self.assertEqual(list(Path(directory).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
