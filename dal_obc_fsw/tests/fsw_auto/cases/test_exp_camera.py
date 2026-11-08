"""EXP thermal camera commands through the ICD 02/0C gateway."""
import struct

from fsw_auto.fake_exp import ExpModel
from fsw_auto.fixtures import FswTestCase


class ExpCamera(FswTestCase):
    exp_responder = ExpModel()

    def command(self, text):
        payload = self.link.txrx(2, 0x0C, text.encode()).expect_ack(2, 0x0C)
        status, size = struct.unpack_from("<BH", payload)
        self.assertEqual(status, 0)
        return payload[3:3 + size].decode(errors="replace")

    def test_exp_camera_config_capture_and_fresh_point(self):
        self.assertIn("OK", self.command("camera_config 0 mlx90642 102 float32 8 1000"))
        self.assertIn("ENABLED", self.command("camera_buffer_enable 0"))
        self.assertIn("QUEUED", self.command("camera_init 0"))
        self.assertIn("QUEUED", self.command("camera_point 0 0 0 2"))
        status = self.command("camera_status 0")
        self.assertIn("object=0", status)
        self.assertIn("CAMERA_POINT", status)
        self.assertIn("center_cC=2400", status)
        self.assertIn("max_cC=2412", status)
        self.assertIn("mean_cC=2406", status)
        self.assertIn("QUEUED", self.command("camera_capture 0"))
        self.assertIn("object=1", self.command("camera_status 0"))

    def test_exp_camera_program_address_reports_restart(self):
        self.command("camera_buffer_enable 1")
        self.assertIn("result=QUEUED", self.command("camera_address_set 1 52"))
        self.assertIn("restart_required=1", self.command("camera_status 1"))
