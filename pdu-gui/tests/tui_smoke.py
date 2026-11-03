#!/usr/bin/env python3
"""Launch the actual terminal binary without DISPLAY and verify clean restoration."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import sys
import termios
import time

binary = str(Path(sys.argv[1] if len(sys.argv) > 1 else "target/release/dal-pdu-gui").resolve())
pid, master = pty.fork()
if pid == 0:
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
        os.environ.pop(key, None)
    os.environ["TERM"] = "xterm-256color"
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 35, 120, 0, 0))
    os.execv(binary, [binary])

output = bytearray()
reaped = False

def collect_until(marker, timeout=5):
    deadline = time.monotonic() + timeout
    start = len(output)
    while time.monotonic() < deadline:
        if select.select([master], [], [], 0.1)[0]:
            try:
                data = os.read(master, 65536)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    break
                raise
            output.extend(data)
            if marker in output[start:]:
                return
    raise AssertionError(f"Missing terminal text {marker!r}: {output[-2000:]!r}")

try:
    collect_until(b"Terminal Monitor")
    assert b"DISCONNECTED" in output
    os.write(master, b"s")
    collect_until(b"Connection settings")
    os.write(master, b"\x1b")
    time.sleep(0.2)
    os.write(master, b"\t")
    collect_until(b"RS485 baud configuration")
    os.write(master, b"\t\t\t\t")
    collect_until(b"Console controls")
    os.write(master, b"q")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            reaped = True
            assert os.waitstatus_to_exitcode(status) == 0
            break
        time.sleep(0.05)
    assert reaped, "Application did not exit"
    flags = termios.tcgetattr(master)[3]
    assert flags & termios.ICANON and flags & termios.ECHO, "Terminal was not restored"
    assert b"WinitEventLoop" not in output
    print("PASS: no-display terminal startup, settings, console tab, exit and terminal restoration")
finally:
    if not reaped:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
    os.close(master)
