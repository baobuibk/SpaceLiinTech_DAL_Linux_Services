import importlib.machinery
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT))
import boot_host

loader = importlib.machinery.SourceFileLoader("avr_xbld", str(PROJECT / "avr_xbld"))
spec = importlib.util.spec_from_loader(loader.name, loader)
client = importlib.util.module_from_spec(spec)
loader.exec_module(client)


class FakeLink:
    def __init__(self):
        self.writes = []

    def write(self, address, data):
        self.writes.append((address, data))


class FakePort:
    def __init__(self):
        self.request = b""
        self.writes = []

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.request = data
        self.writes.append(data)

    def flush(self):
        pass

    def read(self, _size):
        data, self.request = self.request, b""
        return data


class AvrClientTests(unittest.TestCase):
    def test_modbus_reset_uses_holding_40060_key(self):
        frame = client.modbus_reset_frame(1)
        self.assertEqual(frame, bytes.fromhex("01 06 00 3B B0 07 CC 05"))

    def test_text_bootloader_command_precedes_binary_hold(self):
        port = FakePort()
        link = mock.Mock()
        with mock.patch.object(client, "boot_link", return_value=link), \
             mock.patch.object(client, "geometry", return_value={"hw_id": 5}), \
             mock.patch.object(client.time, "sleep"):
            self.assertIs(client.request_bootloader(port), link)
        self.assertEqual(port.writes, [b"bootloader confirm\r"])
        link.command.assert_called_once_with(0x48, "bhold")

    def test_modbus_fallback_waits_for_ack_before_binary_hold(self):
        port = FakePort()
        link = mock.Mock()
        with mock.patch.object(client, "boot_link", return_value=link), \
             mock.patch.object(client, "geometry", return_value={"hw_id": 5}), \
             mock.patch.object(client.time, "sleep"):
            self.assertIs(client.request_bootloader_modbus(port, 1), link)
        self.assertEqual(port.writes, [bytes.fromhex("01 06 00 3B B0 07 CC 05")])
        link.command.assert_called_once_with(0x48, "bhold")

    def test_packaged_image_is_accepted_and_raw_image_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "raw.hex"
            packaged = Path(directory) / "packaged.hex"
            boot_host.write_hex(raw, {0: 0x0C, 1: 0x94, 2: 0x00, 3: 0x00})
            boot_host.package(raw, packaged)
            image, header = client.checked_image(packaged)
            self.assertEqual(image, bytes.fromhex("0C 94 00 00"))
            self.assertEqual(len(header), 256)
            resolved, _, _ = client.resolve_image("packaged.hex", [], Path(directory))
            self.assertEqual(resolved, packaged)
            with self.assertRaises(ValueError):
                client.checked_image(raw)

    def test_default_firmware_directory_is_pdu(self):
        self.assertEqual(client.DEFAULT_FIRMWARE_DIR, Path("/home/bee/firmware/PDU"))

    def test_writes_never_cross_avr_flash_page(self):
        link = FakeLink()
        image = bytes(range(256)) + bytes(range(44))
        header = bytes(256)
        client.write_image(link, image, header)
        self.assertEqual(link.writes[0][0], 0)
        self.assertEqual(len(link.writes[0][1]), 251)
        self.assertEqual(link.writes[1][0], 251)
        self.assertEqual(len(link.writes[1][1]), 5)
        self.assertEqual(link.writes[-1][0] + len(link.writes[-1][1]), 0x1E000)
        for address, data in link.writes:
            self.assertLessEqual(len(data), 251)
            self.assertLessEqual((address & 255) + len(data), 256)


if __name__ == "__main__":
    unittest.main()
