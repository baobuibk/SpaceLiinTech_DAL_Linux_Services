"""Forward one com0com endpoint to Renode's raw UART TCP terminal."""
import argparse
import select
import socket
import sys

import serial


def forward_once(port, connection, wait=0.01):
    """Copy available bytes unchanged; fail on disconnect or incomplete writes."""
    uart = port.read(min(port.in_waiting, 4096))
    if uart:
        connection.sendall(uart)
    received = b""
    if select.select([connection], [], [], wait)[0]:
        received = connection.recv(4096)
        if not received:
            raise ConnectionError("Renode closed the UART TCP connection")
        offset = 0
        while offset < len(received):
            count = port.write(received[offset:])
            if not count:
                raise ConnectionError(f"COM write stalled after {offset}/{len(received)} bytes; check the GUI endpoint")
            offset += count
    return len(uart), len(received)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="Peer of the GUI COM port (e.g. COM6 for GUI COM3)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19002)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    try:
        # Disable handshake: this transports raw bytes through a virtual null modem.
        with socket.create_connection((args.host, args.port), timeout=3) as connection:
            connection.settimeout(1)
            with serial.Serial(args.serial, args.baud, timeout=0, write_timeout=1,
                               xonxoff=False, rtscts=False, dsrdtr=False) as port:
                print(f"Bridge ready: {args.serial} <-> {args.host}:{args.port}. Open the paired COM port in the GUI. Ctrl+C stops.", flush=True)
                while True:
                    tx, rx = forward_once(port, connection)
                    if tx or rx:
                        print(f"GUI -> Renode: {tx} bytes; Renode -> GUI: {rx} bytes", flush=True)
    except KeyboardInterrupt:
        print("Bridge stopped.")
        return 0
    except (OSError, serial.SerialException) as error:
        print(f"Bridge error: {error}. Check Renode TCP terminal and the com0com pair. No automatic reconnect or replay.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
