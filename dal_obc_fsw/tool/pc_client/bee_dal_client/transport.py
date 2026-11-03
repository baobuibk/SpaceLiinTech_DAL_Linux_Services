"""
transport.py -- serial I/O layer for the BEE-DAL test client.
"""
import time

from . import protocol

READ_TICK_S = 0.2
READ_TICK_MS = READ_TICK_S * 1000.0


def hexdump(data: bytes, indent: str = "    ") -> str:
    """Classic offset / hex / ASCII dump, 16 bytes per line."""
    if not data:
        return f"{indent}(empty)"
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        hex_bytes = " ".join(f"{b:02X}" for b in chunk)
        ascii_repr = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{indent}{offset:04X}  {hex_bytes:<47}  {ascii_repr}")
    return "\n".join(lines)


class TimeoutResult:
    def __repr__(self):
        return "<timeout>"


TIMEOUT = TimeoutResult()


class SerialLink:
    """Thin wrapper: encode/send a TC, then wait for and decode one TM."""

    def __init__(self, ser, verbose: bool = False):
        self.ser = ser
        self.verbose = verbose

    # -- TX ------------------------------------------------------------
    def send_tc(self, type_: int, addr: int, payload: bytes = b"") -> bytes:
        frame = protocol.encode_tc(type_, addr, payload)

        if self.verbose:
            type_name = protocol.TYPE_NAMES.get(type_, "?")
            print(f"[TX] type=0x{type_:02X} ({type_name})  addr=0x{addr:02X}  "
                  f"payload_len={len(payload)}")
            if payload:
                print("[TX] payload:")
                print(hexdump(payload))
            print(f"[TX] frame ({len(frame)} bytes):")
            print(hexdump(frame))

        self.ser.reset_input_buffer()
        self.ser.write(frame)
        return frame

    # -- RX --------------------------------------------------------------
    def recv_tm(self, timeout_s: float):
        """Waits up to timeout_s for one CRC-valid TM frame. Returns
        (ack_err, addr_errcode, payload, elapsed_ms) or TIMEOUT."""
        dec = protocol.FrameDecoder()
        raw = bytearray()
        t0 = time.perf_counter()
        deadline = t0 + timeout_s

        while time.perf_counter() < deadline:
            chunk = self.ser.read(1)

            if not chunk:
                # No byte within this read tick -- feed the inter-byte
                # timeout so a corrupted LENGTH doesn't hang the wait.
                if dec.idle(READ_TICK_MS) == "TIMEOUT" and self.verbose:
                    print("[RX] inter-byte timeout -- decoder resynced "
                          "(possible corrupted LENGTH)")
                    raw = bytearray()
                continue

            b = chunk[0]
            raw.append(b)
            result = dec.feed(b)

            if result == "OK":
                elapsed_ms = (time.perf_counter() - t0) * 1000.0
                if self.verbose:
                    print(f"[RX] frame ({len(raw)} bytes), {elapsed_ms:.1f} ms:")
                    print(hexdump(bytes(raw)))
                    print(f"[RX] header: ACK/ERR=0x{dec.ack_err:02X}  "
                          f"ADDR/ERRCODE=0x{dec.addr_errcode:02X}  "
                          f"LENGTH={len(dec.payload)}")
                    if dec.payload:
                        print("[RX] payload:")
                        print(hexdump(bytes(dec.payload)))
                return dec.ack_err, dec.addr_errcode, bytes(dec.payload), elapsed_ms

            if result == "CRC_ERROR":
                if self.verbose:
                    print(f"[RX] CRC error -- calc=0x{dec.crc_calc:04X} "
                          f"rx=0x{dec.crc_rx:04X}, frame discarded:")
                    print(hexdump(bytes(raw)))
                else:
                    print("[!] CRC error, frame discarded")
                raw = bytearray()

        return TIMEOUT

    # -- combined --------------------------------------------------------
    def txrx(self, type_: int, addr: int, payload: bytes = b"", timeout_s: float = 3.0):
        """Send a TC and wait for the TM reply. Always prints a one-line
        summary (ACK/ERR result + round-trip time) regardless of verbose.
        Returns the response payload (bytes) on ACK, or None on ERR/timeout."""
        self.send_tc(type_, addr, payload)
        result = self.recv_tm(timeout_s)

        if result is TIMEOUT:
            print(f"[!] Timeout -- no valid response within {timeout_s * 1000:.0f} ms")
            return None

        ack_err, addr_errcode, resp_payload, elapsed_ms = result

        if ack_err == protocol.ACK_ERR_MARK:
            name = protocol.ERRCODE_NAMES.get(addr_errcode, f"unknown(0x{addr_errcode:02X})")
            print(f"[ERR] errcode=0x{addr_errcode:02X} ({name})  ({elapsed_ms:.1f} ms)")
            return None

        type_name = protocol.TYPE_NAMES.get(ack_err, f"0x{ack_err:02X}")
        print(f"[ACK] type={type_name} addr=0x{addr_errcode:02X} "
              f"len={len(resp_payload)}  ({elapsed_ms:.1f} ms)")
        return resp_payload
