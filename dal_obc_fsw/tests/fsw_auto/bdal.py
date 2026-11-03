"""Independent BEE-DAL framing for the automated tests.

Deliberately does not import tool/pc_client or lib/bee_dal_proto: the tests
check the flight software against the ICD, not against a copy of its code.
"""
import struct
import zlib

START = 0xA5
MAX_PAYLOAD = 32768
ERR = 0xFF
DEBUG_ACK = 0xFE

ERR_NAMES = {0x00: "invalid TC", 0x01: "data error", 0x02: "memory error", 0x03: "unknown"}


def crc16(data, crc=0xFFFF):
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout)."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def encode_tc(type_, addr, payload=b"", crc=None, length=None):
    """Build a TC frame. `crc`/`length` override the computed values (to corrupt frames)."""
    payload = bytes(payload)
    if length is None:
        length = len(payload)
    if crc is None:
        crc = crc16(payload)
    return (bytes((START, type_, addr)) + struct.pack("<H", length) + payload +
            struct.pack("<H", crc))


class ProtocolError(AssertionError):
    pass


class Tm:
    """One decoded telemetry frame."""

    def __init__(self, ack, code, payload, ms, raw=b"", request=None):
        self.ack, self.code, self.payload, self.ms = ack, code, payload, ms
        self.raw, self.request = raw, request

    @property
    def is_err(self):
        return self.ack == ERR

    def __repr__(self):
        if self.is_err:
            what = f"ERR {self.code:#04x} ({ERR_NAMES.get(self.code, '?')})"
        else:
            what = f"ACK {self.ack:#04x}/{self.code:#04x} len={len(self.payload)}"
        return f"<Tm {what} {self.ms:.1f} ms>"

    def _fail(self, message):
        req = f"\n  request: {self.request.hex(' ')}" if self.request else ""
        raise ProtocolError(f"{message}: {self!r}{req}\n  reply:   {self.raw[:64].hex(' ')}")

    def expect_ack(self, type_, addr, length=None):
        if self.is_err:
            self._fail(f"expected ACK {type_:#04x}/{addr:#04x}")
        if (self.ack, self.code) != (type_, addr):
            self._fail(f"ACK should echo {type_:#04x}/{addr:#04x}")
        if length is not None and len(self.payload) != length:
            self._fail(f"expected payload length {length}")
        return self.payload

    def expect_err(self, code):
        if not self.is_err or self.code != code:
            self._fail(f"expected ERR {code:#04x} ({ERR_NAMES.get(code, '?')})")
        if self.payload:
            self._fail("error reply must have an empty payload")
        return self


class FrameDecoder:
    """Streaming TM decoder: START, ACK/ERR, ADDR/ERRCODE, LEN(u16 LE), payload, CRC16 LE."""

    def __init__(self):
        self.reset()
        self.crc_errors = 0

    def reset(self):
        self.buf = bytearray()

    def feed(self, data):
        """Feed bytes; return a list of (ack, code, payload, raw) for each complete frame."""
        frames = []
        for byte in data:
            if not self.buf and byte != START:
                continue
            self.buf.append(byte)
            if len(self.buf) < 5:
                continue
            length = self.buf[3] | (self.buf[4] << 8)
            if length > MAX_PAYLOAD:
                raise ProtocolError(f"TM length {length} exceeds {MAX_PAYLOAD}")
            if len(self.buf) < 5 + length + 2:
                continue
            payload = bytes(self.buf[5:5 + length])
            rx_crc = self.buf[5 + length] | (self.buf[6 + length] << 8)
            raw = bytes(self.buf)
            self.reset()
            if rx_crc != crc16(payload):
                self.crc_errors += 1
                raise ProtocolError(f"TM CRC mismatch: rx={rx_crc:#06x} calc={crc16(payload):#06x}\n"
                                    f"  frame: {raw[:64].hex(' ')}")
            frames.append((raw[1], raw[2], payload, raw))
        return frames
