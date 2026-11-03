#!/usr/bin/env python3
"""Exercise the host TUI in a pseudo-terminal without board utilities."""
import errno
import fcntl
import os
import pathlib
import pty
import re
import struct
import subprocess
import sys
import termios
import threading
import time


program = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "../target/release/obc_gui")
master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
environment = os.environ.copy()
environment.update({
    "TERM": "xterm-256color",
    "OBC_GUI_XIO": "/no-such-board/xio",
    "OBC_GUI_USB_CAM": "/no-such-board/usb_cam",
})
process = subprocess.Popen(
    [str(program), "host-sim"], stdin=slave, stdout=slave, stderr=slave,
    env=environment,
)
os.close(slave)
output = bytearray()


def collect():
    while True:
        try:
            data = os.read(master, 65536)
        except OSError as error:
            if error.errno == errno.EIO:
                return
            raise
        if not data:
            return
        output.extend(data)


reader = threading.Thread(target=collect, daemon=True)
reader.start()
try:
    time.sleep(0.4)
    for keys in [b"4", b"\r", b"jj", b"\r", b"f", b"2", b"\r", b"2026-10-25 12:00:00", b"\r",
                 b"3", b"j", b"\r", b"5", b"j", b"\r", b"jjjj", b"\r", b"jjjjjj", b"\r",
                 b"6", b"\r", b"7", b"jjjjjjj", b"\r", b"pc_status", b"\r",
                 b"8", b"\r", b"\r", b"\r", b"\x1b", b"\x1b", b"\x1b[C", b"\x1b[C", b"\x1b[C", b"\x1b[C"]:
        os.write(master, keys)
        time.sleep(0.24)
    time.sleep(0.4)
    os.write(master, b"q")
    exit_code = process.wait(timeout=8)
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
    reader.join(timeout=2)
    os.close(master)

text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", output.decode("utf-8", errors="replace"))
compact = re.sub(r"[^a-z0-9]+", "", text.lower())
expected = [
    "hostsim", "pduonline", "camera1selected",
    "bme688sampleinvalid", "aiefuseoff", "rtcsettoepoch1792929600",
    "mipicamera2sensor3rawcapture",
    "pcstatus",
    "helpsimulatedcommandset", "livestatus", "pac1934", "u11singlepump", "bme688",
]
missing = [item for item in expected if item not in compact]
if exit_code != 0 or missing:
    print(f"host_sim smoke failed: exit={exit_code}, missing={missing}", file=sys.stderr)
    print(repr(text[-2000:]), file=sys.stderr)
    sys.exit(1)
print("host_sim TUI smoke passed: pages, USB/MIPI capture, fault, eFuse, RTC, EXP command")
