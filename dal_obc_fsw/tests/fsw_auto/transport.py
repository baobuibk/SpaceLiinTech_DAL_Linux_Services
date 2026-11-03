"""Transports: PTY (simulation) and the console client. A serial transport for the
target (tier B) is intentionally not here yet."""
import os
import select
import subprocess
import time

from . import bdal


class LinkTimeout(AssertionError):
    pass


class PtyLink:
    """Test side of a PTY pair whose slave is the daemon's RS-422 port."""

    def __init__(self, master_fd):
        self.fd = master_fd
        self.decoder = bdal.FrameDecoder()
        self.pending = []

    # -- low level ---------------------------------------------------------
    def write(self, data):
        view = memoryview(data)
        while view:
            _, writable, _ = select.select([], [self.fd], [], 10)
            if not writable:
                raise LinkTimeout("RS-422 write stalled")
            view = view[os.write(self.fd, view[:4096]):]

    def drain(self, quiet=0.05):
        """Discard anything pending (call between cases)."""
        self.pending.clear()
        self.decoder.reset()
        while select.select([self.fd], [], [], quiet)[0]:
            os.read(self.fd, 65536)

    def read_tm(self, timeout=3.0, request=None, t0=None):
        """Return the next Tm, or None if nothing arrived within `timeout`."""
        t0 = t0 if t0 is not None else time.perf_counter()
        deadline = time.perf_counter() + timeout
        while not self.pending:
            left = deadline - time.perf_counter()
            if left <= 0:
                return None
            if select.select([self.fd], [], [], left)[0]:
                self.pending.extend(self.decoder.feed(os.read(self.fd, 65536)))
        ack, code, payload, raw = self.pending.pop(0)
        return bdal.Tm(ack, code, payload, (time.perf_counter() - t0) * 1000.0, raw, request)

    # -- convenience -------------------------------------------------------
    def send(self, type_, addr, payload=b""):
        frame = bdal.encode_tc(type_, addr, payload)
        self.write(frame)
        return frame

    def txrx(self, type_, addr, payload=b"", timeout=3.0):
        t0 = time.perf_counter()
        frame = self.send(type_, addr, payload)
        tm = self.read_tm(timeout, request=frame[:64], t0=t0)
        if tm is None:
            raise LinkTimeout(f"no reply to {type_:#04x}/{addr:#04x} within {timeout} s")
        return tm

    def expect_silence(self, seconds, what="frame"):
        tm = self.read_tm(seconds)
        if tm is not None:
            raise bdal.ProtocolError(f"unexpected reply to {what}: {tm!r}\n  {tm.raw[:64].hex(' ')}")


class Console:
    """The local text/JSON socket, through the real dal_flight_console client."""

    def __init__(self, client, socket_path):
        self.client, self.socket = str(client), str(socket_path)

    def run(self, *args, timeout=10):
        return subprocess.run([self.client, "--socket", self.socket, *args], text=True,
                              capture_output=True, timeout=timeout).stdout
