"""EXP actuator CLI model: manual thermal (thm_*), fluidic (pump/valve) and motor (motor5_*) commands.

Reply formats are copied from cli_ir.c (THM_CFG/THM_CTL/THM_STAT6), cli_pump.c (FLD_STAT, "Pump request
QUEUED id=", "Pump type: ..."), cli_pc_exp.c ("pc onground_control sent") and cli_motor5.c
(RBA_STAT, "<STATE> request=<id> position=<n>"); test_oracle.py re-checks the literals.
Pump requests from UART0 are always queued, so every pump command answers QUEUED like the real CLI.
"""
import re
import struct

MOTOR_MAX_SPS = 1000                                              # motor5.h MOTOR5_MAX_SPS (was 200 before ddc3ae2)
MOTOR_STOPPED, MOTOR_MOVING, MOTOR_RUNNING, MOTOR_HOMING, MOTOR_HOLDING, MOTOR_ERROR = range(6)


class Actuators:
    def __init__(self):
        self.reset()

    def reset(self):
        self.thm = bytearray(32)
        self.thm[0], self.thm[1] = 1, 0x3F
        for i in range(4):                                        # max duty %, slew %, max on 10 ms units*...
            struct.pack_into("<BBH", self.thm, 2 + 4 * i, 80, 0, 10000)
        for i in range(2):                                        # max duty %, max on ms, PWM Hz, min off x100 ms
            struct.pack_into("<BBIB", self.thm, 18 + 7 * i, 25, 100, 1000, 0)
        self.heater_duty = [0] * 4
        self.ir_duty = [0, 0]
        self.next_id = 100
        self.valves = 0
        self.pump_type = 0                                        # 0 unconfigured, 1 low, 2 high driver
        self.u11 = dict(enabled=False, freq=0, volt=0)
        self.u15 = dict(enabled=False, freq=0, volt=[0, 0, 0, 0])
        self.pump_results = {}
        self.motor_init = False
        self.asleep = False
        self.position = [0, 0]
        self.motor_state = [0, 0]
        self.reference_valid = [False, False]
        self.speed = [0, 0]
        self.motor_results = {}
        self.refuse_motor = None                                  # e.g. "REFUSED": next motion answers this

    def _id(self):
        self.next_id += 1
        return self.next_id

    # ------------------------------------------------------------------------------------
    def handle(self, command, exp):
        """Return the reply for `command`, or None when it is not an actuator command."""
        name, _, rest = command.partition(" ")
        args = rest.split()
        fn = getattr(self, "c_" + name, None)
        if fn is None:
            return None
        return fn(args, exp)

    # -- thermal --------------------------------------------------------------------------
    def c_thm_cfg(self, a, exp):
        if a:
            if len(a) != 1 or not re.fullmatch(r"[0-9a-fA-F]{64}", a[0]):
                return "ERROR thm_cfg: use thm_cfg [64 hex digits]"
            p = bytearray.fromhex(a[0])
            if p[0] != 1 or not p[1] or p[1] & 0xC0:
                return "ERROR thm_cfg: use thm_cfg [64 hex digits]"
            for i in range(4):
                if p[1] >> i & 1 and struct.unpack_from("<H", p, 4 + 4 * i)[0] % 10:
                    return "ERROR thm_cfg: EXP service refused setting"
            for i in range(2):
                if p[1] >> (4 + i) & 1 and p[18 + 7 * i] > 25:
                    return "ERROR thm_cfg: EXP service refused setting"
            for i in range(4):
                if p[1] >> i & 1:
                    self.thm[2 + 4 * i:6 + 4 * i] = p[2 + 4 * i:6 + 4 * i]
            for i in range(2):
                if p[1] >> (4 + i) & 1:
                    self.thm[18 + 7 * i:25 + 7 * i] = p[18 + 7 * i:25 + 7 * i]
        return "THM_CFG " + bytes(self.thm).hex()

    def c_thm_wire_ctl(self, a, exp):
        try:
            if len(a) != 5 or not all(re.fullmatch(r"\d+", x) for x in a):
                raise ValueError
            kind, ch, action, duty, dur = map(int, a)
            if (kind > 1 or ch > 3 or action > 2 or duty > 100 or dur > 600000 or
                    (kind == 1 and ch > 1) or (kind == 0 and action == 2) or
                    (action == 0 and (duty or dur)) or
                    (action == 1 and (not duty or (kind == 0 and not dur) or (kind == 1 and dur))) or
                    (action == 2 and (duty or dur))):
                raise ValueError
        except ValueError:
            return "ERROR THM_CTL INVALID"
        if kind == 0:
            if exp.thermal.heater_phase[ch] != 0:
                return "ERROR THM_CTL REFUSED"
            max_duty, _, max_on = struct.unpack_from("<BBH", self.thm, 2 + 4 * ch)
            if action and (not exp.power["exp12v"] or duty > max_duty or dur > max_on):
                return "ERROR THM_CTL REFUSED"
            self.heater_duty[ch] = duty
            return f"THM_CTL {2 if action else 0} {duty} {self._id()}"
        if action == 0:
            self.ir_duty[ch] = 0
            return f"THM_CTL 0 0 {self._id()}"
        if not exp.power["exp12v"] or duty > self.thm[18 + 7 * ch]:
            return "ERROR THM_CTL REFUSED"
        self.ir_duty[ch] = duty
        return f"THM_CTL 1 0 {self._id()}"

    def c_thm_wire_stat(self, a, exp):
        if a != ["all"]:
            return "ERROR THM_STAT INVALID"
        duties = self.heater_duty + self.ir_duty
        return "THM_STAT6 " + bytes(d | (0x80 if d else 0) for d in duties).hex()

    # -- fluidic --------------------------------------------------------------------------
    def c_pc_onground_control(self, a, exp):
        if len(a) == 4 and all(x in ("0", "1") for x in a):
            self.valves = sum(int(x) << i for i, x in enumerate(a))
            return "pc onground_control sent"
        return "Usage: pc_onground_control <v0> <v1> <v2> <v3>"

    def _queue(self, apply=None):
        if self.pump_type == 0:
            return "Pump type unconfigured"
        if apply:
            apply()
        pid = self._id()
        self.pump_results[pid] = "DONE"
        return f"Pump request QUEUED id={pid}; query pump_result {pid}"

    def c_pump_type_set(self, a, exp):
        if a not in (["lowdriver"], ["highdriver"]):
            return "Usage: pump_type_set <lowdriver|highdriver>"
        self.pump_type = 2 if a[0] == "highdriver" else 1
        return "Pump type: " + a[0]

    def c_pump_type_get(self, a, exp):
        if a:
            return "Usage: pump_type_get"
        return "Pump type: " + ("unconfigured", "lowdriver", "highdriver")[self.pump_type]

    def c_pump_detect(self, a, exp):
        return self._queue()

    c_pump4_detect = c_pump_detect

    def c_pump_enable(self, a, exp):
        return self._queue(lambda: self.u11.update(enabled=True))

    def c_pump_disable(self, a, exp):
        return self._queue(lambda: self.u11.update(enabled=False))

    def c_pump4_enable(self, a, exp):
        return self._queue(lambda: self.u15.update(enabled=True))

    def c_pump4_disable(self, a, exp):
        return self._queue(lambda: self.u15.update(enabled=False))

    def c_pump_set_freq(self, a, exp):
        if len(a) != 1 or not a[0].isdigit() or not 8 <= int(a[0]) <= 1992:
            return "Usage: pump_set_freq/pump4_set_freq <Hz>"
        return self._queue(lambda: self.u11.update(freq=int(a[0])))

    def c_pump4_set_freq(self, a, exp):
        if len(a) != 1 or not a[0].isdigit() or not 50 <= int(a[0]) <= 800:
            return "Usage: pump_set_freq/pump4_set_freq <Hz>"
        return self._queue(lambda: self.u15.update(freq=int(a[0])))

    def c_pump_set_volt(self, a, exp):
        if len(a) != 1 or not a[0].isdigit() or int(a[0]) > 150:
            return "Usage: pump_set_volt <Vpp>"
        return self._queue(lambda: self.u11.update(volt=int(a[0])))

    def c_pump4_set_volt(self, a, exp):
        if len(a) != 2 or not all(x.isdigit() for x in a) or int(a[0]) > 3 or int(a[1]) > 250:
            return "Usage: pump4_set_volt <0-3> <0-250 Vpp>"
        return self._queue(lambda: self.u15["volt"].__setitem__(int(a[0]), int(a[1])))

    def c_pump_result(self, a, exp):
        if len(a) == 1 and a[0].isdigit() and int(a[0]) in self.pump_results:
            return f"Pump request {a[0]} {self.pump_results[int(a[0])]} (r=0)"
        return "Pump result replaced or unknown"

    def c_fld_wire_stat(self, a, exp):
        d = bytearray(16)
        d[0], d[1] = self.valves & 15, 15
        d[2] = (int(exp.power["sln_hd"]) | 2 * (self.pump_type > 0) | 4 * self.u11["enabled"] |
                8 * (self.pump_type > 0) | 16 * self.u15["enabled"])
        d[3] = self.pump_type
        struct.pack_into("<HB", d, 4, self.u11["freq"], self.u11["volt"])
        struct.pack_into("<H", d, 7, self.u15["freq"])
        d[9:13] = bytes(self.u15["volt"])
        return "FLD_STAT " + d.hex()

    # -- motors ---------------------------------------------------------------------------
    def _motor_line(self, name, rid, error=False):
        self.motor_results[rid] = (name, self.position[self._last_motor])
        return f"{'ERROR ' if error else ''}{name} request={rid} position={self.position[self._last_motor]}"

    _last_motor = 0

    def _motion(self, a, minargs, maxargs, kind):
        if not minargs <= len(a) <= maxargs or a[0] not in ("0", "1"):
            return "ERROR INVALID motor command/arguments"
        m = int(a[0])
        self._last_motor = m
        rid = self._id()
        if not self.motor_init or self.asleep:
            return self._motor_line("NOT_READY", rid, True)
        if self.refuse_motor:
            return self._motor_line(self.refuse_motor, rid, True)
        if kind == "move" or kind == "goto":
            if not re.fullmatch(r"-?\d+", a[1]):
                return "ERROR INVALID motor command/arguments"
            steps = int(a[1])
            if len(a) == 3 and not (a[2].isdigit() and int(a[2]) <= MOTOR_MAX_SPS):
                return "ERROR INVALID motor command/arguments"
            self.position[m] = self.position[m] + steps if kind == "move" else steps
            self.speed[m] = 0
            self.motor_state[m] = MOTOR_HOLDING                   # finish(hold=true): hold_pct holds the last phase
            return self._motor_line("DONE", rid)
        if kind == "run":
            if a[1] not in ("cw", "ccw") or not a[2].isdigit() or int(a[2]) > MOTOR_MAX_SPS:
                return "ERROR INVALID motor command/arguments"
            self.motor_state[m] = MOTOR_RUNNING
            self.speed[m] = int(a[2])
            return self._motor_line("ACCEPTED", rid)
        self.position[m] = 0                                      # home releases the outputs (finish(hold=false))
        self.motor_state[m] = MOTOR_STOPPED
        self.reference_valid[m] = True
        return self._motor_line("DONE", rid)

    def c_motor5_init(self, a, exp):
        if a:
            return "ERROR INVALID motor/arguments"
        self._last_motor = 0
        rid = self._id()
        self.motor_init, self.asleep = True, False
        return self._motor_line("DONE", rid)

    def c_motor5_disable(self, a, exp):
        if a:
            return "ERROR INVALID motor/arguments"
        self.asleep = True
        self.motor_state = [0, 0]
        return "DISABLE_REQUESTED; SLEEP asserted; poll motor5_status"

    def c_motor5_move(self, a, exp):
        return self._motion(a, 2, 3, "move")

    def c_motor5_goto(self, a, exp):
        return self._motion(a, 2, 3, "goto")

    def c_motor5_run(self, a, exp):
        return self._motion(a, 3, 3, "run")

    def c_motor5_home(self, a, exp):
        return self._motion(a, 1, 1, "home")

    def c_motor5_stop(self, a, exp):
        hold = len(a) == 2 and a[1] == "hold" and a[0] in ("0", "1")
        if not hold and (len(a) != 1 or a[0] not in ("0", "1", "all")):
            return "ERROR INVALID motor/arguments"
        for m in (0, 1) if a[0] == "all" else (int(a[0]),):
            self.motor_state[m] = 4 if hold and self.motor_state[m] in (1, 2, 3) else 0
            self.speed[m] = 0
        return ("STOP_HOLD_REQUESTED" if hold else "STOP_REQUESTED") + "; poll motor status stop_sequence"

    def c_motor5_result(self, a, exp):
        if len(a) == 1 and a[0].isdigit() and int(a[0]) in self.motor_results:
            name, pos = self.motor_results[int(a[0])]
            return f"{name} request={a[0]} position={pos}"
        return f"ERROR NOT_FOUND request={a[0] if a else 0} position=0"

    def c_rba_wire_stat(self, a, exp):
        out = bytearray(16)
        out[0] = int(self.motor_init) | 2 * int(self.asleep)
        for m in (0, 1):
            out[3] |= int(self.reference_valid[m]) << m
            out[4 + m] = self.motor_state[m]
            struct.pack_into("<i", out, 6 + 4 * m, self.position[m])
            out[14 + m] = min(self.speed[m], 255)                    # one byte per motor on the wire
        return "RBA_STAT " + out.hex().upper()
