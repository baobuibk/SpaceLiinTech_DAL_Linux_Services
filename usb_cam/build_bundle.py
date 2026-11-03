#!/usr/bin/env python3
"""Embed usb_cam.py and the camera map in one deployable shell command."""
import json
from pathlib import Path
import sys


def build(source_path: Path, config_path: Path, output_path: Path) -> None:
    source = source_path.read_text()
    config = json.loads(config_path.read_text())
    marker = "DEFAULT_CONFIG = None  # Replaced with usb-cam.json by the Yocto build."
    if source.count(marker) != 1:
        raise ValueError("usb_cam.py: missing or duplicate configuration marker")
    source = source.replace(marker, "DEFAULT_CONFIG = " + repr(config))
    output_path.write_text(
        "#!/bin/sh\nexec python3 - \"$@\" <<'USB_CAM_EMBEDDED_PYTHON'\n"
        + source + "\nUSB_CAM_EMBEDDED_PYTHON\n"
    )


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: build_bundle.py SOURCE CONFIG OUTPUT")
    build(*(Path(value) for value in sys.argv[1:]))
