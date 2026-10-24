"""Package ATmega128 images and upload them using the SAM-compatible xTP envelope."""
import argparse
import binascii
from pathlib import Path
import struct
import time
import zlib

APP_LIMIT = 0x1DF00
META_ADDR = APP_LIMIT
BOOT_START = 0x1E000
HW_ID = 5
MODE = 0xA2


def crc16(data):
    return binascii.crc_hqx(data, 0)


def ecc(data):
    a = b = 0
    for value in data:
        a ^= value
        b ^= value
        b = (b >> 1) ^ (0x8E if b & 1 else 0)
    return bytes([a, b])


def encode_frame(command, sequence, payload=b""):
    if len(payload) > 255:
        raise ValueError("xTP payload exceeds 255 bytes")
    body = struct.pack("<B H B B", MODE, command, sequence & 0x7F, len(payload)) + payload + ecc(payload)
    return b"\xc0\xde" + body + struct.pack("<H", crc16(body)) + b"\xda\xed"


def decode_frame(frame):
    if len(frame) < 13 or frame[:3] != b"\xc0\xde\xa2" or frame[-2:] != b"\xda\xed":
        raise ValueError("Invalid xTP envelope")
    length = frame[6]
    if len(frame) != length + 13 or crc16(frame[2:-4]) != struct.unpack("<H", frame[-4:-2])[0]:
        raise ValueError("Invalid xTP CRC or length")
    return struct.unpack("<H", frame[3:5])[0], frame[5] & 0x7F, frame[7:7 + length]


def read_hex(path):
    """Read checked Intel HEX with byte addresses; reject overlaps and unsupported records."""
    memory = {}
    base = 0
    eof = False
    for line_number, line in enumerate(Path(path).read_text(encoding="ascii").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if eof or not line.startswith(":"):
            raise ValueError(f"Invalid HEX record at line {line_number}")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as error:
            raise ValueError(f"Invalid HEX encoding at line {line_number}") from error
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xFF:
            raise ValueError(f"Invalid HEX checksum or length at line {line_number}")
        size, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        data = record[4:-1]
        if kind == 0:
            if address + size > 0x10000:
                raise ValueError("HEX data crosses a 64 KiB record boundary")
            for offset, value in enumerate(data):
                target = base + address + offset
                if target in memory:
                    raise ValueError("Overlapping HEX records")
                memory[target] = value
        elif kind == 1 and size == 0 and address == 0:
            eof = True
        elif kind in (2, 4) and size == 2 and address == 0:
            base = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind in (3, 5) and size == 4 and address == 0:
            # AVR reset is fixed at zero; execution-start records are not used.
            pass
        else:
            raise ValueError(f"Unsupported HEX record {kind}")
    if not eof or not memory:
        raise ValueError("Missing HEX EOF or image data")
    return memory


def hex_record(address, kind, data):
    body = bytes([len(data)]) + address.to_bytes(2, "big") + bytes([kind]) + data
    return ":" + (body + bytes([(-sum(body)) & 0xFF])).hex().upper()


def write_hex(path, memory):
    lines = []
    previous_base = None
    addresses = sorted(memory)
    index = 0
    while index < len(addresses):
        address = addresses[index]
        base = address >> 16
        if base != previous_base:
            lines.append(hex_record(0, 4, base.to_bytes(2, "big")))
            previous_base = base
        chunk = bytearray([memory[address]])
        index += 1
        while index < len(addresses) and len(chunk) < 16 and addresses[index] == address + len(chunk) and addresses[index] >> 16 == base:
            chunk.append(memory[addresses[index]])
            index += 1
        lines.append(hex_record(address & 0xFFFF, 0, bytes(chunk)))
    lines.append(hex_record(0, 1, b""))
    Path(path).write_text("\n".join(lines) + "\n", encoding="ascii")


def metadata(image, build=0):
    if len(image) < 2 or len(image) > APP_LIMIT or image[:2] == b"\xff\xff":
        raise ValueError("Application is empty, oversized, or missing its reset entry")
    header = bytearray(256)
    struct.pack_into("<I H B B I I I I I I", header, 0,
                     0xB007C0DE, 2, HW_ID, 0, len(image), zlib.crc32(image), 0, 0, build, 0)
    struct.pack_into("<I", header, 28, zlib.crc32(header))
    return bytes(header)


def package(input_path, output_path, build=0):
    memory = read_hex(input_path)
    if min(memory) != 0 or max(memory) >= APP_LIMIT:
        raise ValueError("Application must start at byte zero and exclude metadata/boot flash")
    image = bytes(memory.get(address, 0xFF) for address in range(max(memory) + 1))
    header = metadata(image, build)
    combined = dict(enumerate(image))
    combined.update({META_ADDR + i: value for i, value in enumerate(header)})
    write_hex(output_path, combined)
    return len(image)


def validate_package(memory):
    if not memory or min(memory) != 0 or max(memory) >= BOOT_START:
        raise ValueError("Package contains out-of-range addresses")
    try:
        header = bytes(memory[META_ADDR + i] for i in range(256))
    except KeyError as error:
        raise ValueError("Package is missing its complete metadata page") from error
    magic, version, hardware, flags, size, crc, load, entry, build, header_crc = struct.unpack_from("<I H B B I I I I I I", header)
    check = bytearray(header)
    check[28:32] = bytes(4)
    if (magic, version, hardware, flags, load, entry) != (0xB007C0DE, 2, HW_ID, 0, 0, 0) or any(header[32:]):
        raise ValueError("Unsupported metadata format or hardware")
    if not 0 < size <= APP_LIMIT or zlib.crc32(check) != header_crc:
        raise ValueError("Invalid metadata size or CRC")
    if any(address >= size and address < META_ADDR for address in memory):
        raise ValueError("Package contains bytes outside the declared image")
    image = bytes(memory.get(address, 0xFF) for address in range(size))
    if len(image) < 2 or image[:2] == b"\xff\xff" or zlib.crc32(image) != crc:
        raise ValueError("Invalid image CRC")
    return image, header


def fields(payload):
    if len(payload) < 6 or payload[0] != 1 or payload[4:6] != b"\x01\x00":
        raise ValueError("Unsupported xCLI response")
    result = {}
    position = 6
    while position < len(payload):
        name_length = payload[position]
        position += 1
        if position + name_length + 3 > len(payload):
            raise ValueError("Truncated field header")
        name = payload[position:position + name_length].decode("ascii")
        position += name_length
        kind = payload[position]
        size = int.from_bytes(payload[position + 1:position + 3], "little")
        position += 3
        if position + size > len(payload):
            raise ValueError("Truncated field data")
        raw = payload[position:position + size]
        position += size
        result[name] = raw.decode("utf-8") if kind == 9 else (raw if kind == 10 else int.from_bytes(raw, "little"))
    return result


class Link:
    def __init__(self, serial_port, timeout=30):
        self.port = serial_port
        self.timeout = timeout
        self.sequence = 0
        self.buffer = bytearray()

    def receive(self, deadline):
        while time.monotonic() < deadline:
            self.buffer.extend(self.port.read(1))
            while len(self.buffer) >= 2 and self.buffer[:2] != b"\xc0\xde":
                del self.buffer[0]
            if len(self.buffer) < 7:
                continue
            size = self.buffer[6] + 13
            if len(self.buffer) < size:
                continue
            frame = bytes(self.buffer[:size])
            del self.buffer[:size]
            try:
                return decode_frame(frame)
            except ValueError:
                continue
        raise TimeoutError("Timed out waiting for bootloader response")

    def exchange(self, command, payload, response_id, check):
        packet = encode_frame(command, self.sequence, payload)
        try:
            for attempt in range(4):
                self.port.write(packet)
                self.port.flush()
                deadline = time.monotonic() + self.timeout
                try:
                    while True:
                        identifier, _, data = self.receive(deadline)
                        if identifier == 0xFFFD and data == bytes([self.sequence]):
                            break
                        # A transport ACK alone never completes an operation.
                        if identifier == response_id and check(data):
                            return data
                except TimeoutError:
                    if attempt == 3:
                        raise
            raise TimeoutError("Bootloader rejected all retransmissions")
        finally:
            self.sequence = (self.sequence + 1) & 0x7F

    def command(self, identifier, text):
        request_sequence = self.sequence
        payload = bytes([0, identifier, request_sequence]) + text.encode("ascii")
        answer = self.exchange(0x0100, payload, 0x0101,
                               lambda data: len(data) >= 6 and data[:3] == bytes([1, identifier, request_sequence]))
        values = fields(answer)
        if answer[3] != 0 or values.get("ok", 1) != 1:
            raise RuntimeError(f"{text}: bootloader status {answer[3]}, {values}")
        return values

    def write(self, address, data):
        if not 1 <= len(data) <= 251:
            raise ValueError("Writes require 1..251 data bytes")
        expected = struct.pack("<I", address)
        try:
            answer = self.exchange(0x0200, expected + data, 0x0201,
                                   lambda payload: len(payload) == 5 and payload[:4] == expected)
        except TimeoutError as error:
            raise TimeoutError(f"No write result at 0x{address:05X} ({len(data)} bytes); "
                               "device may have reset or the UART link stalled. "
                               "Run diagnose before restarting the upload.") from error
        if answer[4]:
            raise RuntimeError(f"Flash write failed at 0x{address:05X}: target returned status {answer[4]}. "
                               "Run diagnose to inspect state/reset flags and flash readback.")


def diagnose(port_name, baud, address=0x900, length=32):
    """Query existing firmware without erase, program, journal commit or jump."""
    if not 0 <= address < BOOT_START or not 1 <= length <= 160 or length > BOOT_START - address:
        raise ValueError("Read must contain 1..160 bytes below the boot section")
    try:
        import serial
    except ImportError as error:
        raise SystemExit("Diagnostics require pyserial: python -m pip install -r AVRBootLoader/requirements.txt") from error
    with serial.Serial(port_name, baud, timeout=0.1, write_timeout=5) as port:
        port.reset_input_buffer()
        link = Link(port, timeout=2)
        port.write(encode_frame(0xFFFC, 0))  # Clear transport cache, not an MCU reset.
        port.flush()
        status = link.command(0x44, "bstatus")
        print("Status:", status)
        reset = status.get("reset", 0)
        flags = [name for mask, name in [(1, "power-on"), (2, "external"), (4, "brown-out"),
                                        (8, "internal-watchdog"), (16, "JTAG")] if reset & mask]
        print(f"Saved reset flags: 0x{reset:02X} ({', '.join(flags) or 'none'})")
        try:
            print("Version:", link.command(0x03, "version"))
        except RuntimeError as error:
            print(f"Version unavailable (older firmware may lack this command): {error}")
        readback = link.command(0x45, f"bread 0x{address:X} {length}")
        print(f"Flash at 0x{address:05X}:", readback.get("data", b"").hex(" "))
        print("No flash or EEPROM was modified. Reset flags describe the last reset, not necessarily the upload failure.")


def upload(port_name, baud, image_path):
    # Validate everything before opening a device or issuing an erase.
    image, header = validate_package(read_hex(image_path))
    try:
        import serial
    except ImportError as error:
        raise SystemExit("Upload requires pyserial: python -m pip install -r AVRBootLoader/requirements.txt") from error
    with serial.Serial(port_name, baud, timeout=0.1, write_timeout=5) as port:
        port.reset_input_buffer()
        link = Link(port)
        port.write(encode_frame(0xFFFC, 0))
        port.flush()
        link.command(0x48, "bhold")
        geometry = link.command(0x40, "bping")
        for name, expected in {"hw_id": HW_ID, "slots": 1, "s0_hdr": META_ADDR,
                               "s0_max": APP_LIMIT, "hdr_sz": 256, "meta_ver": 2, "app_addr": 0}.items():
            if geometry.get(name) != expected:
                raise RuntimeError(f"Unexpected device geometry: {name}={geometry.get(name)}")
        print("Connected. Erasing application flash...", flush=True)
        link.command(0x41, "berase 0")
        print(f"Erase completed. Writing {len(image)} application bytes...", flush=True)
        for start, content in [(0, image), (META_ADDR, header)]:
            offset = 0
            while offset < len(content):
                address = start + offset
                count = min(251, 256 - (address & 255), len(content) - offset)
                link.write(address, content[offset:offset + count])
                offset += count
            print(f"Written {len(content)} bytes at 0x{start:05X}")
        print("Verified:", link.command(0x42, "bverify 0"))
        link.command(0x43, "bjump 0")
        print("Application started")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    package_parser = commands.add_parser("package")
    package_parser.add_argument("input")
    package_parser.add_argument("output")
    package_parser.add_argument("--build", type=int, default=0)
    upload_parser = commands.add_parser("upload")
    upload_parser.add_argument("image")
    upload_parser.add_argument("--port", required=True)
    upload_parser.add_argument("--baud", type=int, default=115200)
    diagnose_parser = commands.add_parser("diagnose", help="read boot status, reset flags, version and flash without erasing")
    diagnose_parser.add_argument("--port", required=True)
    diagnose_parser.add_argument("--baud", type=int, default=115200)
    diagnose_parser.add_argument("--address", type=lambda value: int(value, 0), default=0x900)
    diagnose_parser.add_argument("--length", type=int, default=32)
    args = parser.parse_args()
    if args.action == "package":
        print(f"Packaged {package(args.input, args.output, args.build)} application bytes")
    elif args.action == "diagnose":
        diagnose(args.port, args.baud, args.address, args.length)
    else:
        upload(args.port, args.baud, args.image)


if __name__ == "__main__":
    main()
