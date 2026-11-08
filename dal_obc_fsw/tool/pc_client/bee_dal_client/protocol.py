"""
protocol.py
Frame layout
    TC (we send):  START(0xA5) TYPE ADDR LENGTH(u16,LE) PAYLOAD CRC16(LE)
    TM (we parse): START(0xA5) ACK/ERR ADDR/ERRCODE LENGTH(u16,LE) PAYLOAD CRC16(LE)
    CRC-16/CCSDS: poly 0x1021, init 0xFFFF, no reflect, xorout 0.
    If LENGTH==0, CRC16 is fixed 0xFFFF .
    Endianness: LITTLE-ENDIAN
"""
import struct

# =============================================================================
# Wire constants
# =============================================================================
START_BYTE = 0xA5
ACK_ERR_MARK = 0xFF

INTER_BYTE_TIMEOUT_MS = 500

ERRCODE_NAMES = {
    0x00: "TC invalid",
    0x01: "Data error",
    0x02: "Memory error",
    0x03: "Unknown error",
}

TYPE_NAMES = {
    0x00: "PING",
    0x01: "SYS_CFG",
    0x02: "EXP_CTRL",
    0x03: "DATA_XFER",
    0x04: "TELEMETRY",
    0x05: "SFU",
    0xFE: "DEBUG_ACK",
}

DATA_TYPE_NAMES = {0: "HK", 1: "SCI1", 2: "SCI2"}

TM_GET_FMT = "<IbI9h2H11h2H"
TM_GET_LAYOUT = [
    ('sys_time_epoch', 'U32', 4, 5),
    ('sys_status', 'I8', 1, 9),
    ('bootcount', 'U32', 4, 10),
    ('pdu_temp_cC', 'I16', 2, 14),
    ('exp_temp_cC', 'I16', 2, 16),
    ('obc_temp_cC', 'I16', 2, 18),
    ('ntc0_cC', 'I16', 2, 20),
    ('ntc1_cC', 'I16', 2, 22),
    ('ntc2_cC', 'I16', 2, 24),
    ('ntc3_cC', 'I16', 2, 26),
    ('mlx_avg_cC', 'I16', 2, 28),
    ('bme_temp_cC', 'I16', 2, 30),
    ('bme_pressure_10Pa', 'U16', 2, 32),
    ('bme_humidity_cPct', 'U16', 2, 34),
    ('imu_temp_cC', 'I16', 2, 36),
    ('ax_mg', 'I16', 2, 38),
    ('ay_mg', 'I16', 2, 40),
    ('az_mg', 'I16', 2, 42),
    ('gx_dps', 'I16', 2, 44),
    ('gy_dps', 'I16', 2, 46),
    ('gz_dps', 'I16', 2, 48),
    ('slf1_temp_cC', 'I16', 2, 50),
    ('slf1_flow_uL_min', 'I16', 2, 52),
    ('slf2_temp_cC', 'I16', 2, 54),
    ('slf2_flow_uL_min', 'I16', 2, 56),
    ('rba_status', 'U16', 2, 58),
    ('ta_status', 'U16', 2, 60),
]
TM_GET_FIELDS = [row[0] for row in TM_GET_LAYOUT]
assert struct.calcsize(TM_GET_FMT) == 57
assert sum(row[2] for row in TM_GET_LAYOUT) == 57

DAT_LST_FMT = "<III"
DAT_LST_FIELDS = ["hk_count", "sci1_count", "sci2_count"]
assert struct.calcsize(DAT_LST_FMT) == 12

DAT_LST_TC_FMT = "<B"  # required mark filter, 255 = all files
DAT_LST_FILTER_ALL = 0xFF  # no filter, same as omitting the payload
DAT_LST_FILTER_UNMARKED = 0x00

DAT_SEL_TC_FMT = "<BIH"  # data_type, data_index, chunk_size
DAT_GET_TC_FMT = "<I"    # chunk_index
DAT_DEL_TC_FMT = "<BI"   # data_type, data_index
DAT_MARK_TC_FMT = "<B"   # mark
DAT_MARK_RESERVED = 0xFF

DAT_SEL_MAX_CHUNK_SIZE = 32768

DAT_SEL_TM_FMT = "<BII20sIII"
assert struct.calcsize(DAT_SEL_TM_FMT) == 41, "dat_sel TM payload must be 41 bytes"

PING_TM_FMT = "<I"  
SYS_INFO_FMT = "<BB"
SYS_INFO_FIELDS = ["hw_version", "fw_version"]

SYS_RTC_TC_FMT = "<I"
SYS_RTC_TM_FMT = "<I"
PDU_VOUT_TC_FMT = "<H6x"
PDU_VOUT_TM_FMT = "<HHHH"  # setpoint mV, measured mV, measured mA, flags

# Debug shell 
SHELL_TYPE = 0xFF
SHELL_ADDR = 0x01
SHELL_MAX_CMD = 2048
SHELL_TM_HDR_FMT = "<BBBI"  # exit_status, exit_code, truncated, out_len
SHELL_EXIT_STATUS_NAMES = {0: "exited", 1: "signaled", 2: "timeout", 3: "error"}


def decode_version_byte(v: int) -> str:
    """101 -> '1.0.1', 222 -> '2.2.2' (decimal digits = major.minor.patch)."""
    return f"{v // 100}.{(v // 10) % 10}.{v % 10}"


# =============================================================================
# CRC-16/CCSDS 
# =============================================================================
def crc16_update(crc: int, byte: int) -> int:
    crc ^= (byte << 8)
    for _ in range(8):
        if crc & 0x8000:
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF
        else:
            crc = (crc << 1) & 0xFFFF
    return crc


def crc16_ccsds(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc = crc16_update(crc, b)
    return crc


# =============================================================================
# Encoder
# =============================================================================
def encode_tc(type_: int, addr: int, payload: bytes = b"") -> bytes:
    """Returns the full TC frame bytes, ready to write to the serial port."""
    length = len(payload)
    header_no_start = bytes([type_ & 0xFF, addr & 0xFF]) + struct.pack("<H", length)
    crc = crc16_ccsds(payload)
    return bytes([START_BYTE]) + header_no_start + payload + struct.pack("<H", crc)


# =============================================================================
# Decoder 
# =============================================================================
class FrameDecoder:
    WAIT_START, TYPE, ADDR, LEN_LO, LEN_HI, PAYLOAD, CRC_LO, CRC_HI = range(8)

    def __init__(self):
        self.ack_err = 0
        self.addr_errcode = 0
        self.payload = bytearray()
        self.reset()

    def reset(self):
        self.state = self.WAIT_START
        self.length = 0
        self.crc_calc = 0
        self.crc_rx = 0
        self.idle_ms = 0

    def feed(self, b: int):
        self.idle_ms = 0

        if self.state == self.WAIT_START:
            if b == START_BYTE:
                self.crc_calc = 0xFFFF
                self.state = self.TYPE
            return None

        if self.state == self.TYPE:
            self.ack_err = b
            self.state = self.ADDR
            return None

        if self.state == self.ADDR:
            self.addr_errcode = b
            self.state = self.LEN_LO
            return None

        if self.state == self.LEN_LO:
            self.length = b
            self.state = self.LEN_HI
            return None

        if self.state == self.LEN_HI:
            self.length |= b << 8
            self.payload = bytearray()
            self.state = self.CRC_LO if self.length == 0 else self.PAYLOAD
            return None

        if self.state == self.PAYLOAD:
            self.payload.append(b)
            self.crc_calc = crc16_update(self.crc_calc, b)
            if len(self.payload) >= self.length:
                self.state = self.CRC_LO
            return None

        if self.state == self.CRC_LO:
            self.crc_rx = b
            self.state = self.CRC_HI
            return None

        if self.state == self.CRC_HI:
            self.crc_rx |= b << 8
            result = "OK" if self.crc_rx == self.crc_calc else "CRC_ERROR"
            self.reset()
            return result

        self.reset()
        return None

    def idle(self, elapsed_ms: float):
        if self.state == self.WAIT_START:
            return None

        self.idle_ms += elapsed_ms
        if self.idle_ms >= INTER_BYTE_TIMEOUT_MS:
            self.reset()
            return "TIMEOUT"
        return None
