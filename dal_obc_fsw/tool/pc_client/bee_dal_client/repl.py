"""
repl.py -- interactive command layer for the BEE-DAL
"""
import os
import sys
import cmd
import time
import zlib
import struct
import argparse
import datetime

try:
    import serial
except ImportError:
    print("pyserial not installed -- run: pip install -r requirements.txt")
    sys.exit(1)

from . import protocol
from .transport import SerialLink


def print_table(headers, rows):
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    def fmt_row(cells):
        return "| " + " | ".join(c.ljust(w) for c, w in zip(cells, widths)) + " |"

    sep = "+-" + "-+-".join("-" * w for w in widths) + "-+"

    print(sep)
    print(fmt_row(headers))
    print(sep)
    for row in rows:
        print(fmt_row(row))
    print(sep)


class BeeDalRepl(cmd.Cmd):
    intro = ("BEE-DAL RS-422 test REPL\n"
             "Type 'help' for commands, 'quit' to exit.\n")
    prompt = "bee-dal> "

    def __init__(self, link: SerialLink):
        super().__init__()
        self.link = link
        self.timeout = 3.0  
        self.selection = None 

    # -- commands ----------------------------------------------------------
    def do_ping(self, arg):
        "ping -- Communication check, expect ACK + system uptime. TYPE=0x00 ADDR=0x00"
        payload = self.link.txrx(0x00, 0x00, timeout_s=self.timeout)
        if payload is not None and len(payload) >= 4:
            (uptime,) = struct.unpack(protocol.PING_TM_FMT, payload[:4])
            print(f"    uptime = {uptime} s")

    def do_sys_info(self, arg):
        "sys_info -- Get hw/fw version. TYPE=0x01 ADDR=0x00"
        payload = self.link.txrx(0x01, 0x00, timeout_s=self.timeout)
        if payload is None:
            return
        expected = struct.calcsize(protocol.SYS_INFO_FMT)
        if len(payload) != expected:
            print(f"[!] unexpected payload length {len(payload)} (expected {expected})")
            return
        hw, fw = struct.unpack(protocol.SYS_INFO_FMT, payload)
        print(f"    hw_version = {hw}  ({protocol.decode_version_byte(hw)})")
        print(f"    fw_version = {fw}  ({protocol.decode_version_byte(fw)})")

    def do_sys_rtc(self, arg):
        "sys_rtc [epoch|now] -- Get system time, or set from epoch/PC time. TYPE=0x01 ADDR=0x01"
        arg = arg.strip()
        epoch = None
        if arg.lower() == "now":
            epoch = int(time.time())
        elif arg:
            try:
                epoch = int(arg, 0)
            except ValueError:
                print("usage: sys_rtc [epoch|now]  (omit to read; 'now' sends PC time)")
                return
        if epoch is not None and not 0 <= epoch <= 0xffffffff:
            print("epoch must be an unsigned 32-bit Unix time")
            return
        payload = struct.pack(protocol.SYS_RTC_TC_FMT, epoch) if epoch is not None else b""
        resp = self.link.txrx(0x01, 0x01, payload, timeout_s=self.timeout)
        if resp is None:
            return
        if len(resp) != struct.calcsize(protocol.SYS_RTC_TM_FMT):
            print(f"[!] unexpected payload length {len(resp)} (expected 4)")
            return
        (reported,) = struct.unpack(protocol.SYS_RTC_TM_FMT, resp)
        dt = datetime.datetime.fromtimestamp(reported, datetime.timezone.utc)
        action = "system epoch" if epoch is None else "applied epoch"
        print(f"    {action} = {reported}  ({dt.isoformat()})")

    def do_pdu_vout(self, arg):
        "pdu_vout get | set <mV> -- Read or change PDU VOUT. TYPE=0x01 ADDR=0x02"
        parts = arg.split()
        if parts == ["get"]:
            payload = b""
            selected_count = 0
        elif len(parts) == 2 and parts[0] == "set":
            try:
                millivolts = int(parts[1], 0)
            except ValueError:
                millivolts = -1
            if not 800 <= millivolts <= 20000:
                print("VOUT must be 800..20000 mV")
                return
            payload = struct.pack(protocol.PDU_VOUT_TC_FMT, millivolts)
        else:
            print("usage: pdu_vout get | pdu_vout set <800..20000 mV>")
            return
        resp = self.link.txrx(0x01, 0x02, payload, timeout_s=self.timeout)
        if resp is None:
            return
        if len(resp) != struct.calcsize(protocol.PDU_VOUT_TM_FMT):
            print(f"[!] unexpected payload length {len(resp)} (expected 8)")
            return
        setpoint, voltage, current, flags = struct.unpack(protocol.PDU_VOUT_TM_FMT, resp)
        print(f"    TRANS={'on' if flags & 1 else 'off'}  VOUT={'on' if flags & 2 else 'off'}")
        print(f"    setpoint={setpoint if flags & 4 else 'unavailable'} mV  "
              f"measured={voltage if flags & 8 else 'unavailable'} mV  "
              f"current={current if flags & 16 else 'unavailable'} mA")

    def do_pwr_en(self, arg):
        "pwr_en <OBC|EXP|PDU> get | <output> on|off | set <select_mask> <on_mask> [force]"
        parts = arg.split()
        if len(parts) < 2 or parts[0].upper() not in ("OBC", "EXP", "PDU"):
            print("usage: pwr_en <OBC|EXP|PDU> get | <output> on|off | set <select_mask> <on_mask> [force]")
            return
        addr = {"OBC": 3, "EXP": 4, "PDU": 5}[parts.pop(0).upper()]
        limit = {3: 0x00ff, 4: 0x0007, 5: 0x0003}[addr]
        names = {
            3: ("AI_power", "FPGA_power", "USB1_power", "USB2_power",
                "USB3_power", "USB4_power", "MIPI1_power", "MIPI2_power"),
            4: ("EXP12V", "SLN_HD", "MOTOR"),
            5: ("VOUT", "TRANS"),
        }[addr]
        if parts == ["get"]:
            payload = bytes([addr])
            selected_count = 0
        elif len(parts) in (2, 3) and parts[0].lower() in (name.lower() for name in names):
            bit = [name.lower() for name in names].index(parts[0].lower())
            state = parts[1].lower()
            force = len(parts) == 3 and parts[2].lower() == "force"
            if state not in ("on", "off") or (len(parts) == 3 and not force) or \
                    (force and (addr != 4 or state != "off")):
                print("invalid power state or force option")
                return
            select = 1 << bit
            on = select if state == "on" else 0
            payload = struct.pack("<BHHB", addr, select, on, int(force))
            selected_count = 1
        elif len(parts) in (3, 4) and parts[0] == "set":
            try:
                select, on = int(parts[1], 0), int(parts[2], 0)
            except ValueError:
                select, on = -1, -1
            force = len(parts) == 4 and parts[3] == "force"
            if not 0 < select <= limit or on < 0 or on & ~select or \
                    select & ~limit or (len(parts) == 4 and not force) or \
                    (force and (addr != 4 or on)) or \
                    (addr == 5 and select == 3 and on == 1):
                print("invalid power mask or force option")
                return
            payload = struct.pack("<BHHB", addr, select, on, int(force))
            selected_count = bin(select).count("1")
        else:
            print("usage: pwr_en <board> get | <output_name> on|off [force for EXP off]")
            print("outputs: " + ", ".join(names))
            print("for multiple outputs: set <select_mask> <on_mask> [force for EXP off]")
            return
        timeout = max(self.timeout, 15.0 * (selected_count + 1)) if addr == 4 else self.timeout
        reply = self.link.txrx(0x01, 0x32, payload, timeout_s=timeout)
        if reply is None:
            return
        if len(reply) != 7 or reply[0] != addr:
            print(f"[!] unexpected power reply (expected board {addr} and 7 bytes)")
            return
        on, known, failed = struct.unpack("<HHH", reply[1:])
        print(f"    on=0x{on:04X} known=0x{known:04X} failed=0x{failed:04X}")

    def do_pwr_stat(self, arg):
        "pwr_stat all|monitor|enable|error -- ICD v1.2 one-byte report selector"
        reports = {"all": 0, "monitor": 1, "enable": 2, "error": 3}
        name = arg.strip().lower()
        if name not in reports:
            print("usage: pwr_stat all|monitor|enable|error")
            return
        reply = self.link.txrx(1, 4, bytes([reports[name]]), timeout_s=max(self.timeout, 15))
        if reply is None:
            return
        if len(reply) < 6 or len(reply) > 200:
            print("[!] invalid pwr_stat length")
            return
        schema, count, errors = struct.unpack_from("<HHH", reply)
        if schema != 0x200 | reports[name] or len(reply) != 22 + 6 * count or errors & ~0x8007:
            print("[!] invalid pwr_stat header")
            return
        print(f"report={name} availability_errors=0x{errors & 7:04X} hardware_faults=unavailable")
        for off in range(6, len(reply)-16, 6):
            desc, mv, ma = struct.unpack_from("<HHh", reply, off)
            board, source, flags = desc >> 4 & 7, desc & 15, desc >> 7 & 31
            state = "unknown" if not flags & 1 else "on" if flags & 2 else "off"
            print(f"  board={board} source={source} state={state} "
                  f"voltage={str(mv)+' mV' if flags & 4 else 'unavailable'} "
                  f"current={str(ma)+' mA' if flags & 8 else 'unavailable'}")

        valid, flags, mode, tps, temp, age, setpoint, reserved = struct.unpack_from("<HHBBhIHH", reply, len(reply)-16)
        names = ["UNKNOWN", "UNPOWERED", "STARTING", "READY", "COMM_ERROR"]
        print(f"  PDU valid=0x{valid:04X} flags=0x{flags:04X} mode={mode} TPS={names[tps] if tps<5 else 'INVALID'} age_ms={age}")
        if valid & 4:
            print(f"  temperature={temp/10:.1f} C")

    def do_tm_get(self, arg):
        "tm_get -- Get payload telemetry. TYPE=0x04 ADDR=0x00"
        payload = self.link.txrx(0x04, 0x00, timeout_s=self.timeout)
        if payload is None:
            return
        expected = struct.calcsize(protocol.TM_GET_FMT)
        if len(payload) != expected:
            print(f"[!] unexpected payload length {len(payload)} (expected {expected})")
            return

        values = struct.unpack(protocol.TM_GET_FMT, payload)
        rows = []
        for (name, dtype, size, offset), val in zip(protocol.TM_GET_LAYOUT, values):
            if name == "sys_time_epoch":
                dt = datetime.datetime.utcfromtimestamp(val)
                result = f"{val}  ({dt.isoformat()}Z)"
            elif (dtype == "I16" and val == -32768) or (dtype == "U16" and val == 65535):
                result = "unavailable"
            else:
                result = str(val)
            rows.append((name, dtype, str(offset), str(size), result))

        print_table(["Field", "Data type", "Address", "Size", "Value"], rows)

    def do_tm_obc_get(self, arg):
        "tm_obc_get -- Get the complete OBC housekeeping snapshot. TYPE=0x04 ADDR=0x02"
        payload = self.link.txrx(0x04, 0x02, timeout_s=self.timeout)
        if payload is None:
            return
        if len(payload) != 64:
            print(f"[!] unexpected payload length {len(payload)} (expected 64)")
            return
        valid, epoch, boot, onboard = struct.unpack_from("<HIIh", payload, 0)
        ntc = struct.unpack_from("<4h", payload, 12)
        bme_temp, pressure, humidity, gas = struct.unpack_from("<hIHI", payload, 20)
        voltage = struct.unpack_from("<8H", payload, 32)
        current = struct.unpack_from("<8h", payload, 48)
        names = ("5V_SAMA7", "5V_FPGA", "3V3", "5V_AI", "PMIC_3V3",
                 "PMIC_DDR_1V35", "PMIC_CORE_1V15", "PMIC_CPU_1V15")
        print(f"    snapshot = {epoch}  boot_count = {boot}  valid_mask = 0x{valid:04X}")
        print(f"    onboard = {onboard / 100:.2f} C"
              + ("" if valid & 1 else " (invalid)"))
        for i, value in enumerate(ntc):
            print(f"    NTC{i + 1} = {value / 100:.2f} C"
                  + ("" if valid & (1 << (i + 1)) else " (invalid)"))
        print(f"    BME = {bme_temp / 100:.2f} C, {pressure} Pa, "
              f"{humidity / 100:.2f} %RH"
              + ("" if valid & (1 << 5) else " (invalid)"))
        print(f"    gas = {gas} ohm"
              + ("" if valid & (1 << 6) else " (invalid)"))
        for i, name in enumerate(names):
            chip_valid = valid & (1 << (7 if i < 4 else 8))
            print(f"    {name} = {voltage[i]} mV, {current[i]} mA"
                  + ("" if chip_valid else " (invalid)"))

    def _exp_reply(self, addr, payload):
        reply = self.link.txrx(0x02, addr, payload, timeout_s=25.0)
        if reply is None:
            return
        if len(reply) < 3:
            print("[!] short EXP gateway reply")
            return
        status, length = struct.unpack_from("<BH", reply)
        if len(reply) != length + 3:
            print("[!] malformed EXP gateway reply length")
            return
        print(f"    EXP gateway status = {status}")
        print(reply[3:].decode("utf-8", errors="replace"))

    def do_exp_ping(self, arg):
        "exp_ping -- Check EXP UART using tlm_json system. TYPE=0x02 ADDR=0x0A"
        self._exp_reply(0x0A, b"")

    def do_exp_tlm(self, arg):
        "exp_tlm <section> -- Get EXP schema-2 telemetry. TYPE=0x02 ADDR=0x0B"
        section = arg.strip()
        allowed = {"system", "sensors", "power", "pc", "heaters", "pump",
                   "valves", "motors", "burn", "ir", "cameras", "data", "all"}
        if section not in allowed:
            print("usage: exp_tlm <system|sensors|power|pc|heaters|pump|valves|motors|burn|ir|cameras|data|all>")
            return
        self._exp_reply(0x0B, section.encode("ascii"))

    def do_exp_run(self, arg):
        "exp_run <EXP command> -- Run EXP UART text command. TYPE=0x02 ADDR=0x0C"
        command = arg.strip()
        if not command or len(command.encode("utf-8")) > 95:
            print("usage: exp_run <EXP command up to 95 bytes>")
            return
        self._exp_reply(0x0C, command.encode("utf-8"))

    def do_sen_bme_cfg(self, arg):
        "sen_bme_cfg EXP get|set <temp_os> <press_os> <hum_os> <filter>|result <id> -- Configure EXP BME via TYPE=0x02 ADDR=0x0C"
        parts = arg.split()
        usage = "usage: sen_bme_cfg EXP get | set <skip|1x|2x|4x|8x|16x> <skip|1x|2x|4x|8x|16x> <skip|1x|2x|4x|8x|16x> <off|1|3|7|15|31|63|127> | result <id>"
        if len(parts) < 2 or parts[0].upper() != "EXP":
            print(usage)
            return
        action = parts[1].lower()
        if action == "get" and len(parts) == 2:
            command = "bme688_cfg_get"
        elif action == "set" and len(parts) == 6:
            os_codes = {"skip": 0, "1x": 1, "2x": 2, "4x": 3, "8x": 4, "16x": 5}
            filter_codes = {"off": 0, "1": 1, "3": 2, "7": 3,
                            "15": 4, "31": 5, "63": 6, "127": 7}
            names = [part.lower() for part in parts[2:]]
            if any(name not in os_codes for name in names[:3]) or names[3] not in filter_codes:
                print(usage)
                return
            command = "bme688_cfg_set {} {} {} {} 0 0 0".format(
                *(os_codes[name] for name in names[:3]), filter_codes[names[3]])
        elif action == "result" and len(parts) == 3:
            try:
                request_id = int(parts[2], 0)
            except ValueError:
                request_id = 0
            if not 1 <= request_id <= 0xffffffff:
                print(usage)
                return
            command = f"bme688_result {request_id}"
        else:
            print(usage)
            return
        self.do_exp_run(command)

    def do_sen_imu_cfg(self, arg):
        "sen_imu_cfg EXP get|reset|set accel|gyro <odr> <scale>|set freefall|wakeup <threshold> <duration>|result <id> -- Configure EXP IMU via TYPE=0x02 ADDR=0x0C"
        parts = arg.split()
        usage = ("usage: sen_imu_cfg EXP get | reset | result <id> | "
                 "set accel|gyro <off|12.5Hz|26Hz|52Hz|104Hz|208Hz|416Hz|833Hz> <scale> | "
                 "set freefall|wakeup <threshold> <duration>")
        if len(parts) < 2 or parts[0].upper() != "EXP":
            print(usage)
            return
        action = parts[1].lower()
        if action == "get" and len(parts) == 2:
            command = "lsm6d_conf_get"
        elif action == "reset" and len(parts) == 2:
            command = "lsm6d_init"
        elif action == "result" and len(parts) == 3:
            try:
                request_id = int(parts[2], 0)
            except ValueError:
                request_id = 0
            if not 1 <= request_id <= 0xffffffff:
                print(usage)
                return
            command = f"lsm6d_result {request_id}"
        elif action == "set" and len(parts) == 5:
            channel = parts[2].lower()
            if channel in ("accel", "gyro"):
                odr_codes = {"off": 0, "12.5hz": 1, "26hz": 2, "52hz": 3,
                             "104hz": 4, "208hz": 5, "416hz": 6, "833hz": 7}
                accel_scales = {"2g": 0, "16g": 1, "4g": 2, "8g": 3}
                gyro_scales = {"125dps": 0, "250dps": 1, "500dps": 2,
                               "1000dps": 3, "2000dps": 4}
                scales = gyro_scales if channel == "gyro" else accel_scales
                odr, scale = parts[3].lower(), parts[4].lower()
                if odr not in odr_codes or scale not in scales:
                    print(usage)
                    return
                command = f"lsm6d_conf_{channel} {odr_codes[odr]} {scales[scale]}"
            elif channel in ("freefall", "wakeup"):
                try:
                    threshold, duration = int(parts[3], 0), int(parts[4], 0)
                except ValueError:
                    print(usage)
                    return
                bounds = (7, 63) if channel == "freefall" else (63, 3)
                if not 0 <= threshold <= bounds[0] or not 0 <= duration <= bounds[1]:
                    print(usage)
                    return
                suffix = "ff" if channel == "freefall" else "wu"
                command = f"lsm6d_conf_{suffix} {threshold} {duration}"
            else:
                print(usage)
                return
        else:
            print(usage)
            return
        self.do_exp_run(command)

    def do_sen_stat(self, arg):
        "sen_stat <OBC|EXP|PDU> get <all|temperature|bme|imu|flow|power|camera|limits> [source] [cursor n] -- Paged sensor values and health. TYPE=0x01 ADDR=0x07"
        parts = arg.split()
        usage = "usage: sen_stat <OBC|EXP|PDU> get <group> [source] [cursor n]"
        boards = {"OBC": 3, "EXP": 4, "PDU": 5}
        groups = {name: i for i, name in enumerate(
            ("all", "temperature", "bme", "imu", "flow", "power", "camera", "limits"))}
        sources = {
            3: ("all", "board_temp", "ntc", "bme", "pac_u6", "pac_u2", "mipi1", "mipi2"),
            4: ("all", "board_temp", "ntc", "bme", "imu", "flow1", "flow2",
                "pac", "camera1", "camera2", "limits"),
            5: ("all", "board_temp", "pac"),
        }
        if len(parts) < 3 or parts[0].upper() not in boards or parts[1].lower() != "get" or parts[2].lower() not in groups:
            print(usage)
            return
        board = boards[parts[0].upper()]
        group = groups[parts[2].lower()]
        remaining = parts[3:]
        source = 0
        if remaining and remaining[0].lower() != "cursor":
            try:
                source = sources[board].index(remaining[0].lower())
            except ValueError:
                print(usage)
                return
            remaining = remaining[1:]
        cursor = 0
        if remaining:
            if len(remaining) != 2 or remaining[0].lower() != "cursor":
                print(usage)
                return
            try:
                cursor = int(remaining[1], 0)
            except ValueError:
                print(usage)
                return
            if not 0 <= cursor <= 254:
                print(usage)
                return
        reply = self.link.txrx(0x01, 0x07, bytes((board, group, source, cursor)), timeout_s=self.timeout)
        if reply is None:
            return
        if len(reply) < 10 or len(reply) != 10 + 12 * reply[4] or reply[0] != board:
            print("[!] invalid sen_stat reply")
            return
        epoch = struct.unpack_from("<I", reply, 6)[0]
        next_cursor = "end" if reply[5] == 255 else str(reply[5])
        print(f"    {parts[0].upper()} {'offline' if reply[3] else 'online'} epoch={epoch} records={reply[4]} next_cursor={next_cursor}")
        for offset in range(10, len(reply), 12):
            sensor_id, channel, flags, unit, age, value = struct.unpack_from("<BBBBIi", reply, offset)
            name = sources[board][sensor_id] if sensor_id < len(sources[board]) else "unknown"
            if flags & 1:
                reading = {1: lambda: f"{value / 100:.2f} C",
                           2: lambda: f"{value} Pa",
                           3: lambda: f"{value / 100:.2f} %RH",
                           4: lambda: f"{value} ohm",
                           5: lambda: f"{value} mg",
                           6: lambda: f"{value / 1000:.3f} dps",
                           7: lambda: f"{value / 100:.2f} uL/min",
                           8: lambda: "yes" if value else "no"}.get(unit, lambda: str(value))()
            else:
                reading = ("unsupported" if flags & 0x20 else "error" if flags & 0x10
                           else "disabled" if flags & 0x08 else "stale" if flags & 0x02
                           else "unavailable")
            age_text = "unknown" if age == 0xffffffff else str(age)
            print(f"    {name}[{channel}] {reading} flags=0x{flags:02x} age_ms={age_text}")

    def do_uvc_cfg(self, arg):
        "uvc_cfg <1|2|3|4> get|set <setting> <value> ... -- USB camera profile. TYPE=0x01 ADDR=0x10"
        parts = arg.split()
        usage = "usage: uvc_cfg <1|2|3|4> get|set <setting> <value> ..."
        if len(parts) < 2 or parts[0] not in ("1", "2", "3", "4"):
            print(usage)
            return
        camera = int(parts[0])
        if parts[1] == "get" and len(parts) == 2:
            payload = bytes((camera,))
        elif parts[1] == "set" and len(parts) >= 4 and len(parts) % 2 == 0:
            data = bytearray(32)
            data[0:2] = bytes((camera, 1))
            mask = 0
            fields = {"exposure_time": (5, 17, 4), "gain": (6, 21, 2),
                      "focus": (8, 24, 2), "white_balance_temp": (10, 27, 2),
                      "brightness": (11, 29, 2)}
            try:
                for name, value in zip(parts[2::2], parts[3::2]):
                    if name == "format" and value == "MJPG":
                        data[4:8] = b"MJPG"; mask |= 1
                    elif name == "resolution":
                        width, height = map(int, value.lower().split("x"))
                        if not 1 <= width <= 65535 or not 1 <= height <= 65535: raise ValueError
                        struct.pack_into("<HH", data, 8, width, height); mask |= 6
                    elif name == "fps":
                        fps = int(value, 0)
                        if not 1 <= fps <= 65535: raise ValueError
                        struct.pack_into("<HH", data, 12, fps, 1); mask |= 8
                    elif name == "exposure" and value in ("auto", "manual"):
                        data[16] = int(value == "manual"); mask |= 1 << 4
                    elif name == "autofocus" and value in ("on", "off"):
                        data[23] = int(value == "on"); mask |= 1 << 7
                    elif name == "white_balance" and value in ("auto", "manual"):
                        data[26] = int(value == "auto"); mask |= 1 << 9
                    elif name in fields:
                        bit, offset, size = fields[name]
                        number = int(value, 0)
                        limit = 1 << (8 * size)
                        if name == "brightness":
                            if not -32768 <= number <= 32767: raise ValueError
                        elif not 0 <= number < limit: raise ValueError
                        data[offset:offset + size] = number.to_bytes(size, "little", signed=name == "brightness")
                        mask |= 1 << bit
                    else: raise ValueError
                struct.pack_into("<H", data, 2, mask)
            except (ValueError, TypeError):
                print(usage)
                return
            payload = bytes(data)
        else:
            print(usage)
            return
        reply = self.link.txrx(0x01, 0x30 if len(payload) == 1 else 0x10, payload, timeout_s=max(self.timeout, 30))
        if reply is None:
            return
        if len(reply) != 32 or reply[0] != camera or reply[1] != 1:
            print("[!] invalid uvc_cfg reply")
            return
        mask = struct.unpack_from("<H", reply, 2)[0]
        print(f"    camera={camera} mask=0x{mask:03x} format={reply[4:8].decode('ascii', errors='replace') if mask & 1 else 'unset'}")
        if mask & 6:
            print(f"    resolution={struct.unpack_from('<H', reply, 8)[0]}x{struct.unpack_from('<H', reply, 10)[0]}")
        if mask & 8:
            print(f"    fps={struct.unpack_from('<H', reply, 12)[0]}/{struct.unpack_from('<H', reply, 14)[0]}")
        for bit, label, offset, size in ((4, "exposure", 16, 1), (5, "exposure_time", 17, 4),
                                         (6, "gain", 21, 2), (7, "autofocus", 23, 1),
                                         (8, "focus", 24, 2), (9, "white_balance", 26, 1),
                                         (10, "white_balance_temp", 27, 2), (11, "brightness", 29, 2)):
            if mask & (1 << bit):
                value = int.from_bytes(reply[offset:offset + size], "little", signed=bit == 11)
                if bit == 4: value = "manual" if value else "auto"
                if bit == 7: value = "on" if value else "off"
                if bit == 9: value = "auto" if value else "manual"
                print(f"    {label}={value}")

    def do_uvc_ctl(self, arg):
        "uvc_ctl <1|2|3|4> capture <request_id> -- Queue one JPEG. TYPE=0x01 ADDR=0x11"
        parts = arg.split()
        if len(parts) != 3 or parts[0] not in ("1", "2", "3", "4") or parts[1] != "capture":
            print("usage: uvc_ctl <1|2|3|4> capture <request_id>")
            return
        try:
            request_id = int(parts[2], 0)
            if not 1 <= request_id <= 0xffffffff: raise ValueError
        except ValueError:
            print("request_id must be 1..4294967295")
            return
        payload = struct.pack("<BBHI", int(parts[0]), 1, 0, request_id)
        reply = self.link.txrx(0x01, 0x11, payload, timeout_s=self.timeout)
        if reply is not None:
            if len(reply) != 4:
                print("[!] invalid uptime ACK")
            else:
                print(f"    capture accepted; uptime={struct.unpack('<I', reply)[0]} s")

    def do_uvc_stat(self, arg):
        "uvc_stat <1|2|3|4> get <request_id> -- Read JPEG capture state. TYPE=0x01 ADDR=0x12"
        parts = arg.split()
        if len(parts) != 3 or parts[0] not in ("1", "2", "3", "4") or parts[1] != "get":
            print("usage: uvc_stat <1|2|3|4> get <request_id>")
            return
        try:
            request_id = int(parts[2], 0)
            if not 1 <= request_id <= 0xffffffff: raise ValueError
        except ValueError:
            print("request_id must be 1..4294967295")
            return
        reply = self.link.txrx(0x01, 0x31, struct.pack("<BI", int(parts[0]), request_id),
                               timeout_s=self.timeout)
        if reply is not None:
            self._show_uvc_capture_reply(reply, int(parts[0]), request_id)

    @staticmethod
    def _show_uvc_capture_reply(reply, camera, request_id):
        if len(reply) != 8 or reply[0] != camera or struct.unpack_from("<I", reply, 4)[0] != request_id:
            print("[!] invalid UVC capture reply")
            return
        states = ("unknown", "queued", "capturing", "saved", "failed")
        state = states[reply[1]] if reply[1] < len(states) else "invalid"
        print(f"    camera={camera} request_id={request_id} state={state} error={reply[2]}")
        if reply[1] == 3:
            print(f"    path=/home/bee/data/images/usb/USB{camera-1}_{request_id:08d}.jpg")

    def do_dat_lst(self, arg):
        "dat_lst <mark> -- Get HK/SCI1/SCI2 file counts. TYPE=0x03 ADDR=0x00"
        try:
            mark_filt = int(arg.strip(), 0)
            if not 0 <= mark_filt <= 255:
                raise ValueError
        except ValueError:
            print("usage: dat_lst <mark>  (0-254, or 0xFF for all)")
            return
        tc_payload = struct.pack(protocol.DAT_LST_TC_FMT, mark_filt)
        self.selection = None
        payload = self.link.txrx(0x03, 0x00, tc_payload, timeout_s=self.timeout)
        if payload is None:
            return
        expected = struct.calcsize(protocol.DAT_LST_FMT)
        if len(payload) != expected:
            print(f"[!] unexpected payload length {len(payload)} (expected {expected})")
            return
        counts = struct.unpack(protocol.DAT_LST_FMT, payload)
        for name, val in zip(protocol.DAT_LST_FIELDS, counts):
            print(f"    {name:12s} = {val}")

    def do_dat_sel(self, arg):
        "dat_sel <type 0=housekeeping|1=images|2=logs> <index> <chunk_size> -- Select a file. TYPE=0x03 ADDR=0x01"
        parts = arg.split()
        if len(parts) != 3:
            print("usage: dat_sel <data_type 0|1|2> <index> <chunk_size>")
            return
        try:
            data_type, index, chunk_size = (int(x, 0) for x in parts)
        except ValueError:
            print("[!] arguments must be integers")
            return

        if not (0 <= data_type <= 2 and 0 <= index <= 0xFFFFFFFF and 1 <= chunk_size <= 32768):
            print("[!] type 0..2, index 0..4294967295, chunk_size 1..32768 required")
            return
        payload = struct.pack(protocol.DAT_SEL_TC_FMT, data_type, index, chunk_size)
        resp = self.link.txrx(0x03, 0x01, payload, timeout_s=self.timeout)
        self.selection = None
        if resp is None:
            return

        expected = struct.calcsize(protocol.DAT_SEL_TM_FMT)
        if len(resp) != expected:
            print(f"[!] unexpected payload length {len(resp)} (expected {expected})")
            return

        dtype, didx, csize, name_raw, fsize, fcrc32, nchunks = \
            struct.unpack(protocol.DAT_SEL_TM_FMT, resp)
        name = name_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")

        self.selection = {
            "data_type": dtype, "index": didx, "chunk_size": csize,
            "name": name, "size": fsize, "crc32": fcrc32, "chunk_count": nchunks,
        }

        print(f"    name        = {name}")
        print(f"    size        = {fsize} bytes")
        print(f"    crc32       = 0x{fcrc32:08X}")
        print(f"    chunk_count = {nchunks}  (chunk_size={csize})")
        print("    -> 'dat_download' to pull the whole file, or 'dat_get <n>' for one chunk")

    def do_dat_get(self, arg):
        "dat_get <chunk_index> -- Download one chunk of the selected file. TYPE=0x03 ADDR=0x02"
        parts = arg.split()
        if len(parts) != 1:
            print("usage: dat_get <chunk_index>")
            return
        try:
            chunk_index = int(parts[0], 0)
        except ValueError:
            print("[!] chunk_index must be an integer")
            return
        if not 0 <= chunk_index <= 0xFFFFFFFF:
            print("[!] chunk index must be 0..4294967295")
            return
        payload = struct.pack(protocol.DAT_GET_TC_FMT, chunk_index)
        resp = self.link.txrx(0x03, 0x02, payload, timeout_s=self.timeout)
        if resp is not None:
            shown = resp[:32].hex(" ")
            more = " ..." if len(resp) > 32 else ""
            print(f"    chunk data ({len(resp)} bytes): {shown}{more}")

    def do_dat_download(self, arg):
        "dat_download -- Download every chunk of the selected file, verify CRC-32, save to CWD"
        if not self.selection:
            print("[!] no file selected -- run dat_sel first")
            return

        sel = self.selection
        print(f"    downloading '{sel['name']}' ({sel['size']} bytes, "
              f"{sel['chunk_count']} chunks of {sel['chunk_size']})...")

        data = bytearray()
        for chunk_index in range(sel["chunk_count"]):
            payload = struct.pack(protocol.DAT_GET_TC_FMT, chunk_index)
            resp = self.link.txrx(0x03, 0x02, payload, timeout_s=self.timeout)
            if resp is None:
                print(f"[!] download aborted -- no reply for chunk {chunk_index}/{sel['chunk_count']}")
                return
            data += resp

        if len(data) != sel["size"]:
            print(f"[!] size mismatch: got {len(data)} bytes, expected {sel['size']}")

        # zlib.crc32() is the standard CRC-32/ISO-HDLC variant
        crc = zlib.crc32(bytes(data)) & 0xFFFFFFFF
        ok = (crc == sel["crc32"])
        print(f"    crc32 (computed) = 0x{crc:08X}  vs  0x{sel['crc32']:08X} (dat_sel)  "
              f"{'OK' if ok else 'MISMATCH'}")

        out_name = sel["name"] or f"dat_{sel['data_type']}_{sel['index']}.bin"
        out_path = os.path.join(os.getcwd(), out_name)
        with open(out_path, "wb") as f:
            f.write(data)
        print(f"    saved -> {out_path}  ({'CRC OK' if ok else 'CRC MISMATCH -- file may be corrupt'})")

    def do_dat_des(self, arg):
        "dat_des -- Release current file selection. TYPE=0x03 ADDR=0x03"
        self.link.txrx(0x03, 0x03, timeout_s=self.timeout)
        self.selection = None

    def do_dat_del(self, arg):
        "dat_del <type 0=housekeeping|1=images|2=logs> <index> -- Delete one file. TYPE=0x03 ADDR=0x04"
        parts = arg.split()
        if len(parts) != 2:
            print("usage: dat_del <data_type 0|1|2> <index>")
            return
        try:
            data_type, index = (int(x, 0) for x in parts)
        except ValueError:
            print("[!] arguments must be integers")
            return

        if not (0 <= data_type <= 2 and 0 <= index <= 0xFFFFFFFF):
            print("[!] type 0..2 and index 0..4294967295 required")
            return
        payload = struct.pack(protocol.DAT_DEL_TC_FMT, data_type, index)
        resp = self.link.txrx(0x03, 0x04, payload, timeout_s=self.timeout)
        self.selection = None
        if resp is None:
            return
        expected = struct.calcsize(protocol.DAT_LST_FMT)
        if len(resp) != expected:
            print(f"[!] unexpected payload length {len(resp)} (expected {expected})")
            return
        counts = struct.unpack(protocol.DAT_LST_FMT, resp)
        for name, val in zip(protocol.DAT_LST_FIELDS, counts):
            print(f"    {name:12s} = {val}")

    def do_dat_mrk(self, arg):
        "dat_mrk <mark 0-254> -- ICD name for dat_mark"
        return self.do_dat_mark(arg)

    def do_dat_mark(self, arg):
        "dat_mark <mark 0-254> -- Tag the file selected by dat_sel. TYPE=0x03 ADDR=0x05"
        parts = arg.split()
        if len(parts) != 1:
            print("usage: dat_mark <mark 0-254>")
            return
        try:
            mark = int(parts[0], 0)
        except ValueError:
            print("[!] mark must be an integer")
            return
        if not 0 <= mark < protocol.DAT_MARK_RESERVED:
            print(f"[!] mark {protocol.DAT_MARK_RESERVED:#04x} is reserved (means 'no filter')")
            return

        payload = struct.pack(protocol.DAT_MARK_TC_FMT, mark)
        resp = self.link.txrx(0x03, 0x05, payload, timeout_s=self.timeout)
        if resp is not None and len(resp) == 4:
            (uptime,) = struct.unpack(protocol.PING_TM_FMT, resp)
            print(f"    marked = {mark}  (uptime = {uptime} s)")

    def do_shell(self, arg):
        "shell <command> -- Run a shell command"
        cmd_str = arg.strip()
        if len(cmd_str) >= 2 and cmd_str[0] == cmd_str[-1] and cmd_str[0] in ('"', "'"):
            cmd_str = cmd_str[1:-1]  # quoting is optional -- the rest of the line is already one arg
        if not cmd_str:
            print("usage: shell <command>  (e.g. shell ls -la)")
            return
        cmd = cmd_str.encode("utf-8", errors="replace")
        if len(cmd) > protocol.SHELL_MAX_CMD:
            print(f"[!] command too long ({len(cmd)} bytes, max {protocol.SHELL_MAX_CMD})")
            return

        resp = self.link.txrx(protocol.SHELL_TYPE, protocol.SHELL_ADDR, cmd, timeout_s=self.timeout)
        if resp is None:
            return
        hdr_len = struct.calcsize(protocol.SHELL_TM_HDR_FMT)
        if len(resp) < hdr_len:
            print(f"[!] unexpected payload length {len(resp)} (expected >= {hdr_len})")
            return

        exit_status, exit_code, truncated, out_len = struct.unpack(
            protocol.SHELL_TM_HDR_FMT, resp[:hdr_len])
        output = resp[hdr_len:hdr_len + out_len]

        status_name = protocol.SHELL_EXIT_STATUS_NAMES.get(exit_status, "unknown")
        print(output.decode("utf-8", errors="replace"), end="" if output.endswith(b"\n") else "\n")
        if truncated:
            print("    [...output truncated...]")
        print(f"    status = {status_name}  code = {exit_code}  bytes = {out_len}")

    def do_raw(self, arg):
        "raw <type_hex> <addr_hex> [payload bytes...] -- Send an arbitrary TC frame"
        parts = arg.split()
        if len(parts) < 2:
            print("usage: raw <type_hex> <addr_hex> [byte byte ...]  (e.g. raw 0x01 0x00)")
            return
        try:
            type_ = int(parts[0], 0)
            addr = int(parts[1], 0)
            payload = bytes(int(x, 0) for x in parts[2:])
        except ValueError:
            print("[!] arguments must be integers (hex OK, e.g. 0x01)")
            return
        self.link.txrx(type_, addr, payload, timeout_s=self.timeout)

    def do_debug_handoff(self, arg):
        "debug_handoff"
        confirm = arg.strip().lower()
        if confirm != "yes":
            print("Run 'debug_handoff yes' to actually send it.")
            return
        payload = b"DEBUG_HANDOFF_OK"
        self.link.txrx(0xFF, 0x00, payload, timeout_s=self.timeout)

    def do_verbose(self, arg):
        "verbose [on|off] -- toggle full frame/header/timing debug dump (no argument = show current)"
        arg = arg.strip().lower()
        if arg in ("on", "off"):
            self.link.verbose = (arg == "on")
        print(f"verbose = {'on' if self.link.verbose else 'off'}")

    def do_timeout(self, arg):
        "timeout [ms] -- set response wait timeout (default: 2000ms; no argument = show current)"
        arg = arg.strip()
        if arg:
            try:
                self.timeout = float(arg) / 1000.0
            except ValueError:
                print("[!] timeout must be a number (milliseconds)")
                return
        print(f"timeout = {self.timeout * 1000:.0f} ms")

    def do_quit(self, arg):
        "quit -- exit the REPL"
        return True

    do_exit = do_quit
    do_EOF = do_quit

    def emptyline(self):
        pass  # don't repeat the last command on blank Enter

    # -- categorized help --------------------------------------------------
    CATEGORIES = [
        ("TELEMETRY", ["ping", "tm_get", "tm_obc_get"]),
        ("SYSTEM", ["sys_info", "sys_rtc", "pdu_vout", "pwr_en", "pwr_stat", "sen_bme_cfg", "sen_imu_cfg", "sen_stat", "uvc_cfg", "uvc_ctl", "uvc_stat"]),
        ("DATA TRANSFER", ["dat_lst", "dat_sel", "dat_get", "dat_download", "dat_des", "dat_del", "dat_mark"]),
        ("SESSION", ["raw", "debug_handoff", "shell", "verbose", "timeout", "help", "quit"]),
    ]

    def _short_doc(self, name):
        """Plain description for the compact summary view. Docstring
        convention: "<name> [args] -- <Description>. TYPE=0xNN ADDR=0xNN"
        -- strips the leading usage prefix and the trailing TYPE/ADDR
        clause, leaving just the description. 'help <command>' still shows
        the full docstring, TYPE/ADDR included."""
        doc = (getattr(self, f"do_{name}").__doc__ or "").split("\n")[0]
        if " -- " in doc:
            doc = doc.split(" -- ", 1)[1]
        idx = doc.find(". TYPE=")
        if idx > 0:
            doc = doc[:idx]
        return doc[:1].upper() + doc[1:]

    def do_help(self, arg):
        "help [command] -- list commands by category, or show help for one command"
        if arg:
            super().do_help(arg)
            return

        name_w = max(len(n) for _, names in self.CATEGORIES for n in names)
        for title, names in self.CATEGORIES:
            print(title)
            for name in names:
                print(f"  {name:<{name_w}}  {self._short_doc(name)}")
            print()
        print("'help <command>' for full usage.")


def main():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    ap = argparse.ArgumentParser(description="BEE-DAL REPL")
    ap.add_argument("port", help="COM port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=921600, help="default: 921600")
    ap.add_argument("--verbose", action="store_true",
                     help="dump full TX/RX frames ")
    args = ap.parse_args()

    from .transport import READ_TICK_S
    try:
        ser = serial.Serial(
            args.port, args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=READ_TICK_S,
        )
    except serial.SerialException as e:
        print(f"[!] Failed to open {args.port}: {e}")
        sys.exit(1)

    print(f"Opened {args.port} @ {args.baud} 8N1"
          + ("  [verbose]" if args.verbose else ""))

    link = SerialLink(ser, verbose=args.verbose)
    repl = BeeDalRepl(link)
    try:
        repl.cmdloop()
    except KeyboardInterrupt:
        print()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
