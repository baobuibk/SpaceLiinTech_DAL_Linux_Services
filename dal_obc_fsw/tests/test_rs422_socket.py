"""Exercise the optional binary RS-422 Unix socket beside the serial link."""

import socket
import stat
import threading
import time

from fsw_auto import bdal
from fsw_auto.fixtures import Fsw


def receive_frame(peer):
    decoder = bdal.FrameDecoder()
    while True:
        frames = decoder.feed(peer.recv(4096))
        if frames:
            return frames[0]


def main():
    fsw = Fsw()
    path = fsw.tmp / "rs422.sock"
    fsw.extra_env = {"DAL_OBC_RS422_SOCKET": str(path)}
    try:
        fsw.start()
        deadline = time.monotonic() + 3
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(0.02)
        assert path.exists(), fsw.log_tail(25)
        assert stat.S_IMODE(path.stat().st_mode) == 0o600

        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as peer:
            peer.settimeout(3)
            peer.connect(str(path))
            request = bdal.encode_tc(0, 0)
            peer.sendall(request[:3])
            time.sleep(0.05)
            peer.sendall(request[3:])
            ack, addr, payload, _ = receive_frame(peer)
            assert (ack, addr, len(payload)) == (0, 0, 4)

            peer.sendall(bdal.encode_tc(1, 1, b"bad"))
            ack, code, payload, _ = receive_frame(peer)
            assert (ack, code, payload) == (0xFF, 0, b"")

            peer.sendall(bdal.encode_tc(0, 0, crc=0))
            peer.settimeout(0.2)
            try:
                peer.recv(1)
                raise AssertionError("bad CRC generated a reply")
            except socket.timeout:
                pass
            peer.settimeout(3)

            failures = []
            def serial_pings():
                try:
                    for _ in range(15):
                        fsw.link.txrx(0, 0).expect_ack(0, 0, 4)
                except Exception as exc:
                    failures.append(exc)
            serial = threading.Thread(target=serial_pings)
            serial.start()
            for _ in range(15):
                peer.sendall(request)
                ack, addr, payload, _ = receive_frame(peer)
                assert (ack, addr, len(payload)) == (0, 0, 4)
            serial.join(5)
            assert not serial.is_alive() and not failures, failures
        fsw.link.txrx(0, 0).expect_ack(0, 0, 4)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as second:
            second.settimeout(3)
            second.connect(str(path))
            second.sendall(bdal.encode_tc(0, 0))
            ack, addr, payload, _ = receive_frame(second)
            assert (ack, addr, len(payload)) == (0, 0, 4)
        print("RS-422 Unix socket framing, routing, and concurrent serial: OK")
    finally:
        fsw.stop()


if __name__ == "__main__":
    main()
