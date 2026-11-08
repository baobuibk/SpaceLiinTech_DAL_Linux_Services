"""Modbus RTU slave modelled on the DAL_PDU firmware register map
(service/ref/SpaceLiinTech_DAL_PDU_AVR_FW_HW1.1/doc/docs/modbus_implementation.md).

Only what dal_obc_fsw uses is modelled: FC1/FC2 reads, FC3/FC4 reads, FC5 coil write, FC6
holding-register write. Addresses are the zero-based request offsets (coil 00001 -> 0).
"""
import os
import threading


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


ILLEGAL_FUNCTION, ILLEGAL_ADDRESS, ILLEGAL_VALUE = 1, 2, 3
SETPOINT_RANGE = (800, 20000)   # TPS55287 range the OBC enforces; the fake enforces it too


class FakePdu:
    def __init__(self, master_fd, unit=1):
        self.fd, self.unit = master_fd, unit
        self.lock = threading.Lock()
        self.requests = []          # (function, offset, count_or_value) of every valid request
        self.online = True          # False: ignore everything (board unpowered / link down)
        self.reset()
        threading.Thread(target=self._run, daemon=True).start()

    def reset(self):
        with self.lock:
            self.fail_tps_read = False
            self.fail_input_read = False
            self.setpoint = 0xFFFF          # "none commanded since reset"
            self.vout_en = self.trans_en = False
            self.temp_tenth = 251
            self.mode = 0
            self.status = 0
            self.vout_ma, self.trans_mv, self.trans_ma = 0, 4980, 300
            self.in12_mv, self.in12_ma, self.in5_mv, self.in5_ma = 11900, 150, 5010, 90
            self.vout_mv_override = None    # None: follow the model
            self.build, self.version, self.patch = 0x01A2B3C4, 0x0001, 0
            self.unit_answers = True
            self.live_fault = False
            self.latched_fault = False
            self.heartbeat_enabled = True
            self.heartbeat_on_ms = 1000
            self.heartbeat_off_ms = 1000
            self.bootloader_requested = False
            self._pair = (0, 0)

    # -- derived measurements ---------------------------------------------------
    @property
    def vout_mv(self):
        if self.vout_mv_override is not None:
            return self.vout_mv_override
        return self.setpoint if (self.vout_en and self.setpoint != 0xFFFF) else 0

    def _identity(self):
        return [0x5044, 0x5500, self.version, self.patch, 12, self.unit, 115200 & 0xFFFF,
                (self.build >> 16) & 0xFFFF, self.build & 0xFFFF, 115200 >> 16]

    def _input(self, offset):  # input registers 30001.. -> offset = address - 30001
        table = {19: self.status, 20: self.temp_tenth & 0xFFFF, 21: self.mode,
                 22: self.setpoint, 23: self.vout_mv, 24: self.vout_ma,
                 25: self.in12_mv, 26: self.in12_ma, 27: self.trans_mv if self.trans_en else 0,
                 28: self.trans_ma if self.trans_en else 0, 29: self.in5_mv, 30: self.in5_ma}
        return table.get(offset)

    # -- protocol -----------------------------------------------------------------
    def _exception(self, func, code):
        body = bytes((self.unit, func | 0x80, code))
        return body + crc16(body).to_bytes(2, "little")

    def handle(self, func, offset, value):
        """Return the reply frame (with CRC) for one request."""
        with self.lock:
            self.requests.append((func, offset, value))
            ok = lambda body: body + crc16(body).to_bytes(2, "little")
            if func==1 and offset==0 and self.fail_tps_read:
                return self._exception(func,4)
            if func==4 and self.fail_input_read:
                return self._exception(func,4)
            if func in (1, 2):
                coils = {0: self.vout_en, 1: self.trans_en, 2: self.latched_fault,
                         9: self.heartbeat_enabled}
                inputs = {0: False, 1: False, 9: True, 10: self.trans_en}
                bits = coils if func == 1 else inputs
                if value < 1 or any(offset + i not in bits for i in range(value)):
                    return self._exception(func, ILLEGAL_ADDRESS)
                data = bytearray((value + 7) // 8)
                for i in range(value):
                    if bits[offset + i]:
                        data[i // 8] |= 1 << (i % 8)
                return ok(bytes((self.unit, func, len(data))) + bytes(data))
            if func in (3, 4):
                if func == 3:
                    words = {0: self.setpoint,
                             19: self.heartbeat_on_ms >> 16, 20: self.heartbeat_on_ms & 0xffff,
                             21: self.heartbeat_off_ms >> 16, 22: self.heartbeat_off_ms & 0xffff,
                             59: int(self.bootloader_requested)}
                    words.update({99 + i: w for i, w in enumerate(self._identity())})
                    get = words.get
                else:
                    get = self._input
                regs = [get(offset + i) for i in range(value)]
                if value < 1 or any(r is None for r in regs):
                    return self._exception(func, ILLEGAL_ADDRESS)
                body = bytes((self.unit, func, 2 * value)) + b"".join(r.to_bytes(2, "big") for r in regs)
                return ok(body)
            if func == 5:
                if offset not in (0, 1, 2, 9) or value not in (0x0000, 0xFF00):
                    return self._exception(func, ILLEGAL_ADDRESS if offset not in (0, 1, 2, 9)
                                           else ILLEGAL_VALUE)
                on = value == 0xFF00
                if offset == 0:
                    if on:
                        if self.setpoint == 0xFFFF:       # needs a programmed setpoint
                            return self._exception(func, ILLEGAL_VALUE)
                        self.trans_en = True              # enabling VOUT powers TRANS first
                    self.vout_en = on
                elif offset == 1:
                    self.trans_en = on
                    if not on:
                        self.vout_en = False              # disabling TRANS also drops VOUT
                elif offset == 2 and on:
                    if self.live_fault:
                        return self._exception(func, ILLEGAL_VALUE)
                    self.latched_fault = False
                elif offset == 9:
                    self.heartbeat_enabled = on
                return ok(bytes((self.unit, 5, offset >> 8, offset & 255, value >> 8, value & 255)))
            if func == 6:
                if offset == 59:
                    if value != 0xB007:
                        return self._exception(func, ILLEGAL_VALUE)
                    self.bootloader_requested = True
                    self.online = False
                    return ok(bytes((self.unit, 6, 0, 59, value >> 8, value & 255)))
                if offset != 0:
                    return self._exception(func, ILLEGAL_ADDRESS)
                if not SETPOINT_RANGE[0] <= value <= SETPOINT_RANGE[1]:
                    return self._exception(func, ILLEGAL_VALUE)
                self.trans_en = True                      # a write powers TRANS on first
                self.setpoint = value
                return ok(bytes((self.unit, 6, 0, 0, value >> 8, value & 255)))
            if func == 16:
                if offset not in (19, 21) or value != 2:
                    return self._exception(func, ILLEGAL_ADDRESS)
                high, low = self._pair
                period = (high << 16) | low
                if period == 0:
                    return self._exception(func, ILLEGAL_VALUE)
                if offset == 19:
                    self.heartbeat_on_ms = period
                else:
                    self.heartbeat_off_ms = period
                return ok(bytes((self.unit, 16, offset >> 8, offset & 255, 0, 2)))
            return self._exception(func, ILLEGAL_FUNCTION)

    def _run(self):
        pending = b""
        try:
            while True:
                pending += os.read(self.fd, 256)
                while len(pending) >= 8:
                    length = 9 + pending[6] if pending[1] == 16 else 8
                    if len(pending) < length:
                        break
                    frame = pending[:length]
                    if crc16(frame[:-2]).to_bytes(2, "little") != frame[-2:]:
                        pending = pending[1:]             # resynchronise byte by byte
                        continue
                    pending = pending[length:]
                    if not self.online or frame[0] != self.unit:
                        continue
                    func = frame[1]
                    offset = int.from_bytes(frame[2:4], "big")
                    value = int.from_bytes(frame[4:6], "big")
                    if func == 16 and value == 2 and frame[6] == 4:
                        self._pair = (int.from_bytes(frame[7:9], "big"),
                                      int.from_bytes(frame[9:11], "big"))
                    os.write(self.fd, self.handle(func, offset, value))
        except OSError:
            pass

    def writes(self):
        """Requests that change state (FC5/FC6), in order."""
        return [r for r in list(self.requests) if r[0] in (5, 6, 16)]
