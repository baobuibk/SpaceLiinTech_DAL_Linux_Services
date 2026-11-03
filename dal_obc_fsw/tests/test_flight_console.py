"""Exercise the local socket and the same dispatcher used by RS422."""

import json
import binascii
import os
import pathlib
import pty
import select
import signal
import subprocess
import tempfile
import threading
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
FSW = ROOT / "build-host/dal_obc_fsw"
CLIENT = ROOT / "build-host/dal_flight_console"
GUI = ROOT.parent / "OBC_GUI/target/debug/obc_gui"


def call(path, *args, timeout=5):
    return subprocess.check_output([str(CLIENT), "--socket", str(path), *args],
                                   text=True, timeout=timeout)


def wait_cached_text(path, expected, *args):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        output = call(path, *args)
        if expected in output:
            return output
        time.sleep(0.1)
    raise AssertionError(output)


def read_until(fd, marker, timeout=3):
    result = bytearray()
    deadline = time.monotonic() + timeout
    while marker not in result and time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], max(0, deadline - time.monotonic()))
        if ready:
            result.extend(os.read(fd, 4096))
    assert marker in result, result.decode(errors="replace")
    return bytes(result)


def read_exact(fd, count):
    result = bytearray()
    while len(result) < count:
        ready, _, _ = select.select([fd], [], [], 3)
        assert ready, "RS422 response timed out"
        result.extend(os.read(fd, count - len(result)))
    return bytes(result)


def read_response(fd, marker, timeout=3):
    result = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        index = result.find(marker)
        if index >= 0 and result.find(b"flight> ", index + len(marker)) >= 0:
            return bytes(result)
        ready, _, _ = select.select([fd], [], [], max(0, deadline - time.monotonic()))
        if ready:
            result.extend(os.read(fd, 4096))
    raise AssertionError(result.decode(errors="replace"))


def main():
    master, slave = pty.openpty()
    exp_master, exp_slave = pty.openpty()
    pdu_master, pdu_slave = pty.openpty()
    exp_commands = []
    thp_profiles = {}
    thp_states = {}
    exp_power = {"exp12v": False, "sln_hd": False, "motor": False}
    exp_refuse_normal_off = [False]
    pdu_identity_reads = []
    pdu_identity_enabled = [True]
    pdu_setpoint = [0xffff]
    pdu_trans_on = [False]
    pdu_vout_on = [False]
    pdu_coil_writes = []
    pdu_vout_writes = []
    exp_version_enabled = [True]
    thm_state = bytearray(32)
    thm_outputs = {}
    thm_next_id = [100]
    thm_state[:2] = bytes((1, 0x3f))
    for ch in range(4):
        thm_state[2+4*ch:6+4*ch] = bytes((100, 0, 0xb8, 0x0b))
    for ch in range(2):
        thm_state[18+7*ch:25+7*ch] = bytes((0, 0, 0xa0, 0x86, 0x01, 0, 0))
    def pdu_crc(data):
        crc = 0xffff
        for byte in data:
            crc ^= byte
            for _ in range(8):
                crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
        return crc.to_bytes(2, "little")
    def pdu_peer():
        pending = b""
        try:
            while True:
                pending += os.read(pdu_master, 256)
                while len(pending) >= 8:
                    request, pending = pending[:8], pending[8:]
                    unit, function = request[:2]
                    offset = int.from_bytes(request[2:4], "big")
                    count = int.from_bytes(request[4:6], "big")
                    if request[6:] != pdu_crc(request[:6]):
                        continue
                    if function == 6 and offset == 0:
                        pdu_setpoint[0] = count
                        pdu_trans_on[0] = True  # PDU powers TRANS before TPS55287 write.
                        pdu_vout_writes.append(count)
                        os.write(pdu_master, request)
                        continue
                    if function == 5 and offset in (0, 1):
                        enabled = count == 0xff00
                        pdu_coil_writes.append((offset, enabled))
                        if offset == 0:
                            if enabled and pdu_setpoint[0] == 0xffff:
                                error = bytes([unit, 0x85, 3])
                                os.write(pdu_master, error + pdu_crc(error))
                                continue
                            pdu_vout_on[0] = enabled
                            if enabled:
                                pdu_trans_on[0] = True
                        else:
                            pdu_trans_on[0] = enabled
                            if not enabled:
                                pdu_vout_on[0] = False
                        os.write(pdu_master, request)
                        continue
                    if function == 3 and offset == 99 and count in (2, 10):
                        if count == 10:
                            pdu_identity_reads.append(time.monotonic())
                            if not pdu_identity_enabled[0]:
                                continue
                        words = [0x5044, 0x5500, 0x0102, 0x0003, 0, 1, 0,
                                 0x1234, 0x5678, 0][:count]
                    elif function == 4 and offset == 22 and count == 3:
                        words = [pdu_setpoint[0], 11800 if pdu_setpoint[0] != 0xffff else 0xffff,
                                 240 if pdu_setpoint[0] != 0xffff else 0xffff]
                    elif function == 4 and offset == 19 and count == 12:
                        words = [0, 250, 0, pdu_setpoint[0],
                                 11800 if pdu_setpoint[0] != 0xffff else 0xffff,
                                 240 if pdu_setpoint[0] != 0xffff else 0xffff,
                                 12000, 0, 5000, 100, 5000, 0]
                    else:
                        words = [0] * count
                    if function in (1, 2):
                        bits = ((1 if pdu_vout_on[0] else 0) |
                                (2 if pdu_trans_on[0] else 0)) if function == 1 else (
                                    1 if offset == 10 and pdu_trans_on[0] else 0)
                        data = bytes([bits]) * ((count + 7) // 8)
                    else:
                        data = b"".join(word.to_bytes(2, "big") for word in words)
                    reply = bytes([unit, function, len(data)]) + data
                    os.write(pdu_master, reply + pdu_crc(reply))
        except OSError:
            pass
    threading.Thread(target=pdu_peer, daemon=True).start()
    def exp_peer():
        pending = b""
        try:
            while True:
                pending += os.read(exp_master, 256)
                while b"\r" in pending:
                    raw, pending = pending.split(b"\r", 1)
                    command = raw.decode()
                    exp_commands.append(command)
                    if command.startswith("thp_wire_ctl "):
                        selector = int(command.split()[1], 16)
                        key = ((selector >> 2) & 1, selector & 3)
                        action = (selector >> 3) & 3
                        if action == 1:
                            thp_states[key] = 0xc3 if key[0] else 0xc1
                        elif action == 2:
                            thp_states[key] = 0x40
                        elif action == 3:
                            thp_states[key] = 0xc1
                        reply = f"THP_CTL {thp_states.get(key, 0x40):02x}"
                    elif command.startswith("thp_wire_cfg "):
                        record = bytearray.fromhex(command.split()[1])
                        key = (record[1], record[2])
                        if record[0]:
                            thp_profiles[key] = bytes(record)
                        else:
                            record = bytearray(thp_profiles.get(key, bytes(record)))
                            record[0] = 0
                        reply = "THP_CFG " + record.hex()
                    elif command == "tlm_json valves":
                        reply = '{"valves":{"commanded_open_mask":5,"position_valid_mask":15,"busy":false,"driver_ready":true,"position_feedback":false}}'
                    elif command.startswith("tlm_json "):
                        section = command.split()[1]
                        reply = json.dumps({"schema_version": 2, "section": section,
                                            "data": {"online": True}})
                    elif command == "version":
                        if not exp_version_enabled[0]:
                            continue
                        reply = "DAL_EXP firmware v1.4.2 (release) build abc1234"
                    elif command == "cfg_json_pc":
                        reply = json.dumps({"status": "CHUNK_REQUIRED", "report_id": 7,
                                            "total": 112, "next": 0}, separators=(",", ":"))
                    elif command.startswith("json_chunk 7 "):
                        offset = int(command.split()[2])
                        sample = '{"section":"config.pc","data":' + '"' + 'x' * 77 + '"}'
                        reply = json.dumps({"report_id": 7, "offset": offset,
                                            "next": offset + 64, "total": 112,
                                            "data": sample[offset:offset+64]}, separators=(",", ":"))
                    elif command == "pump_detect":
                        reply = "Pump request QUEUED id=42; query pump_result 42"
                    elif command == "pump4_detect":
                        reply = "Pump request QUEUED id=43; query pump_result 43"
                    elif command == "pump_result 42":
                        reply = "Pump request 42 DONE (r=0)"
                    elif command == "pump_result 43":
                        reply = "Pump request 43 IO_ERROR (r=5)"
                    elif command == "pump_state":
                        reply = "U11 rail=1 init=1 enabled=1 frequency=500 Hz\nvoltage=120 Vpp"
                    elif command == "pc_datalog_enable":
                        reply = "OK pc_datalog: ON (takes effect on next run)"
                    elif command == "pc_datalog_disable":
                        reply = "OK pc_datalog: OFF (takes effect on next run)"
                    elif command == "pc_profile_load_default":
                        reply = "OK pc_profile_load default"
                    elif command == "pc_activate":
                        reply = "OK pc_activate"
                    elif command == "pc_deactivate":
                        reply = "OK pc_deactivate"
                    elif command.startswith("pc_test "):
                        reply = "pc test sent"
                    elif command.startswith("pc_replace "):
                        reply = "pc replace sent"
                    elif command.startswith("pc_circulation "):
                        reply = "pc circulation sent"
                    elif command.startswith("pc_onground "):
                        reply = "pc onground preset sent"
                    elif command == "pc_standby":
                        reply = "pc standby sent"
                    elif command == "pc_stop":
                        reply = "pc stop sent"
                    elif command == "pc_reset_error":
                        reply = "pc reset_error sent"
                    elif command == "pc_wire_stat":
                        reply = "PC_STAT 29"  # activated Test, Case B
                    elif command == "pump4_state":
                        reply = "U15 rail=1 init=1 enabled=0 frequency=400 Hz\nchannel 0: 25 Vpp\nchannel 1: 50 Vpp\nchannel 2: 75 Vpp\nchannel 3: 100 Vpp"
                    elif command == "fld_wire_stat":
                        reply = "FLD_STAT 050f4f02f40178900119324b64000000"
                    elif command.startswith(("motor5_move ", "motor5_goto ", "motor5_run ",
                                             "motor5_home ")) or command == "motor5_init":
                        reply = "ACCEPTED request=71 position=-12"
                    elif command == "motor5_result 71":
                        reply = "DONE request=71 position=123"
                    elif command.startswith("motor5_stop ") and command.endswith(" hold"):
                        reply = "STOP_HOLD_REQUESTED; poll motor status stop_sequence"
                    elif command.startswith("motor5_stop "):
                        reply = "STOP_REQUESTED; poll motor status stop_sequence"
                    elif command == "motor5_disable":
                        reply = "DISABLE_REQUESTED; SLEEP asserted; poll motor status"
                    elif command == "rba_wire_stat":
                        status = (bytes((0x21, 0x05, 0x0f, 0x03, 1, 0)) +
                                  (123).to_bytes(4, "little", signed=True) +
                                  (-12).to_bytes(4, "little", signed=True) + bytes((80, 0)))
                        reply = "RBA_STAT " + status.hex()
                    elif command.startswith("pc_onground_control "):
                        reply = "pc onground_control sent"
                    elif command.startswith(("pump_enable", "pump_disable", "pump_set_",
                                             "pump4_enable", "pump4_disable", "pump4_set_")):
                        reply = "Pump request QUEUED id=44; query pump_result 44"
                    elif command.startswith(("pc_valve_conf_", "pc_flow_conf_", "pc_standby_conf_",
                                             "pc_test_conf_", "pc_replace_conf_", "pc_circulation_conf_",
                                             "pc_onground_conf", "pc_profile_set", "pc_resume_cfg")) or command.startswith("pump_type_set "):
                        reply = ("Pump type: highdriver" if command == "pump_type_set highdriver"
                                 else "Pump type: lowdriver" if command == "pump_type_set lowdriver"
                                 else "OK " + command.split()[0])
                    elif command == "thm_cfg" or command.startswith("thm_cfg "):
                        if command.startswith("thm_cfg "):
                            data = bytes.fromhex(command.split()[1])
                            for ch in range(4):
                                if data[1] & (1 << ch):
                                    thm_state[2+4*ch:6+4*ch] = data[2+4*ch:6+4*ch]
                            for ch in range(2):
                                if data[1] & (1 << (ch+4)):
                                    thm_state[18+7*ch:25+7*ch] = data[18+7*ch:25+7*ch]
                        reply = "THM_CFG " + thm_state.hex()
                    elif command.startswith("thm_wire_ctl "):
                        _, kind, channel, action, duty, duration = command.split()
                        kind, channel, action, duty = map(int, (kind, channel, action, duty))
                        thm_next_id[0] += 1
                        thm_outputs[(kind, channel)] = (action, duty, thm_next_id[0])
                        reply = f"THM_CTL {0 if action == 0 else 2 if kind == 0 else 1} {duty if kind == 0 else 0} {thm_next_id[0]}"
                    elif command == "thm_wire_stat all":
                        values = bytearray(6)
                        for index in range(6):
                            kind, channel = (0, index) if index < 4 else (1, index-4)
                            action, duty, _ = thm_outputs.get((kind, channel), (0, 0, 0))
                            values[index] = duty | (0x80 if duty else 0)
                        reply = "THM_STAT6 " + values.hex()
                    elif command.startswith("thm_wire_stat "):
                        _, kind, channel, request_id = command.split()
                        kind, channel, request_id = map(int, (kind, channel, request_id))
                        action, duty, last_id = thm_outputs.get((kind, channel), (0, 0, 0))
                        record = bytearray(16)
                        record[0:2] = bytes((kind, channel))
                        record[2] = 1 if action else 0
                        record[3] = 3 if request_id else 2 if action else 0
                        record[4:6] = bytes((duty, duty))
                        record[6] = 1
                        record[8:12] = (request_id or last_id).to_bytes(4, "little")
                        record[12:16] = (0xffffffff if kind and action else 5000 if action else 0).to_bytes(4, "little")
                        reply = "THM_STAT " + record.hex()
                    elif command == "bme688_cfg_get":
                        reply = "BME688 config temp_os=2 press_os=3 hum_os=1 filter=2 gas=off heater_c=0 heater_10ms=0"
                    elif command.startswith("bme688_cfg_set "):
                        reply = "BME688 QUEUED id=17"
                    elif command == "bme688_result 17":
                        reply = "BME688 DONE id=17"
                    elif command == "lsm6d_conf_get":
                        reply = "desired xl=5/3 gyro=5/3; effective xl=5/3 gyro=5/3"
                    elif command.startswith("lsm6d_conf_") or command == "lsm6d_init":
                        reply = "LSM6DSOX QUEUED id=18; query lsm6d_result"
                    elif command == "lsm6d_result 18":
                        reply = "LSM6DSOX DONE id=18 error=0"
                    elif command.startswith("power_"):
                        if command == "power_all_get":
                            reply = "\r\n".join(
                                f"Power {label} requested={'ON' if exp_power[key] else 'OFF'} observed=UNKNOWN phase=IDLE last=OK seq=1"
                                for key, label in (("exp12v", "12V_EXP"),
                                                   ("sln_hd", "SLN HD"),
                                                   ("motor", "MOTOR")))
                            os.write(exp_master, raw + b"\r\n" + reply.encode()
                                     + b"\r\nDEBUG@MCU:~ $ ")
                            continue
                        rail = command.split("_")[1]
                        if rail == "sln":
                            label, key = "SLN HD", "sln_hd"
                        elif rail == "exp12v":
                            label, key = "12V_EXP", "exp12v"
                        else:
                            label, key = "MOTOR", "motor"
                        if command.endswith("_get"):
                            state = exp_power[key]
                            reply = f"Power {label} requested={'ON' if state else 'OFF'} observed=UNKNOWN phase=IDLE last=OK seq=1"
                        else:
                            desired = "_on" in command
                            refused = not desired and exp_refuse_normal_off[0] and "force" not in command
                            if not refused:
                                exp_power[key] = desired
                            reply = f"Power {label} {'ON' if desired else 'OFF'}: {'REFUSED' if refused else 'OK'}"
                    else:
                        reply = "PC state=IDLE"
                    os.write(exp_master, raw + b"\r\n" + reply.encode()
                             + b"\r\nDEBUG@MCU:~ $ ")
        except OSError:
            pass
    peer = threading.Thread(target=exp_peer, daemon=True)
    peer.start()
    with tempfile.TemporaryDirectory(prefix="dal-flight-console-") as tmp:
        path = pathlib.Path(tmp) / "console.sock"
        snapshot = pathlib.Path(tmp) / "obc_data.json"
        leds = pathlib.Path(tmp) / "leds"
        (leds / "pow_onoff_fpga").mkdir(parents=True)
        (leds / "pow_onoff_fpga/brightness").write_text("0\n")
        hw_env = dict(os.environ, DAL_GUI_LED_ROOT=str(leds),
                      DAL_GUI_FPGA_LOCK=str(pathlib.Path(tmp) / "fpga.lock"))
        subprocess.check_call([str(FSW), "--gui-hw", "gpio", "pow_onoff_fpga", "on"],
                              env=hw_env, stdout=subprocess.DEVNULL)
        assert (leds / "pow_onoff_fpga/brightness").read_text().strip() == "1"
        subprocess.check_call([str(FSW), "--gui-hw", "gpio", "pow_onoff_fpga", "off"],
                              env=hw_env, stdout=subprocess.DEVNULL)
        assert (leds / "pow_onoff_fpga/brightness").read_text().strip() == "0"
        assert subprocess.run([str(FSW), "--gui-hw", "gpio", "unknown_pin", "on"],
                              env=hw_env, capture_output=True).returncode != 0
        for name in ("pow_mipicam_1", "pow_mipicam_2", "en_i2c_mipicam_1",
                     "en_i2c_mipicam_2", "sel_mipicam_on2_off1"):
            (leds / name).mkdir()
            (leds / name / "brightness").write_text("0\n")
        for name in ("pow_onoff_ai", "usb_1_pwr_en", "usb_2_pwr_en",
                     "usb_3_pwr_en", "usb_4_pwr_en"):
            (leds / name).mkdir()
            (leds / name / "brightness").write_text("0\n")
        lane = pathlib.Path(tmp) / "lane"
        sensor = pathlib.Path(tmp) / "sensor"
        lane.write_text("0\n")
        sensor.write_text("0\n")
        hw_env.update(DAL_GUI_MIPI_LOCK=str(pathlib.Path(tmp) / "mipi.lock"),
                      DAL_GUI_MIPI_LANE=str(lane), DAL_GUI_MIPI_SENSOR=str(sensor))
        subprocess.check_call([str(FSW), "--gui-hw", "mipi-power", "1", "on"],
                              env=hw_env, stdout=subprocess.DEVNULL)
        assert (leds / "pow_mipicam_1/brightness").read_text().strip() == "1"
        subprocess.check_call([str(FSW), "--gui-hw", "mipi-route", "1", "2"],
                              env=hw_env, stdout=subprocess.DEVNULL)
        assert lane.read_text().strip() == sensor.read_text().strip() == "2"
        assert (leds / "en_i2c_mipicam_2/brightness").read_text().strip() == "0"
        exp_snapshot = pathlib.Path(tmp) / "exp_data.json"
        exp_snapshot.write_text(json.dumps({
            "online": 1, "data_valid": 1, "obc_timestamp": int(time.time()),
            "sensor_valid_mask": 1 << 9,
            "sensor_age_ms": [0] * 9 + [10],
            "sensor_status": [2] * 10,
            "pwr_volt_mv": [12000, 11900, 5000, 5100],
            "pwr_curr_ma": [-25, 120, 50, 60],
        }))
        fake_uvc = pathlib.Path(tmp) / "fake_uvc.py"
        fake_uvc.write_text("#!/usr/bin/env python3\nimport sys\nfrom pathlib import Path\n"
                            "if sys.argv[2] == '-o':\n"
                            "    Path(sys.argv[3]).write_bytes(b'\\xff\\xd8test\\xff\\xd9')\n"
                            "else:\n"
                            "    print(sys.argv[3] if sys.argv[2] == '--uvc-set' else "
                            "(bytes((int(sys.argv[1]), 1)) + bytes(30)).hex())\n")
        fake_uvc.chmod(0o755)
        (pathlib.Path(tmp) / "data").mkdir()
        env = dict(os.environ, DAL_OBC_RS422_PORT=os.ttyname(slave),
                   DAL_EXP_UART_PORT=os.ttyname(exp_slave),
                   DAL_GUI_PDU_PORT=os.ttyname(pdu_slave),
                   DAL_EXP_UART_BAUD="115200",
                   DAL_FLIGHT_CONSOLE_SOCKET=str(path),
                   DAL_OBC_PORT_CONFIG_FILE=str(pathlib.Path(tmp) / "dal-obc-fsw.env"),
                   DAL_DATA_DIR=str(pathlib.Path(tmp) / "data"),
                   DAL_OBC_HK_SNAPSHOT_PATH=str(snapshot),
                   DAL_EXP_HK_SNAPSHOT_PATH=str(exp_snapshot), DAL_EXP_CSP_ENABLE="0",
                   DAL_OBC_HK_COLLECTOR_ENABLE="0", **{
                       "DAL_USB_CAM_TOOL": str(fake_uvc),
                       "DAL_UVC_DATA_DIR": str(pathlib.Path(tmp) / "uvc"),
                       "DAL_GUI_LED_ROOT": str(leds),
                       "DAL_GUI_FPGA_LOCK": str(pathlib.Path(tmp) / "fpga.lock")})
        env.pop("DAL_EXP_UART_POLL", None)
        process = subprocess.Popen([str(FSW)], env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        try:
            for _ in range(100):
                if path.exists():
                    break
                if process.poll() is not None:
                    raise AssertionError(f"flight software exited: {process.returncode}\n"
                                         f"{process.stdout.read()}")
                time.sleep(0.05)
            else:
                raise AssertionError("console socket did not appear")

            output = call(path, "ping")
            assert "OK ping: uptime=" in output, output
            ports = json.loads(call(path, "--json", "port_cfg", "get"))
            assert ports["ok"] and "DAL_EXP_UART_PORT=/dev/ttyS4" in ports["text"], ports
            saved = json.loads(call(path, "--json", "port_cfg", "set", "DAL_EXP_UART_PORT", "/dev/ttyS5"))
            assert saved["ok"], saved
            ports = json.loads(call(path, "--json", "port_cfg", "get"))
            assert "DAL_EXP_UART_PORT=/dev/ttyS5" in ports["text"], ports
            rejected = json.loads(call(path, "--json", "port_cfg", "set", "DAL_GUI_PDU_PORT", "/dev/ttyS2"))
            assert not rejected["ok"], rejected
            output = call(path, "pdu_vout", "get")
            uvc = json.loads(call(path, "--json", "uvc_cfg", "2", "get"))
            assert uvc["ok"] and uvc["length"] == 32 and uvc["payload_hex"].startswith("0201"), uvc
            uvc = json.loads(call(path, "--json", "uvc_cfg", "2", "set", "format", "MJPG",
                                  "resolution", "1280x720", "exposure", "manual"))
            assert uvc["ok"] and uvc["length"] == 32, uvc
            assert bytes.fromhex(uvc["payload_hex"])[4:8] == b"MJPG", uvc
            capture = json.loads(call(path, "--json", "uvc_ctl", "2", "capture", "1042"))
            assert capture["ok"] and capture["length"] == 8, capture
            for _ in range(100):
                state = json.loads(call(path, "--json", "uvc_stat", "2", "get", "1042"))
                if bytes.fromhex(state["payload_hex"])[1] == 3:
                    break
                time.sleep(0.02)
            else:
                raise AssertionError(state)
            assert (pathlib.Path(tmp) / "uvc/USB1_00001042.jpg").read_bytes().startswith(b"\xff\xd8")
            payload = bytes((2,)) + (1042).to_bytes(4, "little")
            frame = bytes.fromhex("a5 01 12 05 00") + payload + binascii.crc_hqx(payload, 0xffff).to_bytes(2, "little")
            os.write(master, frame)
            header = read_exact(master, 5)
            assert header == bytes.fromhex("a5 01 12 08 00"), header.hex()
            answer = read_exact(master, 10)
            assert answer[:8] == bytes((2, 3, 0, 0)) + (1042).to_bytes(4, "little"), answer.hex()
            assert binascii.crc_hqx(answer[:8], 0xffff) == int.from_bytes(answer[8:], "little"), answer.hex()
            assert "TRANS=off" in output and "setpoint=unavailable" in output, output
            no_setpoint = json.loads(call(path, "--json", "pwr_en", "PDU", "VOUT", "on"))
            assert not no_setpoint["ok"] and bytes.fromhex(no_setpoint["payload_hex"])[5:7] == b"\x01\0", no_setpoint
            output = call(path, "pdu_vout", "set", "12000")
            assert "TRANS=on" in output and "setpoint=12000 mV" in output, output
            assert "measured=11800 mV" in output and pdu_vout_writes == [12000], output
            readback = json.loads(call(path, "--json", "pdu_vout", "get"))
            assert readback["ok"] and readback["length"] == 8, readback
            assert bytes.fromhex(readback["payload_hex"]) == bytes.fromhex(
                "e0 2e 18 2e f0 00 1d 00"), readback
            os.write(master, bytes.fromhex("a5 01 02 00 00 ff ff"))
            header = read_exact(master, 5)
            assert header == bytes.fromhex("a5 01 02 08 00"), header.hex()
            answer = read_exact(master, 10)
            assert answer[:8] == bytes.fromhex("e0 2e 18 2e f0 00 1d 00"), answer.hex()
            os.write(master, bytes.fromhex("a5 01 02 02 00 e0 2e 11 c8"))
            header = read_exact(master, 5)
            assert header == bytes.fromhex("a5 01 02 08 00"), header.hex()
            answer = read_exact(master, 10)
            assert answer[:8] == bytes.fromhex("e0 2e 18 2e f0 00 1d 00"), answer.hex()
            assert pdu_vout_writes == [12000, 12000]
            power = json.loads(call(path, "--json", "pwr_en", "PDU", "VOUT", "on"))
            assert power["ok"] and bytes.fromhex(power["payload_hex"]) == bytes.fromhex(
                "05 03 00 03 00 00 00"), power
            assert pdu_coil_writes[-1] == (0, True)
            bad_power = json.loads(call(path, "--json", "pwr_en", "PDU", "set", "3", "1"))
            assert not bad_power["ok"] and bad_power["code"] == 1, bad_power
            power = json.loads(call(path, "--json", "pwr_en", "PDU", "TRANS", "off"))
            assert power["ok"] and bytes.fromhex(power["payload_hex"])[1:3] == b"\0\0", power
            assert pdu_coil_writes[-1] == (1, False)
            power = json.loads(call(path, "--json", "pwr_en", "EXP", "SLN_HD", "on"))
            assert power["ok"] and exp_power["sln_hd"], power
            assert "power_sln_hd_on" in exp_commands, exp_commands
            exp_refuse_normal_off[0] = True
            refused = json.loads(call(path, "--json", "pwr_en", "EXP", "SLN_HD", "off"))
            assert not refused["ok"] and bytes.fromhex(refused["payload_hex"])[5:7] == b"\x02\0", refused
            power = json.loads(call(path, "--json", "pwr_en", "EXP", "SLN_HD", "off", "force"))
            assert power["ok"] and not exp_power["sln_hd"], power
            assert "power_sln_hd_off force" in exp_commands, exp_commands
            exp_refuse_normal_off[0] = False
            power = json.loads(call(path, "--json", "pwr_en", "OBC", "set", "0x41", "0x41"))
            assert power["ok"] and bytes.fromhex(power["payload_hex"])[1:3] == b"\x41\0", power
            assert (leds / "pow_onoff_ai/brightness").read_text().strip() == "1"
            assert (leds / "pow_mipicam_1/brightness").read_text().strip() == "1"
            power = json.loads(call(path, "--json", "pwr_en", "OBC", "MIPI1_power", "off"))
            assert power["ok"] and (leds / "en_i2c_mipicam_1/brightness").read_text().strip() == "0"
            power = json.loads(call(path, "--json", "pwr_en", "OBC", "AI_power", "off"))
            assert power["ok"] and (leds / "pow_onoff_ai/brightness").read_text().strip() == "0"
            bad_power = json.loads(call(path, "--json", "raw", "1", "3"))
            assert not bad_power["ok"] and bad_power["code"] == 0, bad_power
            payload = bytes.fromhex("03 01 00 01 00 00")
            frame = bytes.fromhex("a5 01 03 06 00") + payload + binascii.crc_hqx(payload, 0xffff).to_bytes(2, "little")
            os.write(master, frame)
            header = read_exact(master, 5)
            assert header == bytes.fromhex("a5 01 03 07 00"), header.hex()
            answer = read_exact(master, 9)
            assert answer[:3] == b"\x03\x01\0" and answer[3:5] == b"\xff\0", answer.hex()
            status = json.loads(call(path, "--json", "pwr_stat", "OBC", "get"))
            assert status["ok"] and status["length"] == 198, status
            status = json.loads(call(path, "--json", "pwr_stat", "OBC", "get", "AI_power"))
            assert status["ok"] and status["length"] == 18, status
            row = bytes.fromhex(status["payload_hex"])[6:]
            assert row[0] == 8 and row[1] & 1, row.hex()
            status = json.loads(call(path, "--json", "pwr_stat", "EXP", "get", "12V_MOTOR"))
            row = bytes.fromhex(status["payload_hex"])[6:]
            assert status["ok"] and status["length"] == 18 and row[0] == 0, status
            assert row[1] & 0x1c == 0x1c and int.from_bytes(row[4:8], "little") == 12000, row.hex()
            assert int.from_bytes(row[8:12], "little", signed=True) == -25, row.hex()
            status = json.loads(call(path, "--json", "pwr_stat", "PDU", "get"))
            assert status["ok"] and status["length"] == 54, status
            sensor = json.loads(call(path, "--json", "sen_stat", "PDU", "get", "temperature"))
            assert sensor["ok"] and sensor["type"] == 1 and sensor["address"] == 7, sensor
            assert sensor["length"] == 22, sensor
            record = bytes.fromhex(sensor["payload_hex"])[10:]
            assert record[:4] == b"\x01\x00\x01\x01", record.hex()
            exp_snapshot.write_text(json.dumps({
                "online": 1, "data_valid": 1, "obc_timestamp": int(time.time()),
                "sensor_valid_mask": 1023,
                "sensor_age_ms": [100] * 10,
                "sensor_status": [2] * 10,
                "ntc_c": [21.1, 22.2, 23.3, 24.4], "board_temp_c": 25.5,
                "bme_temp_c": 26.5, "bme_press_pa": 101325, "bme_hum_rh": 44.5,
                "imu_mask": 7, "imu_accel_mg": [0, 0, 1000],
                "imu_gyro_dps": [0.1, 0.2, 0.3], "imu_temp_c": 27.0,
                "flow_ul_min": [12.34, 56.78], "flow_temp_c": [25.1, 25.2],
                "pwr_volt_mv": [12000, 11900, 5000, 5100],
                "pwr_curr_ma": [-25, 120, 50, 60]
            }))
            sensor = json.loads(call(path, "--json", "sen_stat", "EXP", "get", "flow", "flow2"))
            assert sensor["ok"] and sensor["length"] == 34, sensor
            records = bytes.fromhex(sensor["payload_hex"])[10:]
            assert records[:4] == b"\x06\x01\x01\x07", records.hex()
            assert int.from_bytes(records[8:12], "little", signed=True) == 5678, records.hex()
            sensor = json.loads(call(path, "--json", "sen_stat", "EXP", "get", "all"))
            assert sensor["ok"] and sensor["length"] == 106, sensor
            assert bytes.fromhex(sensor["payload_hex"])[5] == 8, sensor
            sensor = json.loads(call(path, "--json", "sen_stat", "EXP", "get", "all", "cursor", "8"))
            assert sensor["ok"] and sensor["length"] == 106, sensor
            sensor = json.loads(call(path, "--json", "sen_stat", "EXP", "get", "camera", "camera1"))
            assert sensor["ok"] and sensor["length"] == 22, sensor
            assert bytes.fromhex(sensor["payload_hex"])[12] == 0x20, sensor
            fresh_exp = exp_snapshot.read_text()
            stale_exp = json.loads(fresh_exp)
            stale_exp["obc_timestamp"] = int(time.time()) - 60
            exp_snapshot.write_text(json.dumps(stale_exp))
            sensor = json.loads(call(path, "--json", "sen_stat", "EXP", "get", "bme"))
            assert sensor["ok"] and sensor["length"] == 10, sensor
            assert bytes.fromhex(sensor["payload_hex"])[3:6] == b"\x01\x00\xff", sensor
            exp_snapshot.write_text(fresh_exp)
            payload = bytes.fromhex("04 04 06 00")
            frame = bytes.fromhex("a5 01 07 04 00") + payload + binascii.crc_hqx(payload, 0xffff).to_bytes(2, "little")
            os.write(master, frame)
            header = read_exact(master, 5)
            assert header == bytes.fromhex("a5 01 07 22 00"), header.hex()
            answer = read_exact(master, 36)
            assert answer[:6] == bytes.fromhex("04 04 06 00 02 ff"), answer.hex()
            assert binascii.crc_hqx(answer[:-2], 0xffff) == int.from_bytes(answer[-2:], "little"), answer.hex()
            invalid_sensor = json.loads(call(path, "--json", "sen_stat", "EXP", "get", "flow", "imu"))
            assert not invalid_sensor["ok"] and invalid_sensor["code"] == 1, invalid_sensor
            invalid_status = json.loads(call(path, "--json", "raw", "1", "4", "5", "16", "0"))
            assert not invalid_status["ok"] and invalid_status["code"] == 1, invalid_status
            payload = bytes.fromhex("03 00 01")
            frame = bytes.fromhex("a5 01 04 03 00") + payload + binascii.crc_hqx(payload, 0xffff).to_bytes(2, "little")
            os.write(master, frame)
            header = read_exact(master, 5)
            assert header == bytes.fromhex("a5 01 04 12 00"), header.hex()
            answer = read_exact(master, 20)
            assert answer[0] == 3 and answer[1] == 1 and answer[6] == 8, answer.hex()
            assert binascii.crc_hqx(answer[:18], 0xffff) == int.from_bytes(answer[18:], "little"), answer.hex()
            invalid = json.loads(call(path, "--json", "raw", "1", "2", "0", "0"))
            assert not invalid["ok"] and invalid["code"] == 1 and pdu_vout_writes == [12000, 12000], invalid
            old_length = json.loads(call(path, "--json", "raw", "1", "2",
                                         "224", "46", "0", "0", "0", "0", "0", "0"))
            assert not old_length["ok"] and old_length["code"] == 0, old_length
            before = int(time.time())
            rtc = json.loads(call(path, "--json", "sys_rtc"))
            after = int(time.time())
            assert rtc["ok"] and rtc["length"] == 4, rtc
            assert before <= int.from_bytes(bytes.fromhex(rtc["payload_hex"]), "little") <= after, rtc
            assert "epoch=" in rtc["text"], rtc
            bad_rtc = json.loads(call(path, "--json", "raw", "1", "1", "1"))
            assert not bad_rtc["ok"] and bad_rtc["code"] == 0, bad_rtc
            os.write(master, bytes.fromhex("a5 01 01 00 00 ff ff"))
            header = read_exact(master, 5)
            assert header == bytes.fromhex("a5 01 01 04 00"), header.hex()
            answer = read_exact(master, 6)
            assert before <= int.from_bytes(answer[:4], "little") <= int(time.time())
            os.write(master, bytes.fromhex("a5 00 00 00 00 ff ff"))
            ready, _, _ = select.select([master], [], [], 2)
            assert ready, "RS422 ping reply timed out"
            frame = os.read(master, 64)
            assert frame[:5] == bytes.fromhex("a5 00 00 04 00"), frame.hex()
            output = call(path, "sys_info")
            assert "PDU online hardware=1.0.0 firmware=1.2.3 build=12345678" in output, output
            assert "EXP offline hardware=255.255.255" in output, output
            assert "OBC online hardware=1.0.0 firmware=0.1.0" in output, output
            assert pdu_identity_reads and "version" not in exp_commands
            assert not any(command.startswith("tlm_json ") for command in exp_commands)
            identity_polls = len(pdu_identity_reads)
            info = json.loads(call(path, "--json", "sys_info"))
            assert info["length"] == 96 and pdu_identity_reads, info
            assert len(pdu_identity_reads) == identity_polls, pdu_identity_reads
            assert "version" not in exp_commands, exp_commands
            output = call(path, "sys_info")
            assert "PDU online hardware=1.0.0" in output, output
            assert "EXP offline hardware=255.255.255" in output, output
            output = call(path, "sys_info", timeout=25)
            assert "PDU online hardware=1.0.0" in output, output
            assert "EXP offline hardware=255.255.255" in output, output
            output = json.loads(call(path, "--json", "exp_tlm", "sensors"))
            assert output["ok"] and json.loads(output["text"].split("\n", 1)[1])["section"] == "sensors", output
            output = json.loads(call(path, "--json", "exp_run", "pc_status"))
            assert output["ok"] and "PC state=IDLE" in output["text"], output
            output = json.loads(call(path, "--json", "thm_cfg", "get"))
            assert output["ok"] and output["length"] == 32, output
            output = json.loads(call(path, "--json", "fld_cfg", "get"))
            assert output["ok"] and output["length"] == 32 and "config JSON offset=0" in output["text"], output
            output = json.loads(call(path, "--json", "fld_cfg", "get", "1"))
            assert output["ok"] and "config JSON offset=28" in output["text"], output
            assert "configuration accepted" in call(path, "fld_cfg", "valve", "map", "0", "1", "2", "3")
            assert exp_commands[-1] == "pc_valve_conf_channel 0 1 2 3"
            pc = json.loads(call(path,"--json","pc_cfg","profile","test","flow_check","1"))
            assert pc["ok"] and pc["length"] == 32 and exp_commands[-1] == "pc_profile_set test flow_check 1", pc
            pc = json.loads(call(path,"--json","pc_cfg","profile","global","name","PC_TEST"))
            assert pc["ok"] and exp_commands[-1] == "pc_profile_set global name PC_TEST", pc
            pc = json.loads(call(path,"--json","pc_cfg","datalog","on"))
            assert pc["ok"] and exp_commands[-1] == "pc_datalog_enable", pc
            pc = json.loads(call(path,"--json","pc_cfg","defaults"))
            assert pc["ok"] and exp_commands[-1] == "pc_profile_load_default", pc
            pc = json.loads(call(path,"--json","pc_cfg","get","1"))
            assert pc["ok"] and pc["length"] == 32, pc
            assert not json.loads(call(path,"--json","pc_cfg","pump","detect","u11"))["ok"]
            heater_args = ("thp_cfg", "heater", "set", "0", "0", "1", "3500", "200",
                           "4500", "1000", "0", "5", "5", "1.0", "0.1", "0.0")
            thp = json.loads(call(path, "--json", *heater_args))
            assert thp["ok"] and thp["length"] == 32 and "target_cC=3500" in thp["text"], thp
            assert len(exp_commands[-1].split()[1]) == 64
            thp = json.loads(call(path, "--json", "thp_cfg", "heater", "get", "0"))
            assert thp["ok"] and "target_cC=3500" in thp["text"], thp
            ir_args = ("thp_cfg", "ir", "set", "1", "0", "4", "3", "12", "10",
                       "3500", "4500", "200", "1000", "5000", "8", "5", "1.0", "0", "0")
            thp = json.loads(call(path, "--json", *ir_args))
            assert thp["ok"] and thp["length"] == 32 and "ir led=1" in thp["text"], thp
            thp = json.loads(call(path, "--json", "thp_cfg", "ir", "get", "1"))
            assert thp["ok"] and "target_cC=3500" in thp["text"], thp
            assert not json.loads(call(path, "--json", "thp_cfg", "ir", "get", "2"))["ok"]
            ctl = json.loads(call(path, "--json", "thp_ctl", "heater", "0", "start"))
            assert ctl["ok"] and ctl["payload_hex"] == "c1" and "phase=soak" in ctl["text"], ctl
            ctl = json.loads(call(path, "--json", "thp_ctl", "heater", "0", "status"))
            assert ctl["ok"] and exp_commands[-1] == "thp_wire_ctl 00", ctl
            ctl = json.loads(call(path, "--json", "thp_ctl", "heater", "0", "stop"))
            assert ctl["ok"] and ctl["payload_hex"] == "40", ctl
            ctl = json.loads(call(path, "--json", "thp_ctl", "ir", "1", "start"))
            assert ctl["ok"] and ctl["payload_hex"] == "c3" and "phase=running" in ctl["text"], ctl
            assert not json.loads(call(path, "--json", "thp_ctl", "ir", "1", "resume"))["ok"]
            selector = bytes((0x0c,))  # IR LED 0 start.
            os.write(master, bytes.fromhex("a5 02 04 01 00") + selector +
                     binascii.crc_hqx(selector, 0xffff).to_bytes(2, "little"))
            assert read_exact(master, 5) == bytes.fromhex("a5 02 04 01 00")
            ctl_reply = read_exact(master, 3)
            assert ctl_reply[0] == 0xc3
            assert binascii.crc_hqx(ctl_reply[:1], 0xffff) == int.from_bytes(ctl_reply[1:], "little")
            pc_payload = bytes((1, 6, 1, 13, 1)) + bytes(27)
            os.write(master, bytes.fromhex("a5 02 01 20 00") + pc_payload +
                     binascii.crc_hqx(pc_payload, 0xffff).to_bytes(2, "little"))
            assert read_exact(master, 5) == bytes.fromhex("a5 02 01 20 00")
            pc_reply = read_exact(master, 34)
            assert pc_reply[:32] == pc_payload
            assert binascii.crc_hqx(pc_reply[:32], 0xffff) == int.from_bytes(pc_reply[32:], "little")
            for console_args, expected_cli in (("activate", "pc_activate"),
                    ("test a", "pc_test a"), ("replace both", "pc_replace both"),
                    ("circulation b", "pc_circulation b"),
                    ("ground precip_rsvb", "pc_onground precip_rsvb"),
                    ("standby", "pc_standby"), ("stop", "pc_stop"),
                    ("reset_error", "pc_reset_error"), ("deactivate", "pc_deactivate")):
                ctl = json.loads(call(path,"--json","pc_ctl",*console_args.split()))
                assert ctl["ok"] and ctl["length"] == 0 and exp_commands[-1] == expected_cli, ctl
            assert not json.loads(call(path,"--json","pc_ctl","ground","unknown"))["ok"]
            ctl_payload = bytes((3, 1)) + bytes(6)
            os.write(master, bytes.fromhex("a5 02 02 08 00") + ctl_payload +
                     binascii.crc_hqx(ctl_payload, 0xffff).to_bytes(2, "little"))
            assert read_exact(master, 5) == bytes.fromhex("a5 02 02 00 00")
            assert read_exact(master, 2) == bytes.fromhex("ff ff")
            pc_status = json.loads(call(path,"--json","pc_stat"))
            assert pc_status["ok"] and pc_status["length"] == 1 and pc_status["payload_hex"] == "29", pc_status
            assert "state=test activated=yes" in pc_status["text"], pc_status
            assert "pc_wire_stat" in exp_commands, exp_commands
            assert not json.loads(call(path,"--json","pc_stat","extra"))["ok"]
            os.write(master, bytes.fromhex("a5 02 00 00 00 ff ff"))
            assert read_exact(master, 5) == bytes.fromhex("a5 02 00 01 00")
            pc_status_frame = read_exact(master, 3)
            assert pc_status_frame[:1] == bytes.fromhex("29")
            assert binascii.crc_hqx(pc_status_frame[:1], 0xffff) == int.from_bytes(pc_status_frame[1:], "little")
            assert "configuration accepted" in call(path, "fld_cfg", "route", "test", "b", "1", "0", "1", "0")
            assert exp_commands[-1] == "pc_test_conf_dir b 1 0 1 0"
            assert "configuration accepted" in call(path, "fld_cfg", "profile", "test", "minflow", "0.5")
            assert exp_commands[-1] == "pc_profile_set test minflow 0.500"
            output = json.loads(call(path, "--json", "fld_cfg", "pump", "detect", "u11"))
            assert output["ok"] and "state=queued request_id=42" in output["text"], output
            assert exp_commands[-1] == "pump_detect"
            output = json.loads(call(path, "--json", "fld_cfg", "pump", "result", "42"))
            assert output["ok"] and "state=done request_id=42" in output["text"], output
            output = json.loads(call(path, "--json", "fld_cfg", "pump", "detect", "u15"))
            assert output["ok"] and "state=queued request_id=43" in output["text"], output
            output = json.loads(call(path, "--json", "fld_cfg", "pump", "result", "43"))
            assert not output["ok"] and "state=io_error" in output["text"] and "driver_error=5" in output["text"], output
            assert not json.loads(call(path, "--json", "fld_cfg", "pump", "detect", "u12"))["ok"]
            assert not json.loads(call(path, "--json", "fld_cfg", "valve", "map", "0", "0", "2", "3"))["ok"]
            output = json.loads(call(path, "--json", "fld_ctl", "valve", "mask", "5"))
            assert output["ok"] and exp_commands[-1] == "pc_onground_control 1 0 1 0", output
            output = json.loads(call(path, "--json", "fld_ctl", "pump", "u11", "frequency", "500"))
            assert output["ok"] and output["length"] == 4 and "id=44" in output["text"] and exp_commands[-1] == "pump_set_freq 500", output
            output = json.loads(call(path, "--json", "fld_ctl", "pump", "u15", "voltage", "2", "100"))
            assert output["ok"] and exp_commands[-1] == "pump4_set_volt 2 100", output
            assert not json.loads(call(path, "--json", "fld_ctl", "pump", "u15", "voltage", "4", "100"))["ok"]
            output = json.loads(call(path, "--json", "fld_stat"))
            assert output["ok"] and output["length"] == 16, output
            assert bytes.fromhex(output["payload_hex"]) == bytes.fromhex("050f4f02f40178900119324b64000000"), output
            assert "fld_wire_stat" in exp_commands, output
            motor = json.loads(call(path,"--json","rba_ctl","move","0","-120","80"))
            assert motor["ok"] and motor["length"] == 4 and "id=71" in motor["text"], motor
            assert exp_commands[-1] == "motor5_move 0 -120 80", exp_commands[-1]
            motor = json.loads(call(path,"--json","rba_ctl","result","71"))
            assert motor["ok"] and motor["length"] == 12 and "state=done position=123" in motor["text"], motor
            assert exp_commands[-1] == "motor5_result 71", exp_commands[-1]
            motor = json.loads(call(path,"--json","rba_ctl","jog","1","ccw"))
            assert motor["ok"] and exp_commands[-1] == "motor5_run 1 ccw 0", motor
            assert json.loads(call(path,"--json","rba_ctl","stop","all"))["ok"]
            assert exp_commands[-1] == "motor5_stop all", exp_commands[-1]
            assert json.loads(call(path,"--json","rba_ctl","stop_hold","0"))["ok"]
            assert exp_commands[-1] == "motor5_stop 0 hold", exp_commands[-1]
            assert not json.loads(call(path,"--json","rba_ctl","stop_hold","all"))["ok"]
            assert json.loads(call(path,"--json","rba_ctl","disable"))["ok"]
            assert exp_commands[-1] == "motor5_disable", exp_commands[-1]
            assert not json.loads(call(path,"--json","rba_ctl","move","2","10"))["ok"]
            assert not json.loads(call(path,"--json","rba_ctl","home","0","20"))["ok"]
            rba_status = json.loads(call(path,"--json","rba_stat"))
            assert rba_status["ok"] and rba_status["length"] == 16, rba_status
            assert "motor0 state=moving position=123" in rba_status["text"], rba_status
            assert "limit0=active" in rba_status["text"] and "limit1=inactive" in rba_status["text"], rba_status
            assert "rba_wire_stat" in exp_commands, exp_commands
            assert not json.loads(call(path,"--json","rba_stat","extra"))["ok"]
            os.write(master, bytes.fromhex("a5 01 1b 00 00 ff ff"))
            assert read_exact(master, 5) == bytes.fromhex("a5 01 1b 10 00")
            rba_frame = read_exact(master, 18)
            assert rba_frame[:16] == bytes.fromhex(rba_status["payload_hex"])
            assert binascii.crc_hqx(rba_frame[:16], 0xffff) == int.from_bytes(rba_frame[16:], "little")
            rba_payload = bytes((2, 0, 80, 0)) + (-120).to_bytes(4, "little", signed=True)
            os.write(master, bytes.fromhex("a5 01 1a 08 00") + rba_payload +
                     binascii.crc_hqx(rba_payload, 0xffff).to_bytes(2, "little"))
            assert read_exact(master, 5) == bytes.fromhex("a5 01 1a 04 00")
            rba_answer = read_exact(master, 6)
            assert rba_answer[:4] == (71).to_bytes(4, "little")
            assert binascii.crc_hqx(rba_answer[:4], 0xffff) == int.from_bytes(rba_answer[4:], "little")
            output = call(path, "thm_cfg", "heater", "0", "set", "max_duty", "60",
                          "slew", "5", "max_on_ms", "30000")
            assert "heater0 max_duty=60% slew=5 pct/s max_on_ms=30000" in output, output
            output = call(path, "thm_cfg", "ir", "0", "set", "max_duty", "10",
                          "max_on_ms", "20", "pwm_hz", "100000", "min_off_ms", "500")
            assert "ir0 max_duty=10% max_on_ms=20 pwm_hz=100000 min_off_ms=500" in output, output
            assert not json.loads(call(path,"--json","thm_cfg","heater","0","set",
                                      "max_duty","60","slew","5","max_on_ms","30001"))["ok"]
            output = call(path,"thm_ctl","heater","0","set","40","5000")
            assert "accepted uptime=" in output, output
            output = wait_cached_text(path,"heater0 running duty=40%","thm_stat")
            assert "heater0 running duty=40%" in output, output
            output = call(path,"thm_ctl","ir","1","set","10")
            assert "accepted uptime=" in output, output
            output = wait_cached_text(path,"ir1 running duty=10%","thm_stat","get")
            assert "ir1 running duty=10%" in output, output
            output = call(path,"thm_ctl","ir","0","feedback_start")
            assert "accepted uptime=" in output, output
            output = json.loads(call(path,"--json","thm_stat","get"))
            assert output["ok"] and output["length"]==6, output
            assert "accepted uptime=" in call(path,"thm_ctl","ir","1","off")
            assert "ir1 stopped duty=0%" in wait_cached_text(path,"ir1 stopped duty=0%","thm_stat")
            assert not json.loads(call(path,"--json","thm_ctl","ir","1","set","10","500"))["ok"]
            assert not json.loads(call(path,"--json","thm_stat","get","ir","1"))["ok"]
            output = json.loads(call(path, "--json", "sen_bme_cfg", "EXP", "get"))
            assert output["ok"] and "temp_os=2" in output["text"], output
            assert exp_commands[-1] == "bme688_cfg_get", exp_commands[-1]
            for field, value in (("duty_pct", "25"), ("burn_time_ms", "100"),
                                 ("max_total_current_ma", "1000")):
                output = json.loads(call(path, "--json", "burn_cfg_set", "0", field, value))
                assert output["ok"] and exp_commands[-1] == f"burn_cfg_set 0 {field} {value}", output
            assert not json.loads(call(path, "--json", "burn_cfg_set", "0", "pulse_ms", "100"))["ok"]
            assert not json.loads(call(path, "--json", "burn_cfg_set", "0", "max_total_current_ma", "3001"))["ok"]
            output = json.loads(call(path, "--json", "sen_bme_cfg", "EXP", "set",
                                     "2x", "4x", "1x", "3"))
            assert output["ok"] and "QUEUED id=17" in output["text"], output
            assert exp_commands[-1] == "bme688_cfg_set 2 3 1 2 0 0 0", exp_commands[-1]
            output = json.loads(call(path, "--json", "sen_bme_cfg", "EXP", "result", "17"))
            assert output["ok"] and "DONE id=17" in output["text"], output
            assert exp_commands[-1] == "bme688_result 17", exp_commands[-1]
            output = json.loads(call(path, "--json", "sen_bme_cfg", "EXP", "set",
                                     "2x", "4x", "1x", "2"))
            assert not output["ok"], output
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "get"))
            assert output["ok"] and "desired xl=5/3" in output["text"], output
            assert exp_commands[-1] == "lsm6d_conf_get", exp_commands[-1]
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "set",
                                     "accel", "208Hz", "8g"))
            assert output["ok"] and "QUEUED id=18" in output["text"], output
            assert exp_commands[-1] == "lsm6d_conf_accel 5 3", exp_commands[-1]
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "set",
                                     "gyro", "104Hz", "2000dps"))
            assert output["ok"] and exp_commands[-1] == "lsm6d_conf_gyro 4 4", output
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "set",
                                     "freefall", "3", "20"))
            assert output["ok"] and exp_commands[-1] == "lsm6d_conf_ff 3 20", output
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "set",
                                     "wakeup", "20", "2"))
            assert output["ok"] and exp_commands[-1] == "lsm6d_conf_wu 20 2", output
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "result", "18"))
            assert output["ok"] and "DONE id=18" in output["text"], output
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "reset"))
            assert output["ok"] and exp_commands[-1] == "lsm6d_init", output
            output = json.loads(call(path, "--json", "sen_imu_cfg", "EXP", "set",
                                     "accel", "208Hz", "2000dps"))
            assert not output["ok"], output
            payload = b"pc_status"
            crc = 0xffff
            for byte in payload:
                crc ^= byte << 8
                for _ in range(8):
                    crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
            frame = b"\xa5\x02\x0c" + len(payload).to_bytes(2, "little")
            frame += payload + crc.to_bytes(2, "little")
            os.write(master, frame)
            header = read_exact(master, 5)
            assert header[:3] == b"\xa5\x02\x0c", header.hex()
            length = int.from_bytes(header[3:5], "little")
            answer = read_exact(master, length + 2)
            assert answer[0] == 0 and b"PC state=IDLE" in answer, answer
            output = call(path, "dat_lst")
            assert "logs=" in output and "images=" in output, output
            output = call(path, "dat_des")
            assert "OK dat_des: uptime=" in output, output
            output = json.loads(call(path, "--json", "ping"))
            assert output["ok"] and output["length"] == 4, output
            output = json.loads(call(path, "--json", "help"))
            assert "tm_obc_get" in output["help"], output
            output = json.loads(call(path, "gui_state"))
            assert output["ok"] and output["obc"] is None and "gpio" in output, output
            output = json.loads(call(path, "gui_shell", "3", "printf gui_ok"))
            assert output["ok"] and output["text"] == "gui_ok", output
            output = json.loads(call(path, "gui_shell", "3", str(FSW), "--gui-hw",
                                     "gpio", "pow_onoff_fpga", "on"))
            assert output["ok"] and "pow_onoff_fpga on" in output["text"], output
            assert (leds / "pow_onoff_fpga/brightness").read_text().strip() == "1"
            pending = subprocess.Popen([str(CLIENT), "--socket", str(path),
                                        "gui_shell", "3", "sleep 1; printf finished"],
                                       stdout=subprocess.PIPE, text=True)
            time.sleep(0.1)
            started = time.monotonic()
            output = json.loads(call(path, "gui_state"))
            assert output["ok"] and time.monotonic() - started < 0.7, output
            assert json.loads(pending.communicate(timeout=3)[0])["text"] == "finished"
            output = call(path, "shell", "printf", "console_ok")
            assert "console_ok" in output and "exit=0" in output, output
            output = json.loads(call(path, "--json", "shell", "printf", "json_ok"))
            assert output["ok"] and "json_ok" in output["text"], output
            output = call(path, "tm_obc_get")
            assert "ERROR" in output, output  # no HK snapshot in the host fixture
            output = call(path, "raw", "0x04", "0x02")
            assert "ERROR" in output, output
            now = int(time.time())
            snapshot.write_text(json.dumps({
                "board": "obc", "schema_version": 1, "online": 1,
                "boot_count": 3, "obc_timestamp": now,
                "temp_valid": 1, "temp_sample_unix_s": now,
                "temp_onboard_c": 25.25, "temp_ntc_c": [12.34, "N/A", 30, 40],
                "bme_valid": 1, "bme_sample_unix_s": now,
                "bme_temp_c": 24, "bme_pressure_pa": 101325,
                "bme_humidity_rh": 48.5, "bme_gas_valid": 1,
                "bme_gas_resistance_ohm": 123456,
                "pac_sample_unix_s": now, "pac_u6_valid": 1, "pac_u2_valid": 0,
                "pwr_rail_names": ["5V_SAMA7", "5V_FPGA", "3V3", "5V_AI",
                                   "PMIC_3V3", "PMIC_DDR_1V35", "PMIC_CORE_1V15",
                                   "PMIC_CPU_1V15"],
                "pwr_volt_v": [5.01, 5, 3.3, 5, 3.3, 0, 0, 0],
                "pwr_curr_a": [-0.125, 0.2, 0.3, 0.4, 0.1, 0, 0, 0],
            }))
            time.sleep(1.2)
            output = call(path, "tm_obc_get")
            assert "onboard_temp=25.25 C" in output and "gas=123456 ohm" in output, output
            output = json.loads(call(path, "--json", "tm_obc_get"))
            assert output["ok"] and output["length"] == 64, output
            sensor = json.loads(call(path, "--json", "sen_stat", "OBC", "get", "bme"))
            assert sensor["ok"] and sensor["length"] == 58, sensor
            values = bytes.fromhex(sensor["payload_hex"])
            assert values[10:14] == b"\x03\x01\x01\x01", values.hex()
            assert int.from_bytes(values[18:22], "little", signed=True) == 2400, values.hex()
            sensor = json.loads(call(path, "--json", "sen_stat", "OBC", "get", "temperature", "ntc"))
            assert sensor["ok"] and sensor["length"] == 58, sensor
            values = bytes.fromhex(sensor["payload_hex"])
            assert values[22:26] == b"\x02\x02\x10\x01", values.hex()
            output = json.loads(call(path, "gui_state"))
            assert output["obc"]["temp_onboard_c"] == 25.25, output
            if GUI.exists():
                gui_env = dict(os.environ, OBC_GUI_FLIGHT_SOCKET=str(path))
                screen = subprocess.check_output([str(GUI), "status"], env=gui_env,
                                                 text=True, timeout=5)
                assert "FLIGHT SOFTWARE TELEMETRY" in screen and "25.2 C" in screen, screen

            console_master, console_slave = pty.openpty()
            console = subprocess.Popen([str(CLIENT), "--socket", str(path)],
                                       stdin=console_slave, stdout=console_slave,
                                       stderr=console_slave)
            try:
                read_until(console_master, b"flight> ")
                os.write(console_master, b"ping\r")
                read_response(console_master, b"OK ping:")
                os.write(console_master, b"sys_info\r")
                read_response(console_master, b"OK sys_info:")
                os.write(console_master, b"\x1b[A\r")
                read_response(console_master, b"OK sys_info:")
                os.write(console_master, b"\x1b[A\x1b[A\r")
                read_response(console_master, b"OK ping:")
                os.write(console_master, b"quit\r")
                console.wait(timeout=3)
            finally:
                if console.poll() is None:
                    console.terminate()
                    console.wait(timeout=3)
                os.close(console_master)
                os.close(console_slave)
        finally:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=5)
            os.close(master)
            os.close(slave)
            os.close(exp_master)
            os.close(exp_slave)
            peer.join(timeout=1)
    print("Flight console socket, text/JSON, shell, and dispatch: OK")


if __name__ == "__main__":
    main()
