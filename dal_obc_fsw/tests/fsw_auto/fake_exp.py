"""Scripted EXP UART0 CLI (DAL_EXP firmware) for the commands dal_obc_fsw uses.

Reply formats are copied from the firmware source in service/ref (cli_power.c, cli_command.c,
cli_pc_exp.c, cli_temperature.c); test_oracle.py re-checks every literal against that source.
"""
import math
import re
import struct

from fsw_auto.fake_actuators import Actuators


def _f32(b):
    return struct.unpack("<f", bytes(b))[0]


class ThermalProfiles:
    """`thp_wire_cfg <64 hex>`: heater PID profiles 0-3 and IR LED PID profiles 0-1.

    Validation mirrors cli_temperature.c (CMD_THP_Wire_Cfg), profile_config_valid() in
    profile_state.c and valid() in app_ir_temperature.c.
    """
    IR_HARD_MAX_DUTY = 25

    def __init__(self):
        self.heater = [dict(main=0, sec=1, setpoint=3000, delta=500, cutoff=8000, timeout=5000,
                            resume=0, up=10, down=10, kp=1.5, ki=0.25, kd=0.0) for _ in range(4)]
        self.ir = [None, None]           # unconfigured until set
        self.running = set()             # {("heater", i), ("ir", i)}: configuration refused
        self.heater_phase = [0] * 4      # 0 disabled, 1 SOAK, 2 ERROR (profile_state)
        self.heater_reason = [0] * 4
        self.ir_phase = [0, 0]           # 0 IDLE 1 STARTING 2 WAIT_SAMPLE 3 RUNNING 4 FAULT
        self.ir_reason = [0, 0]
        self.sensor_fault = set()        # heater profiles whose start fails the sensor check

    @staticmethod
    def _rec_heater(p, h):
        struct.pack_into("<BBhhhHBBB", p, 3, h["main"], h["sec"], h["setpoint"], h["delta"],
                         h["cutoff"], h["timeout"], h["resume"], h["up"], h["down"])
        struct.pack_into("<fff", p, 16, h["kp"], h["ki"], h["kd"])

    def handle(self, hexarg):
        if len(hexarg) != 64 or not re.fullmatch(r"[0-9a-fA-F]{64}", hexarg):
            return "ERROR THP_CFG invalid record"
        p = bytearray.fromhex(hexarg)
        action, kind, idx = p[0], p[1], p[2]
        if action > 1 or kind > 1 or idx >= (2 if kind else 4):
            return "ERROR THP_CFG invalid record"
        if action == 0 and any(p[3:]):
            return "ERROR THP_CFG invalid record"
        if action == 1 and kind == 0 and any(p[28:]):
            return "ERROR THP_CFG invalid record"
        if action == 1:
            if (("ir" if kind else "heater"), idx) in self.running:
                return "ERROR THP_CFG refused"
            if kind == 0:
                main, sec = p[3], p[4]
                sp, delta, cut, timeout = struct.unpack_from("<hhhH", p, 5)
                resume, up, down = p[13], p[14], p[15]
                kp, ki, kd = struct.unpack_from("<fff", p, 16)
                ok = (main < 4 and sec < 4 and delta > 0 and cut > sp and 0 < cut <= 20000 and
                      sp >= -10000 and 0 < timeout <= 60000 and resume <= 1 and
                      1 <= up <= 100 and 1 <= down <= 100 and
                      all(math.isfinite(g) and g >= 0 for g in (kp, ki, kd)))
                if not ok:
                    return "ERROR THP_CFG refused"
                self.heater[idx] = dict(main=main, sec=sec, setpoint=sp, delta=delta, cutoff=cut,
                                        timeout=timeout, resume=resume, up=up, down=down,
                                        kp=kp, ki=ki, kd=kd)
            else:
                cam, x, y, w, h = p[3:8]
                target, cutoff, rise, period, timeout = struct.unpack_from("<hhhHH", p, 8)
                duty, slew = p[18], p[19]
                kp, ki, kd = struct.unpack_from("<fff", p, 20)
                ok = (cam < 2 and x < 32 and y < 24 and w and h and x + w <= 32 and y + h <= 24 and
                      target < cutoff and rise > 0 and 0 < duty <= self.IR_HARD_MAX_DUTY and
                      0 < slew <= 100 and 250 <= period <= 10000 and period < timeout <= 60000 and
                      all(math.isfinite(g) and g >= 0 for g in (kp, ki, kd)))
                if not ok:
                    return "ERROR THP_CFG refused"
                self.ir[idx] = bytes(p[3:])
        out = bytearray(32)
        out[0:3] = p[0:3]
        if kind == 0:
            self._rec_heater(out, self.heater[idx])
        else:
            if self.ir[idx] is None:
                return "ERROR THP_CFG refused"      # get of an unconfigured IR profile
            out[3:] = self.ir[idx]
        return "THP_CFG " + out.hex()


    def ctl(self, hexarg, exp12v_on=True):
        """`thp_wire_ctl <2 hex>` (CMD_THP_Wire_Ctl, app_temperature.c, app_ir_temperature.c)."""
        if len(hexarg) != 2 or not re.fullmatch(r"[0-9a-fA-F]{2}", hexarg):
            return "ERROR THP_CTL invalid selector"
        sel = int(hexarg, 16)
        ch, kind, action = sel & 3, (sel >> 2) & 1, (sel >> 3) & 3
        if sel & 0xE0 or (kind and (ch > 1 or action == 3)):
            return "ERROR THP_CTL invalid selector"
        if not kind:
            if action == 2:                                   # stop: always succeeds
                self.heater_phase[ch], self.heater_reason[ch] = 0, 0
                self.running.discard(("heater", ch))
            elif action in (1, 3):
                if action == 3 and self.heater_phase[ch] != 2:
                    return "ERROR THP_CTL refused"           # resume only from ERROR
                if not exp12v_on or ch in self.sensor_fault:
                    return "ERROR THP_CTL refused"
                self.heater_phase[ch], self.heater_reason[ch] = 1, 0
                self.running.add(("heater", ch))
            phase, reason, configured = self.heater_phase[ch], self.heater_reason[ch], 1
            run = phase == 1
        else:
            if action == 1:
                idle = (0, 4)
                if (self.ir[ch] is None or self.ir_phase[ch] not in idle or
                        self.ir_phase[1 - ch] not in idle):
                    return "ERROR THP_CTL refused"
                self.ir_phase[ch], self.ir_reason[ch] = 1, 0
                self.running.add(("ir", ch))
            elif action == 2:
                if self.ir_phase[ch] in (0, 4):
                    return "ERROR THP_CTL refused"
                self.ir_phase[ch], self.ir_reason[ch] = 0, 0
                self.running.discard(("ir", ch))
            phase, reason, configured = self.ir_phase[ch], self.ir_reason[ch], int(self.ir[ch] is not None)
            run = 1 <= phase <= 3
        return "THP_CTL %02x" % (phase | reason << 3 | configured << 6 | int(run) << 7)


class ExpModel:
    """Stateful responder: pass an instance as Fsw(exp_responder=...). Every call is a CLI line."""

    LABELS = {"exp12v": "12V_EXP", "sln_hd": "SLN HD", "motor": "MOTOR"}

    def __init__(self, version="DAL_EXP firmware v1.2.3 (hw110) build 4dc7bbb-dirty"):
        self.version_line = version        # None: `version` prints nothing useful (EXP offline)
        self.power = {k: False for k in self.LABELS}
        self.refuse_normal_off = set()     # lines that need `force` to switch off
        self.extra = {}                    # command -> reply, checked first
        self.thermal = ThermalProfiles()
        self.act = Actuators()
        self.cameras = [
            dict(model="mlx90642", address=0x66, format="float32", hz=8,
                 emissivity=1000, initialized=False, buffer=False,
                 request=0, object=0, result="OK", data_exist=False,
                 restart_required=False)
            for _ in range(2)
        ]

    def __call__(self, command):
        if command in self.extra:
            return self.extra[command]
        if command == "version":
            return self.version_line or "ERROR"
        if command.startswith("camera_"):
            return self.camera_command(command)
        if command.startswith("thp_wire_cfg "):
            return self.thermal.handle(command.split(" ", 1)[1])
        if command.startswith("thp_wire_ctl "):
            return self.thermal.ctl(command.split(" ", 1)[1], self.power["exp12v"])
        reply = self.act.handle(command, self)
        if reply is not None:
            return reply
        if command == "power_all_get":
            lines = []
            for seq, (k, on) in enumerate(self.power.items(), 1):   # cli_power.c print_status()
                lines.append(f"Power {self.LABELS[k]} requested={'ON' if on else 'OFF'} "
                             f"observed=UNKNOWN phase=IDLE last=OK seq={seq}")
                lines.append("  owners=0 epoch=1 forced_off=0")
            return "\r\n".join(lines)
        m = re.fullmatch(r"power_all_(on|off)( force)?", command)
        if m:
            state, force = m.group(1) == "on", bool(m.group(2))
            results = []
            for line in self.LABELS:
                if not state and line in self.refuse_normal_off and not force:
                    results.append(f"Power {self.LABELS[line]} OFF: REFUSED")
                else:
                    self.power[line] = state
                    results.append(f"Power {self.LABELS[line]} {'ON' if state else 'OFF'}: OK")
            return "\r\n".join(results)
        m = re.fullmatch(r"power_(exp12v|sln_hd|motor)_(on|off)( force)?", command)
        if m:
            line, state, force = m.group(1), m.group(2) == "on", bool(m.group(3))
            if not state and line in self.refuse_normal_off and not force:
                return f"Power {self.LABELS[line]} OFF: REFUSED"
            self.power[line] = state
            return f"Power {self.LABELS[line]} {'ON' if state else 'OFF'}: OK"
        return "OK"

    def camera_command(self, command):
        parts = command.split()
        name = parts[0]
        try:
            i = int(parts[1])
            if i not in (0, 1):
                return "ERROR CAMERA INVALID"
            c = self.cameras[i]
            if name == "camera_config" and len(parts) == 7:
                model, address, fmt, hz, emiss = parts[2:]
                address, hz, emiss = int(address, 0), int(hz), int(emiss)
                if (model not in ("mlx90640", "mlx90642") or fmt not in ("int16", "float32") or
                        not 8 <= address <= 0x77 or hz not in (2, 4, 8, 16) or
                        not 100 <= emiss <= 1000):
                    return "ERROR CAMERA INVALID"
                c.update(model=model, address=address, format=fmt, hz=hz,
                         emissivity=emiss, initialized=False)
                return "OK"
            if name == "camera_buffer_enable" and len(parts) == 2:
                c["buffer"] = True
                return f"CAMERA buffer={i} ENABLED result=0"
            if name == "camera_buffer_disable" and len(parts) == 2:
                c["buffer"] = False
                return f"CAMERA buffer={i} DISABLED result=0"
            if name == "camera_address_set" and len(parts) == 3:
                address = int(parts[2], 0)
                if not c["buffer"] or not 8 <= address <= 0x77:
                    return f"CAMERA camera={i} request=0 result=INVALID"
                c["address"] = address
                c["restart_required"] = True
                c["result"] = "RESTART_REQUIRED"
                return f"CAMERA camera={i} request=3 result=QUEUED new_address=0x{address:02X}"
            if name == "camera_detect" and len(parts) == 2:
                return f"CAMERA camera={i} request=1 result={'QUEUED' if c['buffer'] else 'IO_ERROR'}"
            if name == "camera_init" and len(parts) == 2:
                c["initialized"] = c["buffer"]
                c["result"] = "OK" if c["initialized"] else "IO_ERROR"
                return f"CAMERA camera={i} request=2 result={'QUEUED' if c['buffer'] else 'IO_ERROR'}"
            if name == "camera_capture" and len(parts) == 2:
                if not c["initialized"]:
                    return f"CAMERA camera={i} request=0 result=NOT_INITIALIZED"
                c["request"] += 1
                c["object"] += 1
                c["data_exist"] = True
                c["result"] = "OK"
                return f"CAMERA camera={i} request={c['request']} result=QUEUED"
            if name == "camera_status" and len(parts) == 2:
                model = "0" if c["model"] == "mlx90640" else "2"
                fmt = c["format"]
                return (f"CAMERA camera={i} model=MLX9064{model} address=0x{c['address']:02X} "
                        f"format={fmt} hz={c['hz']} emissivity_milli={c['emissivity']} "
                        f"initialized={int(c['initialized'])} data_exist={int(c['data_exist'])} "
                        f"request={c['request']} op=2 result={c['result']} error=0 "
                        f"object={c['object']} seq={c['request']}\r\n"
                        f"CAMERA address camera={i} requested=0x{c['address']:02X} "
                        f"restart_required={int(c['restart_required'])} verified=1\r\n"
                        f"CAMERA buffer={i} {'ENABLED' if c['buffer'] else 'DISABLED'} result=0")
            if name == "camera_temp" and len(parts) == 6:
                x, y, w, h = map(int, parts[2:])
                if not (0 <= x < 32 and 0 <= y < 24 and 1 <= w <= 32 - x and
                        1 <= h <= 24 - y and c["data_exist"]):
                    return "ERROR CAMERA TEMP NOT_READY"
                return (f"CAMERA_TEMP camera={i} request={c['request']} "
                        f"roi={x},{y},{w},{h} max_cC={2400 + i*100 + x*4 + y*2} sample_ms=1000")
        except (IndexError, ValueError):
            pass
        return "ERROR CAMERA INVALID"
