#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select a DAL USB camera and capture one MJPEG frame."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import struct

LEDS = Path("/sys/class/leds")
USB = Path("/sys/bus/usb/devices")
VIDEO = Path("/sys/class/video4linux")
DEFAULT_CONFIG = None  # Replaced with usb-cam.json by the Yocto build.
PROFILE_DIR = Path(os.environ.get("DAL_UVC_PROFILE_DIR", "/var/lib/dal/uvc"))
UVC_FIELDS = ((0, 4, 8), (1, 8, 2), (2, 10, 2), (3, 12, 4),
              (4, 16, 1), (5, 17, 4), (6, 21, 2), (7, 23, 1),
              (8, 24, 2), (9, 26, 1), (10, 27, 2), (11, 29, 2),
              (12, 31, 1))  # OBC CAMERA_LED duty, not a V4L2 control
UVC_MASK = (1 << len(UVC_FIELDS)) - 1


def uvc_decode(blob):
    if len(blob) != 32 or blob[0] not in range(1, 5) or blob[1] != 1:
        raise ValueError("invalid UVC configuration header")
    mask = int.from_bytes(blob[2:4], "little")
    if mask & ~UVC_MASK:
        raise ValueError("unknown UVC field mask")
    if blob[31] > 100 or (blob[31] and not mask & (1 << 12)):
        raise ValueError("invalid CAMERA_LED duty")
    width, height = struct.unpack_from("<HH", blob, 8)
    fps_num, fps_den = struct.unpack_from("<HH", blob, 12)
    if mask & 1 and blob[4:8] != b"MJPG":
        raise ValueError("only MJPG capture is supported")
    if (mask & 2 and not width) or (mask & 4 and not height):
        raise ValueError("width and height must be positive")
    if mask & 8 and (not fps_num or not fps_den):
        raise ValueError("frame rate numerator and denominator must be positive")
    if mask & (1 << 4) and blob[16] not in (0, 1):
        raise ValueError("invalid exposure mode")
    if mask & (1 << 7) and blob[23] not in (0, 1):
        raise ValueError("invalid autofocus mode")
    if mask & (1 << 9) and blob[26] not in (0, 1):
        raise ValueError("invalid white balance mode")
    if (mask & (1 << 4) and mask & (1 << 5) and not blob[16]) or (
            mask & (1 << 7) and mask & (1 << 8) and blob[23]) or (
            mask & (1 << 9) and mask & (1 << 10) and blob[26]):
        raise ValueError("manual control conflicts with automatic mode")
    return mask


def uvc_profile(camera):
    path = PROFILE_DIR / f"camera{camera}.bin"
    if not path.exists():
        return None
    blob = path.read_bytes()
    uvc_decode(blob)
    if blob[0] != camera:
        raise ValueError("camera profile ID mismatch")
    return blob


def uvc_save(blob):
    PROFILE_DIR.mkdir(mode=0o700, parents=True, exist_ok=True)
    path = PROFILE_DIR / f"camera{blob[0]}.bin"
    fd, name = tempfile.mkstemp(prefix=".uvc-", dir=PROFILE_DIR)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(blob)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def uvc_merge(old, new):
    if old is None:
        return new
    merged = bytearray(old)
    old_mask = uvc_decode(old)
    new_mask = uvc_decode(new)
    for bit, offset, size in UVC_FIELDS:
        if new_mask & (1 << bit):
            merged[offset:offset + size] = new[offset:offset + size]
    mask = old_mask | new_mask
    if new_mask & (1 << 4) and not new[16]:
        mask &= ~(1 << 5)
    if new_mask & (1 << 7) and new[23]:
        mask &= ~(1 << 8)
    if new_mask & (1 << 9) and new[26]:
        mask &= ~(1 << 10)
    merged[2:4] = mask.to_bytes(2, "little")
    uvc_decode(merged)
    return bytes(merged)


def uvc_apply(device, blob):
    """Apply only selected fields; v4l2-ctl fails on unsupported controls."""
    mask = uvc_decode(blob)
    width, height = struct.unpack_from("<HH", blob, 8)
    num, den = struct.unpack_from("<HH", blob, 12)
    if mask & 7:
        fmt = run("v4l2-ctl", "-d", device, "--get-fmt-video")
        old = re.search(r"Width/Height\s*:\s*(\d+)\s*/\s*(\d+)", fmt)
        if not old:
            raise RuntimeError("cannot read camera dimensions")
        if not mask & 2:
            width = int(old.group(1))
        if not mask & 4:
            height = int(old.group(2))
        fourcc = blob[4:8].decode("ascii") if mask & 1 else "MJPG"
        run("v4l2-ctl", "-d", device,
            f"--set-fmt-video=width={width},height={height},pixelformat={fourcc}")
    if mask & 8:
        run("v4l2-ctl", "-d", device, f"--set-parm={num / den:g}")
    controls = ((4, "exposure_auto", 1 if blob[16] else 0, None),
                (5, "exposure_absolute", int.from_bytes(blob[17:21], "little"), 4),
                (6, "gain", int.from_bytes(blob[21:23], "little"), None),
                (7, "focus_auto", blob[23], None),
                (8, "focus_absolute", int.from_bytes(blob[24:26], "little"), 7),
                (9, "white_balance_automatic", blob[26], None),
                (10, "white_balance_temperature", int.from_bytes(blob[27:29], "little"), 9),
                (11, "brightness", int.from_bytes(blob[29:31], "little", signed=True), None))
    for bit, name, value, auto_bit in controls:
        if mask & (1 << bit):
            if auto_bit is not None and mask & (1 << auto_bit):
                if (bit == 5 and not blob[16]) or (bit == 8 and blob[23]) or (bit == 10 and blob[26]):
                    raise ValueError(f"{name} conflicts with automatic mode")
            run("v4l2-ctl", "-d", device, f"--set-ctrl={name}={value}")
    return mask


def uvc_readback(device, camera, requested):
    """Return a verified profile. Unsupported fields cause the request to fail."""
    blob = bytearray(requested)
    mask = uvc_decode(blob)
    if mask & 7:
        fmt = run("v4l2-ctl", "-d", device, "--get-fmt-video")
        dims = re.search(r"Width/Height\s*:\s*(\d+)\s*/\s*(\d+)", fmt)
        if not dims or "'MJPG'" not in fmt:
            raise RuntimeError("camera did not accept MJPG format")
        struct.pack_into("<HH", blob, 8, int(dims.group(1)), int(dims.group(2)))
    names = {4: "exposure_auto", 5: "exposure_absolute", 6: "gain",
             7: "focus_auto", 8: "focus_absolute", 9: "white_balance_automatic",
             10: "white_balance_temperature", 11: "brightness"}
    offsets = {4: (16, 1), 5: (17, 4), 6: (21, 2), 7: (23, 1),
               8: (24, 2), 9: (26, 1), 10: (27, 2), 11: (29, 2)}
    for bit, name in names.items():
        if not mask & (1 << bit):
            continue
        output = run("v4l2-ctl", "-d", device, f"--get-ctrl={name}")
        match = re.search(r":\s*(-?\d+)", output)
        if not match:
            raise RuntimeError(f"cannot read {name}")
        value = int(match.group(1))
        if bit == 4:
            value = 1 if value == 1 else 0
        offset, size = offsets[bit]
        blob[offset:offset + size] = value.to_bytes(size, "little", signed=bit == 11)
    blob[0] = camera
    return bytes(blob)


def run(*args, timeout=15):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=timeout).stdout


def positive(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return value


def selection(config, camera):
    mapping = config.get("cameras", {}).get(str(camera))
    if (not isinstance(mapping, list) or len(mapping) != 2
            or any(type(bit) is not int or bit not in (0, 1) for bit in mapping)):
        raise ValueError(f"Camera {camera}: configure [SEL0, SEL1] from schematic first")
    route = config.get("usb_path", "")
    if not isinstance(route, str) or not re.fullmatch(r"\d+-\d+(?:\.\d+)*", route):
        raise ValueError("usb_path must be a USB topology path, for example 1-3")
    return mapping, route


def led(name, value):
    (LEDS / name / "brightness").write_text(str(value) + "\n")


def require_switch_enabled():
    # SOM_USB_SW_nOE is active low. Check the selected path is enabled.
    if (LEDS / "en_switch_usb" / "brightness").read_text().strip() != "0":
        raise RuntimeError("USB switch OE is not active (en_switch_usb must be 0)")


def wait_for(probe, timeout, message):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = probe()
        if result:
            return result
        time.sleep(0.2)
    raise RuntimeError(message)


def find_camera(route):
    """Use the selected USB parent and UVC image index, excluding metadata nodes."""
    matches = []
    for entry in VIDEO.glob("video*"):
        try:
            device = (entry / "device").resolve(strict=True)
            if (device / "driver").resolve().name != "uvcvideo":
                continue
            if (entry / "index").read_text().strip() != "0":
                continue
            if any(p.name == route and (p / "idVendor").exists()
                   for p in device.parents):
                matches.append("/dev/" + entry.name)
        except (FileNotFoundError, OSError):
            continue
    if len(matches) > 1:
        raise RuntimeError(f"Multiple UVC image nodes at {route}: {matches}")
    return matches[0] if matches else None


def configure_format(device, width, height):
    run("v4l2-ctl", "-d", device,
        f"--set-fmt-video=width={width},height={height},pixelformat=MJPG")
    actual = run("v4l2-ctl", "-d", device, "--get-fmt-video")
    dimensions = re.search(r"Width/Height\s*:\s*(\d+)\s*/\s*(\d+)", actual)
    if ("'MJPG'" not in actual or not dimensions
            or tuple(map(int, dimensions.groups())) != (width, height)):
        raise RuntimeError("Camera did not accept the requested MJPG size:\n" + actual)


def capture(device, output, width, height, timeout):
    configure_format(device, width, height)
    fd, name = tempfile.mkstemp(prefix=".usb-cam-", dir=output.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        run("v4l2-ctl", "-d", device, "--stream-mmap", "--stream-count=1",
            f"--stream-to={temporary}", timeout=timeout)
        data = temporary.read_bytes()
        if not data.startswith(b"\xff\xd8") or b"\xff\xd9" not in data[2:]:
            raise RuntimeError("Capture did not produce a complete JPEG frame")
        # Publish without overwriting an existing file, even if created during capture.
        os.link(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("camera", type=int, choices=range(1, 5))
    parser.add_argument("--uvc-get", action="store_true", help="return current saved 32-byte configuration as hex")
    parser.add_argument("--uvc-set", metavar="HEX", help="apply and save a 32-byte configuration")
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--width", type=positive, default=1920)
    parser.add_argument("--height", type=positive, default=1080)
    parser.add_argument("--timeout", type=positive, default=15)
    parser.add_argument("--list-formats", action="store_true",
                        help="select/power the camera and list supported formats")
    local_config = Path(__file__).resolve().with_name("usb-cam.json")
    parser.add_argument("--config", type=Path,
                        help="optional JSON override of the bundled camera map")
    args = parser.parse_args()
    if args.uvc_set:
        try:
            requested = bytes.fromhex(args.uvc_set)
        except ValueError as exc:
            raise ValueError("UVC configuration must be hex") from exc
        uvc_decode(requested)
        if requested[0] != args.camera:
            raise ValueError("UVC camera ID mismatch")
    else:
        requested = None
    config = (json.loads(args.config.read_text()) if args.config
              else DEFAULT_CONFIG if DEFAULT_CONFIG is not None
              else json.loads(local_config.read_text()))
    (sel0, sel1), route = selection(config, args.camera)
    if os.geteuid() != 0:
        raise RuntimeError("Run as root")
    if not shutil.which("v4l2-ctl"):
        raise RuntimeError("v4l2-ctl is required")
    output = args.output or Path(
        f"camera{args.camera}-{time.strftime('%Y%m%d-%H%M%S')}.jpg")
    if not args.list_formats:
        if output.exists():
            raise ValueError(f"Output already exists: {output}")
        if not output.parent.is_dir():
            raise ValueError(f"Output directory does not exist: {output.parent}")
    for name in (f"usb_{args.camera}_pwr_en", "en_switch_usb",
                 "sel_usb_sw0", "sel_usb_sw1"):
        if not os.access(LEDS / name / "brightness", os.W_OK):
            raise RuntimeError(f"Missing or unwritable LED control: {name}")
    os.environ["LC_ALL"] = "C"
    with open("/run/usb-cam.lock", "a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("Another usb_cam process is running")
        run("modprobe", "uvcvideo")
        # Break before make, and wait for the old device to disappear.
        led("en_switch_usb", 1)
        wait_for(lambda: not (USB / route).exists(), args.timeout,
                 f"USB {route} did not disconnect; check switch wiring/configuration")
        led("sel_usb_sw0", sel0)
        led("sel_usb_sw1", sel1)
        led(f"usb_{args.camera}_pwr_en", 1)
        time.sleep(0.5)
        led("en_switch_usb", 0)
        require_switch_enabled()
        device = wait_for(lambda: find_camera(route), args.timeout,
                          f"No UVC camera on {route}; check lsusb/dmesg and switch map")
        require_switch_enabled()
        if args.uvc_set:
            previous = uvc_profile(args.camera)
            combined = uvc_merge(previous, requested)
            if previous:
                uvc_apply(device, previous)
            uvc_apply(device, requested)
            verified = uvc_readback(device, args.camera, combined)
            uvc_save(verified)
            print(verified.hex())
        elif args.uvc_get:
            saved = uvc_profile(args.camera)
            if saved is None:
                saved = bytes([args.camera, 1]) + bytes(30)
            if saved[2] or saved[3]:
                uvc_apply(device, saved)
                saved = uvc_readback(device, args.camera, saved)
            print(saved.hex())
        elif args.list_formats:
            print(f"Camera {args.camera}: SEL0={sel0}, SEL1={sel1}, {device}", flush=True)
            print(run("v4l2-ctl", "-d", device, "--list-formats-ext"))
        else:
            print(f"Camera {args.camera}: SEL0={sel0}, SEL1={sel1}, {device}", flush=True)
            saved = uvc_profile(args.camera)
            if saved:
                uvc_apply(device, saved)
                width, height = struct.unpack_from("<HH", saved, 8)
                if int.from_bytes(saved[2:4], "little") & 6 == 6:
                    args.width, args.height = width, height
            capture(device, output, args.width, args.height, args.timeout)
            print(f"Saved {output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"usb_cam: {exc}", file=sys.stderr)
        if isinstance(exc, subprocess.CalledProcessError) and exc.stderr:
            print(exc.stderr.strip(), file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("usb_cam: interrupted", file=sys.stderr)
        sys.exit(130)
