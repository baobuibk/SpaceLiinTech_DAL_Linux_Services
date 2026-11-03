"""Stateful EXP crystallization CLI responder for the daltui host rig.

Only models command acceptance and the fields shown by the TUI. It does not
emulate the pump, valve timing or PSRAM PCLG object transfer.
"""
import json
import re
import time


class PcModel:
    def __init__(self):
        self.activated = False
        self.state = "STANDBY"
        self.error = "NONE"
        self.valves = [0, 1, 2, 3]
        self.pulse_ms = 20
        self.flow_ch = 0
        self.log_ms = 1000
        self.datalog = False
        self.resume = False
        self.name = "RIG_PC"
        self.mode = "TIME"
        self.case = "BOTH"
        self.ground = "protein_a"
        self.routes = {}
        self.profile = {}
        self.run_until = 0.0

    def _update(self):
        if self.run_until and time.monotonic() >= self.run_until:
            self.state = "STANDBY"
            self.run_until = 0.0

    def _status(self):
        self._update()
        remain = max(0, int((self.run_until - time.monotonic()) * 1000))
        return ("=== PC Status ===\r\n"
                f"  activated  : {'YES' if self.activated else 'NO'}\r\n"
                f"  datalog    : {'ON' if self.datalog else 'OFF'}\r\n"
                f"  auto_resume: {'ON' if self.resume else 'OFF'}\r\n"
                f"  log_every  : {self.log_ms} ms\r\n"
                f"  state      : {self.state}\r\n"
                f"  error      : {self.error}\r\n"
                "  meas_flow  : 0.00 uL/min\r\n"
                f"  case_mode  : {self.case}\r\n"
                f"  run_mode   : {self.mode}\r\n"
                "  volume     : 0.00 / 0.00 uL\r\n"
                f"  remaining  : {remain} ms\r\n"
                "=================")

    def _profile_show(self):
        lines = ["=== PC Experiment Profile ===", f"  Name       : {self.name}"]
        for target in ("test", "replace", "circulation"):
            minimum = self.profile.get((target, "minflow"), "10.000")
            checked = self.profile.get((target, "flow_check"), "1") == "1"
            delay = self.profile.get((target, "flow_delay"), "500")
            samples = self.profile.get((target, "fail_samples"), "1") if target != "circulation" else "1(fixed)"
            action = self.profile.get((target, "flow_action"), "stop")
            label = "[Circulation]" if target == "circulation" else f"[{target.capitalize()}]"
            lines.extend((f"  {label}",
                          f"    minflow : {minimum} uL/min",
                          f"    flow_chk : {'enabled' if checked else 'disabled'} delay={delay}ms samples={samples} action={action.upper() if action != 'warn' else 'WARNING'}"))
        lines.extend(("  [Global]",
                      f"    run_mode : {self.mode}",
                      f"    log_every : {self.log_ms} ms",
                      f"    datalog/resume : {'ON' if self.datalog else 'OFF'}/{'ON' if self.resume else 'OFF'}",
                      "  [Valve]",
                      f"    channels : {' '.join(map(str, self.valves))} (V0..V3)",
                      f"    time delay : {self.pulse_ms} ms",
                      "  [Flow]",
                      f"    channel : {self.flow_ch}",
                      "============================="))
        return "\r\n".join(lines)

    def status_byte(self):
        self._update()
        code = {"STANDBY": 0, "TEST": 1, "REPLACE": 2, "CIRCULATION": 3,
                "STOPPING": 4, "ERROR": 5, "ONGROUND": 6}[self.state]
        return code | (8 if self.activated else 0)

    def handle(self, command, exp):
        self._update()
        if command == "pc_status":
            return self._status()
        if command == "pc_profile_show":
            return self._profile_show()
        if command == "pc_wire_stat":
            return f"PC_STAT {self.status_byte():02X}"
        if command == "cfg_json_pc":
            return json.dumps({"name": self.name, "valves": self.valves,
                               "flow_channel": self.flow_ch, "run_mode": self.mode})
        match = re.fullmatch(r"pc_valve_conf_channel ([0-3]) ([0-3]) ([0-3]) ([0-3])", command)
        if match:
            values = list(map(int, match.groups()))
            if len(set(values)) != 4:
                return "ERROR duplicate channel assigned"
            self.valves = values
            return "OK pc_valve_conf_channel"
        match = re.fullmatch(r"pc_valve_conf_pulse (\d+)", command)
        if match:
            self.pulse_ms = int(match[1])
            return "OK pc_valve_conf_pulse" if 1 <= self.pulse_ms <= 100 else "ERROR pulse range"
        match = re.fullmatch(r"pc_flow_conf_channel ([01])", command)
        if match:
            self.flow_ch = int(match[1])
            return "OK pc_flow_conf_channel"
        match = re.fullmatch(r"pc_flow_conf_interval (\d+)", command)
        if match:
            self.log_ms = int(match[1])
            return "OK pc_flow_conf_interval" if self.log_ms else "ERROR interval"
        match = re.fullmatch(r"pc_(standby|test|replace|circulation|onground)_conf_dir(?: (a|b|protein_a|protein_b|precip_respol|precip_rsvb))? ([01]) ([01]) ([01]) ([01])", command)
        if match:
            self.routes[(match[1], match[2])] = tuple(map(int, match.groups()[2:]))
            return "OK pc_" + match[1] + "_conf_dir"
        match = re.fullmatch(r"pc_onground_conf (protein_a|protein_b|precip_respol|precip_rsvb) ([01]) ([01]) ([01]) ([01])", command)
        if match:
            self.routes[("ground", match[1])] = tuple(map(int, match.groups()[1:]))
            return "OK pc_onground_conf"
        match = re.fullmatch(r"pc_profile_set (global|test|replace|circulation) (\w+) (\S+)", command)
        if match:
            self.profile[(match[1], match[2])] = match[3]
            if (match[1], match[2]) == ("global", "run_mode"):
                self.mode = match[3].upper()
            if (match[1], match[2]) == ("global", "name"):
                self.name = match[3]
            return "OK pc_profile_set"
        match = re.fullmatch(r"pc_resume_cfg ([01])", command)
        if match:
            self.resume = match[1] == "1"
            return "OK pc_resume_cfg"
        if command in ("pc_datalog_enable", "pc_datalog_disable"):
            self.datalog = command.endswith("enable")
            return f"OK pc_datalog: {'ON' if self.datalog else 'OFF'}"
        if command == "pc_profile_load_default":
            self.profile.clear()
            self.name = "RIG_PC"
            self.mode = "TIME"
            return "OK pc_profile_load_default"
        if command == "pc_activate":
            if not exp.act.pump_type:
                return "ERROR select pump type with pump_type_set before pc_activate"
            self.activated = True
            self.state = "STANDBY"
            exp.power["sln_hd"] = True
            exp.publish_power()
            return "OK pc_activate"
        if command == "pc_deactivate":
            if self.state != "STANDBY":
                return "ERROR pc must be in STANDBY to deactivate"
            self.activated = False
            exp.power["sln_hd"] = False
            exp.publish_power()
            return "OK pc_deactivate"
        match = re.fullmatch(r"pc_(test|replace|circulation) (both|a|b)", command)
        if match:
            if not self.activated or self.state != "STANDBY":
                return "ERROR PC not activated or busy"
            self.state = match[1].upper()
            self.case = match[2].upper()
            self.run_until = time.monotonic() + 5.0
            return f"pc {match[1]} sent"
        match = re.fullmatch(r"pc_onground (protein_a|protein_b|precip_respol|precip_rsvb)", command)
        if match:
            if not self.activated or self.state not in ("STANDBY", "ONGROUND"):
                return "ERROR PC not activated or busy"
            self.state = "ONGROUND"
            self.ground = match[1]
            return "pc onground preset sent"
        if command in ("pc_stop", "pc_standby", "pc_reset_error"):
            self.state = "STANDBY"
            self.run_until = 0.0
            self.error = "NONE"
            return "pc " + command[3:] + " sent"
        return None
