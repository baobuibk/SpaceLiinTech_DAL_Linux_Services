#!/usr/bin/env python3
"""Sandbox for trying daltui against the real flight software without hardware.

Starts the host build of dal_obc_fsw (service/dal_obc_fsw/build-host) with the scripted
EXP and a fake PDU from tests/fsw_auto (EXP housekeeping via a fake lite-client, no UART poll), plus the optional RS-422 frame socket. Everything
lives in a temp directory with stubbed systemctl, so nothing touches the build host.

    make -C ../dal_obc_fsw build-host/dal_obc_fsw build-host/dal_flight_console
    python3 tools/fsw_rig.py            # prints the two socket paths, Ctrl-C to stop
    python3 tools/fsw_rig.py -v         # also print what the flight software logs
    python3 tools/fsw_rig.py -v --quiet-housekeeping  # hide routine telemetry polls
    python3 tools/fsw_rig.py --periodic-logs off  # hide routine CSP/PDU logs
    python3 tools/fsw_rig.py --periodic-logs on   # show routine CSP/PDU logs
    python3 tools/fsw_rig.py --exp-data-dir /tmp/dal-exp-sim  # keep EXP downloads
    ./target/release/daltui --console <CONSOLE> --rs422 <RS422> --compare

With -v the terminal shows, in time order:
    [fsw]  dal_obc_fsw -v output: RS-422 / frame-socket frames ([RX]/[TX]) and
           local console requests/responses ([CONSOLE] RX/TX).
    [EXP]  every command line the flight software sent to the EXP UART CLI
    [PDU]  every valid Modbus request the flight software sent to the PDU
    [CSP]  every lite-client call (RPARAM over CAN/CSP): ping, getall <table>, setval, run
"""
import argparse
import os
import re
import json
import struct
import zlib
import sys
import time
from pathlib import Path

FSW = Path(__file__).resolve().parents[2] / "dal_obc_fsw"
sys.path.insert(0, str(FSW / "tests"))
from fsw_auto import fixtures  # noqa: E402
from fsw_auto.fake_exp import ExpModel  # noqa: E402
from test_exp_csp import ICD_ROWS, IDENTITY_ROWS, INVENTORY_ROWS, THERMAL_ROWS, ROWS  # noqa: E402
from rig_pc import PcModel

parser = argparse.ArgumentParser(description="Run the host FSW with fake EXP/PDU devices")
parser.add_argument("-v", "--verbose", action="store_true", help="print FSW and device traffic")
parser.add_argument("--exp-data-dir", type=Path,
                    help="keep collected EXP objects in this absolute folder (default: rig temp directory)")
periodic = parser.add_mutually_exclusive_group()
periodic.add_argument("--periodic-logs", choices=("on", "off"),
                      help="show or hide routine CSP, PDU, and EXP polling logs (implies -v)")
periodic.add_argument("--quiet-housekeeping", action="store_true",
                      help="alias for --periodic-logs off")
args = parser.parse_args()
show_periodic = args.periodic_logs != "off" and not args.quiet_housekeeping
verbose = args.verbose or args.periodic_logs is not None or args.quiet_housekeeping
fsw_args = ("-v", "--quiet-housekeeping") if not show_periodic else (("-v",) if verbose else ())
fsw = fixtures.Fsw(fsw_args=fsw_args)
power_state = fsw.tmp / "exp_power_state"
thermal_state = fsw.tmp / "exp_thermal_state"
profile_state = fsw.tmp / "exp_profile_state"
pc_state = fsw.tmp / "exp_pc_state"
pump_type_state = fsw.tmp / "exp_pump_type"
data_state = fsw.tmp / "exp_data_state.json"


class RigExpModel(ExpModel):
    """Keep the UART power commands and CSP table 2 on the same EXP model."""

    def __init__(self):
        super().__init__()
        self.heater_duty = [0] * 4
        self.heater_until = [0.0] * 4
        self.ir_duty = [0, 0]
        self.ir_config = [dict(max_duty_pct=0, max_on_ms=0, pwm_hz=100000) for _ in range(2)]
        self.burn_config = [dict.fromkeys(("duty_pct", "burn_time_ms",
            "max_total_current_ma"), 0) for _ in range(2)]
        self.burn_armed = None
        self.burn_firing = None
        self.burn_last = ["UNKNOWN", "UNKNOWN"]
        self.burn_attempt = [0, 0]
        self.motor_max_sps = [100, 100]
        self.motor_drive_pct = [20, 20]
        self.motor_mode = ["MANUAL", "MANUAL"]
        self.motor_pwm_hz = [2000, 2000]
        self.motor_limits = [None, None]
        self.pc = PcModel()
        self.data_objects = {}
        self.data_published = 0
        self.data_acked = 0
        self.next_camera_object = 0
        self.publish_data()
        self.publish_pc()
        self.publish_pump_type()
        self.limit_active_level = {(bank, index): "LOW"
                                   for bank in ("pmu", "pml") for index in ("0", "1")}
        self.publish_power()
        self.publish_thermal()
        self.publish_profile()

    def publish_profile(self):
        import json
        temp = profile_state.with_suffix(".tmp")
        temp.write_text(json.dumps({"phase": self.thermal.heater_phase,
                                    "reason": self.thermal.heater_reason,
                                    "running": [int(("heater", i) in self.thermal.running) for i in range(4)],
                                    "profiles": self.thermal.heater}))
        os.replace(temp, profile_state)

    def publish_data(self):
        objects = [{k: v for k, v in item.items() if k != "bytes"}
                   for item in self.data_objects.values()]
        temp = data_state.with_suffix(".tmp")
        temp.write_text(json.dumps({"objects": objects, "published": self.data_published,
                                    "acked": self.data_acked}))
        os.replace(temp, data_state)

    def data_command(self, command):
        parts = command.split()
        try:
            if parts[0] == "data_ready_collect" and len(parts) == 2:
                item = self.data_objects.get(int(parts[1]))
                if not item:
                    return "DATA result=5"
                return (f"DATA boot={item['boot']} object={item['object']} type=3 "
                        f"source={item['source']} encoding={item['encoding']} width=32 height=24 "
                        f"length={item['length']} crc32=0x{item['crc32']:08X} "
                        f"run=0 seq={item['object']} ms=1000")
            if parts[0] == "data_read" and len(parts) == 5:
                boot, obj, offset, length = map(int, parts[1:])
                item = self.data_objects.get(obj)
                if not item or boot != item['boot'] or not 1 <= length <= 32 or offset + length > item['length']:
                    return "DATA result=5"
                return " ".join(f"{v:02X}" for v in item['bytes'][offset:offset + length])
            if parts[0] == "data_ready_done" and len(parts) == 5:
                boot, obj, length, crc = map(int, parts[1:])
                item = self.data_objects.get(obj)
                if not item or (boot, length, crc) != (item['boot'], item['length'], item['crc32']):
                    return "DATA result=6"
                self.data_objects.pop(obj)
                self.cameras[item['source']]['data_exist'] = False
                self.data_acked += 1
                self.publish_data()
                return "DATA result=0"
        except ValueError:
            return "DATA result=4"
        return None

    def publish_power(self):
        values = " ".join(str(int(self.power[name])) for name in
                          ("exp12v", "sln_hd", "motor"))
        temp = power_state.with_suffix(".tmp")
        temp.write_text(values + "\n")
        os.replace(temp, power_state)

    def publish_thermal(self):
        temp = thermal_state.with_suffix(".tmp")
        temp.write_text("".join(f"{duty} {until:.6f}\n" for duty, until in
                                zip(self.heater_duty + self.ir_duty,
                                    self.heater_until + [1e30 if d else 0.0 for d in self.ir_duty])))
        os.replace(temp, thermal_state)

    def publish_pc(self):
        temp = pc_state.with_suffix(".tmp")
        temp.write_text(str(self.pc.status_byte()) + "\n")
        os.replace(temp, pc_state)

    def publish_pump_type(self):
        temp = pump_type_state.with_suffix(".tmp")
        temp.write_text(str(self.act.pump_type) + "\n")
        os.replace(temp, pump_type_state)

    @staticmethod
    def burn_valid(c):
        return (1 <= c["duty_pct"] <= 100 and 50 <= c["burn_time_ms"] <= 1000 and
                1 <= c["max_total_current_ma"] <= 3000)

    def __call__(self, command):
        if command.startswith("data_"):
            result = self.data_command(command)
            if result is not None:
                return result
        pc_reply = self.pc.handle(command, self)
        if pc_reply is not None:
            self.publish_pc()
            return pc_reply
        match = re.fullmatch(r"motor5_mode_set ([01]) (manual|operation)", command)
        if match:
            if any(self.act.motor_state):
                return "ERROR BUSY request=1 position=0"
            self.motor_mode[int(match[1])] = match[2].upper()
            return "DONE request=1 position=0"
        match = re.fullmatch(r"motor5_cfg_set ([01]) max_speed_sps (\d+)", command)
        if match:
            motor, speed = int(match[1]), int(match[2])
            if not 1 <= speed <= 1000 or self.act.motor_state[motor]:
                return "ERROR REFUSED request=1 position=0"
            self.motor_max_sps[motor] = speed
            return "DONE request=1 position=0"
        match = re.fullmatch(r"motor5_cfg_set ([01]) drive_pct (\d+)", command)
        if match:
            motor, duty = int(match[1]), int(match[2])
            if not 1 <= duty <= 80 or any(self.act.motor_state):
                return "ERROR REFUSED request=1 position=0"
            self.motor_drive_pct[motor] = duty
            return "DONE request=1 position=0"
        match = re.fullmatch(r"motor5_cfg_get ([01])", command)
        if match:
            motor = int(match[1])
            return (f"MOTOR{motor} mode={self.motor_mode[motor]} STOPPED position=0 reference_valid=0\r\n"
                    f"max_speed_sps={self.motor_max_sps[motor]} accel_sps2=100 "
                    f"drive_pct={self.motor_drive_pct[motor]}")
        match = re.fullmatch(r"motor5_pwm_freq_set ([01]) (80|100|200|2000)", command)
        if match:
            motor = int(match[1])
            if not self.act.motor_init:
                return "ERROR NOT_READY request=1 position=0"
            self.motor_pwm_hz[motor] = int(match[2])
            return "DONE request=1 position=0"
        match = re.fullmatch(r"motor5_pwm_freq_get ([01])", command)
        if match:
            motor = int(match[1])
            return f"motor={motor} pwm_freq_hz={self.motor_pwm_hz[motor]} initialized={int(self.act.motor_init)} asleep=0"
        match = re.fullmatch(r"motor5_limits_set ([01]) (pmu|pml) ([01]) ([01]) (min|max)", command)
        if match:
            motor, bank, minimum, maximum, home = int(match[1]), match[2], match[3], match[4], match[5]
            other = self.motor_limits[1 - motor]
            if minimum == maximum or self.act.motor_state[motor] or (other and other[0] == bank):
                return "ERROR REFUSED request=1 position=0"
            self.motor_limits[motor] = (bank, minimum, maximum, home)
            return "DONE request=1 position=0"
        match = re.fullmatch(r"limit_cfg_set ([01]) (min|max) active_level (low|high)", command)
        if match:
            motor, role = int(match[1]), match[2]
            mapping = self.motor_limits[motor]
            if mapping is None:
                return "ERROR REFUSED request=1 position=0"
            bank, minimum, maximum, _ = mapping
            self.limit_active_level[(bank, minimum if role == "min" else maximum)] = match[3].upper()
            return "DONE request=1 position=0"
        match = re.fullmatch(r"motor5_limit_get ([01])", command)
        if match:
            motor = int(match[1])
            mapping = self.motor_limits[motor]
            if mapping is None:
                return f"MOTOR{motor} home=none\r\nMOTOR{motor} min=none\r\nMOTOR{motor} max=none"
            bank, minimum, maximum, home = mapping
            end = minimum if home == "min" else maximum
            levels = self.limit_active_level
            return (f"MOTOR{motor} home={bank}{end} Light-ON active_{levels[(bank, end)]}\r\n"
                    f"MOTOR{motor} min={bank}{minimum} Light-ON active_{levels[(bank, minimum)]}\r\n"
                    f"MOTOR{motor} max={bank}{maximum} Light-ON active_{levels[(bank, maximum)]}")
        match = re.fullmatch(r"motor5_home ([01])", command)
        if match and self.motor_limits[int(match[1])] is None:
            return "ERROR REFUSED request=1 position=0"
        match = re.fullmatch(r"motor5_run ([01]) (cw|ccw) (\d+)", command)
        if match and int(match[3]) > self.motor_max_sps[int(match[1])]:
            return "ERROR REFUSED request=1 position=0"
        match = re.fullmatch(r"motor5_move ([01]) (-?\d+) (\d+)", command)
        if match and int(match[3]) > self.motor_max_sps[int(match[1])]:
            return "ERROR REFUSED request=1 position=0"
        if match and self.motor_mode[int(match[1])] == "OPERATION" and self.motor_limits[int(match[1])] is None:
            return "ERROR REFUSED request=1 position=0"
        match = re.fullmatch(r"ir_cfg_set ([01]) (max_duty_pct|max_on_ms|pwm_hz) (\d+)", command)
        if match:
            channel, key, value = int(match[1]), match[2], int(match[3])
            limits = {"max_duty_pct": (0, 25), "max_on_ms": (0, 100), "pwm_hz": (100, 100000)}
            lo, hi = limits[key]
            if not lo <= value <= hi or self.ir_duty[channel]:
                return "IR REFUSED id=0"
            self.ir_config[channel][key] = value
            return "IR QUEUED id=1"
        if command == "ir_status":
            lines = []
            for i, c in enumerate(self.ir_config):
                lines.append(f"IR{i} active={int(bool(self.ir_duty[i]))} duty_pct={self.ir_duty[i]} "
                             f"remaining_ms=0 pwm_hz={c['pwm_hz']} max_duty_pct={c['max_duty_pct']} "
                             f"max_on_ms={c['max_on_ms']}")
            return "\r\n".join(lines)
        match = re.fullmatch(r"ir_set ([01]) (\d+)", command)
        if match:
            channel, duty = int(match[1]), int(match[2])
            c = self.ir_config[channel]
            if (not self.power["exp12v"] or not 1 <= duty <= c["max_duty_pct"] or
                    not c["max_on_ms"] or self.ir_duty[1-channel]):
                return "IR REFUSED id=0"
            self.ir_duty[channel] = duty
            self.act.ir_duty[channel] = duty
            self.publish_thermal()
            return "IR QUEUED id=2"
        match = re.fullmatch(r"ir_off ([01]|all)", command)
        if match:
            for channel in (range(2) if match[1] == "all" else (int(match[1]),)):
                self.ir_duty[channel] = self.act.ir_duty[channel] = 0
            self.publish_thermal()
            return "IR STOP_REQUESTED sequence=3"
        match = re.fullmatch(r"burn_cfg_set ([01]) (\w+) (\d+)", command)
        if match:
            wire, key, value = int(match[1]), match[2], int(match[3])
            if key not in self.burn_config[wire] or self.burn_armed is not None:
                return "ERROR: invalid or busy"
            self.burn_config[wire][key] = value
            return "OK persisted"
        if command == "burn_status":
            now = time.monotonic()
            if self.burn_armed and now >= self.burn_armed[1]:
                self.burn_armed = None
            if self.burn_firing and now >= self.burn_firing[1]:
                self.burn_last[self.burn_firing[0]] = "COMPLETED"
                self.burn_firing = None
            active = self.burn_firing or self.burn_armed
            state = "FIRING" if self.burn_firing else "ARMED" if self.burn_armed else "IDLE"
            lines = [f"burn state={state} active_wire={active[0] if active else -1}"]
            for i, c in enumerate(self.burn_config):
                fields = " ".join(f"{key}={value}" for key, value in c.items())
                lines.append(f"wire{i} configured={int(self.burn_valid(c))} {fields}")
                lines.append(f"wire{i} last={self.burn_last[i]} attempt={self.burn_attempt[i]} peak_delta_ma=0")
            return "\r\n".join(lines)
        match = re.fullmatch(r"burn_arm ([01])", command)
        if match:
            wire = int(match[1])
            if not self.power["motor"] or not self.burn_valid(self.burn_config[wire]) or self.burn_armed or self.burn_firing:
                return "ERROR: motor busy, rail/sensor unavailable, configuration incomplete"
            self.burn_armed = (wire, time.monotonic() + 5.0)
            return "OK armed for 5 s"
        if command == "burn_fire":
            if not self.burn_armed or time.monotonic() >= self.burn_armed[1]:
                return "ERROR: not armed, no new healthy PAC sample, or rail unavailable"
            wire = self.burn_armed[0]
            self.burn_armed = None
            self.burn_firing = (wire, time.monotonic() + self.burn_config[wire]["burn_time_ms"] / 1000)
            self.burn_attempt[wire] += 1
            self.burn_last[wire] = "IN_PROGRESS"
            return "OK firing"
        if command == "burn_abort":
            if self.burn_firing:
                self.burn_last[self.burn_firing[0]] = "ABORTED"
            self.burn_armed = None
            self.burn_firing = None
            return "OK both outputs off"
        match = re.fullmatch(r"heater_set ([0-3]) (\d{1,3}) (\d{1,6})", command)
        if match:
            channel, duty, duration = map(int, match.groups())
            if duty > 100 or not 1 <= duration <= 600000:
                return "Error: heater duty or duration out of range"
            if duty and not self.power["exp12v"]:
                return "Heater request REFUSED: EXP 12 V is OFF"
            self.heater_duty[channel] = duty
            self.heater_until[channel] = time.monotonic() + duration / 1000 if duty else 0.0
            self.publish_thermal()
            return f"--> OK: Heater[{channel}] Duty = {duty}% (requested {duty}%)"
        match = re.fullmatch(r"heater_stop (all|[0-3])", command)
        if match:
            channels = range(4) if match[1] == "all" else (int(match[1]),)
            for channel in channels:
                self.heater_duty[channel] = 0
                self.heater_until[channel] = 0.0
            self.publish_thermal()
            return "--> OK: heater stopped"
        reply = super().__call__(command)
        match = re.fullmatch(r"camera_capture ([01])", command)
        if match and "result=QUEUED" in reply:
            camera = int(match[1])
            state = self.cameras[camera]
            self.next_camera_object += 1
            state['object'] = self.next_camera_object
            raw = (struct.pack('<h', 1250) if state['format'] == 'int16'
                   else struct.pack('<f', 25.0)) * (32 * 24)
            obj = state['object']
            self.data_objects[obj] = dict(boot=1, object=obj, source=camera,
                                          encoding=1 if state['format'] == 'int16' else 2,
                                          length=len(raw), crc32=zlib.crc32(raw), bytes=raw)
            self.data_published += 1
            self.publish_data()
        if command.startswith(("thp_wire_cfg ", "thp_wire_ctl ")):
            self.publish_profile()
        if command.startswith("pump_type_set "):
            self.publish_pump_type()
        if command.startswith("power_"):
            self.publish_power()
            if not self.power["exp12v"] and any(self.heater_duty):
                self.heater_duty = [0] * 4
                self.heater_until = [0.0] * 4
                self.publish_thermal()
            if not self.power["exp12v"] and any(self.ir_duty):
                self.ir_duty = [0, 0]
                self.act.ir_duty = [0, 0]
                self.publish_thermal()
        return reply


fsw.exp_responder = RigExpModel()
sock = fsw.tmp / "rs422.sock"
fsw.extra_env["DAL_OBC_RS422_SOCKET"] = str(sock)
fsw.extra_env["DAL_OBC_RTC_SIMULATE"] = "1"  # sys_rtc uses an in-process UTC offset; never set the host clock
# CAMERA_LED belongs to the board's PWM3. Give the sandbox a writable fake
# pwmN directory so the UI can exercise its console helper without host sysfs.
pwm = fsw.tmp / "pwm3"
pwm.mkdir()
for name, value in (("period", "1000000"), ("duty_cycle", "0"), ("enable", "0")):
    (pwm / name).write_text(value)
fsw.extra_env["DAL_GUI_CAMERA_LED_PWM"] = str(pwm)
# MIPI route and capture use the same FSW path as the board. Replace only the
# sysfs switches and camera tools with files inside this disposable sandbox.
mipi_data = fsw.tmp / "mipi_images"
lane = fsw.tmp / "mipi_lane"
sensor = fsw.tmp / "mipi_sensor"
lane.write_text("0")
sensor.write_text("0")
fsw.extra_env.update({
    "DAL_GUI_MIPI_LANE": str(lane),
    "DAL_GUI_MIPI_SENSOR": str(sensor),
    "DAL_GUI_MIPI_LOCK": str(fsw.tmp / "mipi.lock"),
    "DAL_GUI_MIPI_DATA_DIR": str(mipi_data),
})
capture = fsw.tmp / "fake_mipi_capture.sh"
capture.write_text(
    "#!/bin/sh\n"
    "truncate -s 39321600 \"$1\"\n"
)
capture.chmod(0o755)
fsw.extra_env["DAL_GUI_MIPI_CAPTURE"] = str(capture)
converter = fsw.tmp / "fake_raw_png.py"
converter.write_text(
    "#!/usr/bin/env python3\n"
    "import pathlib, struct, sys, zlib\n"
    "def chunk(kind, data):\n"
    "    return struct.pack('>I', len(data)) + kind + data + "
    "struct.pack('>I', zlib.crc32(kind + data))\n"
    "def write_png(path, width, height):\n"
    "    compressor = zlib.compressobj()\n"
    "    row = bytes(width + 1)  # filter byte plus black grayscale pixels\n"
    "    image = bytearray()\n"
    "    for _ in range(height): image.extend(compressor.compress(row))\n"
    "    image.extend(compressor.flush())\n"
    "    ihdr = struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)\n"
    "    pathlib.Path(path).write_bytes(bytes.fromhex('89504e470d0a1a0a') + "
    "chunk(b'IHDR', ihdr) + chunk(b'IDAT', image) + chunk(b'IEND', b''))\n"
    "base = pathlib.Path(sys.argv[sys.argv.index('-o') + 1]).with_suffix('')\n"
    "write_png(str(base) + '_high.png', 5120, 3840)\n"
    "write_png(str(base) + '_low.png', 1280, 960)\n"
)
converter.chmod(0o755)
fsw.extra_env["DAL_GUI_MIPI_PNG"] = str(converter)
# EXP housekeeping comes from RPARAM over CAN/CSP, as on the board; the UART tlm_json poll is off.
# There is no can0 here, so lite-client serves the table 0 / 2 / 3 / 6 / 12 test fixtures
# (the same one tests/test_exp_csp.py uses) and records every call in csp_calls.
csp_calls = fsw.tmp / "csp_calls"
client = fsw.tmp / "fake-lite-client"
client.write_text(
    "#!/usr/bin/env python3\n"
    "import sys, time, json\n"
    f"ROWS = {ROWS!r}\n"
    f"IDENTITY_ROWS = {IDENTITY_ROWS!r}\n"
    f"INVENTORY_ROWS = {INVENTORY_ROWS!r}\n"
    f"ICD_ROWS = {ICD_ROWS!r}\n"
    f"THERMAL_ROWS = {THERMAL_ROWS!r}\n"
    f"CALLS = {str(csp_calls)!r}\n"
    f"POWER_STATE = {str(power_state)!r}\n"
    f"THERMAL_STATE = {str(thermal_state)!r}\n"
    f"PROFILE_STATE = {str(profile_state)!r}\n"
    f"PC_STATE = {str(pc_state)!r}\n"
    f"PUMP_TYPE_STATE = {str(pump_type_state)!r}\n"
    f"DATA_STATE = {str(data_state)!r}\n"
    "def sensor_rows():\n"
    "    try: exp12v, sln, motor = map(int, open(POWER_STATE).read().split())\n"
    "    except (OSError, ValueError): exp12v, sln, motor = 0, 0, 0\n"
    "    rows = ROWS.splitlines()\n"
    "    updates = {\n"
    "        'voltage_mV': [12000 * motor, 12100 * exp12v, 5000, 5100 * sln],\n"
    "        'current_mA': [15 * motor, 20 * exp12v, 100, 60 * sln],\n"
    "        'power_mW': [180 * motor, 242 * exp12v, 500, 306 * sln],\n"
    "        'rail_enabled': [exp12v, sln, motor],\n"
    "    }\n"
    "    for i, row in enumerate(rows):\n"
    "        parts = row.split()\n"
    "        if len(parts) > 2 and parts[1] in updates:\n"
    "            rows[i] = ' '.join(parts[:3] + [str(v) for v in updates[parts[1]]])\n"
    "    return '\\n'.join(rows)\n"
    "def inventory_rows():\n"
    "    try: state = json.load(open(DATA_STATE))\n"
    "    except (OSError, ValueError): state = {}\n"
    "    objects = sorted(state.get('objects', []), key=lambda o: o['object'])\n"
    "    item = objects[0] if objects else {}\n"
    "    tick = int(time.monotonic() * 1000) & 0xffffffff\n"
    "    counts = [sum(o['source'] == i for o in objects) for i in (0, 1)]\n"
    "    updates = {'seq': [tick], 'tick_ms': [tick], 'boot_id': [1],\n"
    "      'ready': [int(bool(objects))], 'result': [0], 'pending': [len(objects)],\n"
    "      'published': [state.get('published', 0)], 'acknowledged': [state.get('acked', 0)],\n"
    "      'type_count': [0, 0, len(objects)], 'object_id': [item.get('object', 0)],\n"
    "      'length': [item.get('length', 0)], 'crc32': [item.get('crc32', 0)],\n"
    "      'type': [3 if item else 0], 'source': [item.get('source', 0)],\n"
    "      'encoding': [item.get('encoding', 0)], 'width': [32 if item else 0],\n"
    "      'height': [24 if item else 0], 'run_id': [0],\n"
    "      'object_seq': [item.get('object', 0)], 'created_ms': [1000 if item else 0],\n"
    "      'image_count': counts}\n"
    "    rows = []\n"
    "    for row in INVENTORY_ROWS.splitlines():\n"
    "        parts = row.split()\n"
    "        if len(parts) > 2 and parts[1] in updates:\n"
    "            row = ' '.join(parts[:3] + [str(v) for v in updates[parts[1]]])\n"
    "        rows.append(row)\n"
    "    return '\\n'.join(rows)\n"
    "def thermal_rows():\n"
    "    rows = THERMAL_ROWS.splitlines()\n"
    "    tick = int(time.monotonic() * 1000) & 0xffffffff\n"
    "    try: state = json.load(open(PROFILE_STATE))\n"
    "    except (OSError, ValueError): state = {}\n"
    "    try: manual = [int(line.split()[0]) if time.monotonic() < float(line.split()[1]) else 0 for line in open(THERMAL_STATE).read().splitlines()[:4]]\n"
    "    except (OSError, ValueError): manual = [0]*4\n"
    "    phase = state.get('phase', [0]*4)\n"
    "    running = state.get('running', [0]*4)\n"
    "    profiles = state.get('profiles', [{} for _ in range(4)])\n"
    "    updates = {\n"
    "      'seq': [tick], 'tick_ms': [tick],\n"
    "      'state': [5 if running[i] else 8 if phase[i] == 2 else 0 for i in range(4)],\n"
    "      'enabled': running, 'fault': state.get('reason', [0]*4),\n"
    "      'configured': [1]*4, 'sensors_valid': [1]*4,\n"
    "      'main_ntc': [p.get('main', 0) for p in profiles],\n"
    "      'sec_ntc': [p.get('sec', 1) for p in profiles],\n"
    "      'setpoint_mC': [p.get('setpoint', 3000)*10 for p in profiles],\n"
    "      'request_pct': [20 if running[i] else manual[i] for i in range(4)],\n"
    "      'applied_pct': [20 if running[i] else manual[i] for i in range(4)],\n"
    "      'owner_kind': [2 if running[i] else 1 if manual[i] else 0 for i in range(4)],\n"
    "      'owner_profile': [i if running[i] else 255 for i in range(4)],\n"
    "      'pid_demand': [20.0 if running[i] else 0.0 for i in range(4)],\n"
    "      'output_ms': [tick]*4,\n"
    "    }\n"
    "    for i, row in enumerate(rows):\n"
    "        parts = row.split()\n"
    "        if len(parts)>2 and parts[1] in updates: rows[i] = ' '.join(parts[:3] + [str(v) for v in updates[parts[1]]])\n"
    "    return '\\n'.join(rows)\n"
    "def icd_rows():\n"
    "    rows = ICD_ROWS.splitlines()\n"
    "    tick = int(time.monotonic() * 1000) & 0xffffffff\n"
    "    heater = [0] * 6\n"
    "    try:\n"
    "        for i, line in enumerate(open(THERMAL_STATE)):\n"
    "            duty, until = line.split()\n"
    "            if i < 6 and time.monotonic() < float(until):\n"
    "                heater[i] = int(duty) | 0x80\n"
    "    except (OSError, ValueError): pass\n"
    "    for i, row in enumerate(rows):\n"
    "        if row.startswith('0x0002 seq '): rows[i] = f'0x0002 seq U32 {tick}'\n"
    "        elif row.startswith('0x0006 tick_ms '): rows[i] = f'0x0006 tick_ms U32 {tick}'\n"
    "        elif row.startswith('0x0010 group_tick_ms '): rows[i] = '0x0010 group_tick_ms U32 ' + ' '.join([str(tick)] * 4)\n"
    "        elif row.startswith('0x0020 thm_stat '): rows[i] = '0x0020 thm_stat U8 ' + ' '.join(map(str, heater))\n"
    "        elif row.startswith('0x0046 pc_stat '):\n"
    "            try: pc = int(open(PC_STATE).read())\n"
    "            except (OSError, ValueError): pc = 0\n"
    "            rows[i] = f'0x0046 pc_stat U8 {pc}'\n"
    "        elif row.startswith('0x0026 fld_stat '):\n"
    "            try: pump_type = int(open(PUMP_TYPE_STATE).read())\n"
    "            except (OSError, ValueError): pump_type = 0\n"
    "            parts = row.split()\n"
    "            parts[6] = str(pump_type)\n"
    "            rows[i] = ' '.join(parts)\n"
    "    return '\\n'.join(rows)\n"
    "verb = sys.argv[2]\n"
    "open(CALLS, 'a').write(' '.join(sys.argv[2:]) + '\\n')\n"
    "if verb == 'ping': print('reply in 1 ms')\n"
    "elif verb == 'getall':\n"
    "    table = sys.argv[4]\n"
    "    if table == '0': print(IDENTITY_ROWS)\n"
    "    elif table == '2': print(sensor_rows())\n"
    "    elif table == '3': print(thermal_rows())\n"
    "    elif table == '6': print(inventory_rows())\n"
    "    elif table == '12':\n"
    "        print(icd_rows())\n"
    "    else: sys.exit(1)\n"
    "elif verb == 'setval': print('set accepted')\n"
    "elif verb == 'getval': print('value 128')\n"
    "elif verb == 'run': print('command accepted')\n"
    "else: sys.exit(1)\n")
client.chmod(0o755)
fsw.extra_env["DAL_EXP_UART_POLL"] = "0"
fsw.extra_env["DAL_EXP_CSP_ENABLE"] = "1"
fsw.extra_env["DAL_EXP_CSP_CLIENT"] = str(client)
exp_data_dir = args.exp_data_dir.expanduser().resolve() if args.exp_data_dir else fsw.tmp / "data"
fsw.extra_env["DAL_DATA_DIR"] = str(exp_data_dir)
fsw.extra_env["DAL_EXP_DATA_ENABLE"] = "1"
fsw.extra_env["DAL_EXP_DATA_DIR"] = str(exp_data_dir)
fsw.start()
print("CONSOLE", fsw.socket_path, flush=True)
print("RS422", sock, flush=True)
print("MIPI_DATA", mipi_data, flush=True)
print("EXP_DATA", exp_data_dir / "images/exp", flush=True)

seen = {"log": 0, "exp": 0, "pdu": 0, "csp": 0}


def csp_lines():
    """Calls the flight software made to lite-client (RPARAM over CAN/CSP)."""
    try:
        return csp_calls.read_text().splitlines()
    except OSError:
        return []


def show():
    """Print whatever the three sources have added since the last call."""
    for key, label, items in (
        ("log", "fsw", fsw.log),
        ("exp", "EXP", fsw.exp.commands),
        ("pdu", "PDU", fsw.pdu.requests),
        ("csp", "CSP", csp_lines()),
    ):
        items = list(items)
        for item in items[seen[key]:]:
            if not show_periodic:
                if key == "exp" and (item in {"tlm_json system", "tlm_json sensors",
                                                  "tlm_json power"} or item.startswith("json_chunk ")):
                    continue
                if key == "pdu" and isinstance(item, tuple) and item[0] in (1, 2, 3, 4):
                    continue
                if key == "csp" and item.split()[0] in ("ping", "getall"):
                    continue
            print(f"{time.strftime('%H:%M:%S')} [{label}] {item}", flush=True)
        seen[key] = len(items)


try:
    while fsw.alive():
        fsw.write_snapshot()  # the HK collector is off in the sandbox; keep the snapshot fresh
        fsw.exp_responder.publish_pc()  # advance PC run completion in the fake RPARAM table
        for _ in range(10):
            if verbose:
                show()
            time.sleep(0.1)
except KeyboardInterrupt:
    pass
finally:
    fsw.stop()
