"""CAN to RS485 KISS failover, command routing, and CAN recovery."""

import json
import time

from fsw_auto.fixtures import Fsw
from test_exp_csp import ICD_ROWS, IDENTITY_ROWS, INVENTORY_ROWS, THERMAL_ROWS, ROWS


def wait_until(predicate, seconds, message):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError(message)


def main():
    fsw = Fsw()
    client = fsw.tmp / "fake-lite-client"
    can_down = fsw.tmp / "can_down"
    calls = fsw.tmp / "transport_calls"
    can_down.touch()
    client.write_text(
        "#!/usr/bin/env python3\n"
        "import os, sys, time\n"
        f"ROWS = {ROWS!r}\n"
        f"INVENTORY = {INVENTORY_ROWS!r}\n"
        f"ICD = {ICD_ROWS!r}\n"
        f"THERMAL = {THERMAL_ROWS!r}\n"
        f"IDENTITY = {IDENTITY_ROWS!r}\n"
        f"CAN_DOWN = {str(can_down)!r}\n"
        f"CALLS = {str(calls)!r}\n"
        "verb_index = next(i for i, a in enumerate(sys.argv) if a in ('ping', 'getall', 'getval', 'setval', 'run'))\n"
        "opts = sys.argv[1:verb_index]\n"
        "serial = any(a.startswith('-k') for a in opts)\n"
        "if serial and ('-a2' not in opts or not any(a.startswith('-k/dev/ttyS2,speed=115200') for a in opts)): sys.exit(2)\n"
        "mode = 'rs485' if serial else 'can'\n"
        "verb = sys.argv[verb_index]\n"
        "with open(CALLS, 'a') as f: f.write(mode + ' ' + ' '.join(sys.argv[verb_index:]) + '\\n')\n"
        "if mode == 'can' and os.path.exists(CAN_DOWN): sys.exit(1)\n"
        "if verb == 'ping': print('reply in 1 ms')\n"
        "elif verb == 'getall':\n"
        "    table = sys.argv[verb_index + 2]\n"
        "    if table == '0': print(IDENTITY)\n"
        "    elif table == '2': print(ROWS)\n"
        "    elif table == '3': print(THERMAL)\n"
        "    elif table == '6': print(INVENTORY)\n"
        "    elif table == '12':\n"
        "        tick = int(time.monotonic() * 1000) & 0xffffffff\n"
        "        rows = ICD.splitlines()\n"
        "        for i, row in enumerate(rows):\n"
        "            if row.startswith('0x0002 seq '): rows[i] = f'0x0002 seq U32 {tick}'\n"
        "            elif row.startswith('0x0006 tick_ms '): rows[i] = f'0x0006 tick_ms U32 {tick}'\n"
        "            elif row.startswith('0x0010 group_tick_ms '): rows[i] = '0x0010 group_tick_ms U32 ' + ' '.join([str(tick)] * 4)\n"
        "        print('\\n'.join(rows))\n"
        "    else: sys.exit(3)\n"
        "elif verb == 'setval': print('set accepted')\n"
        "elif verb == 'getval': print('value 128')\n"
        "elif verb == 'run': print('command accepted')\n"
        "else: sys.exit(4)\n")
    client.chmod(0o755)
    fsw.extra_env = {
        "DAL_EXP_CSP_ENABLE": "1", "DAL_EXP_CSP_CLIENT": str(client),
        "DAL_EXP_CSP_RS485_ENABLE": "1", "DAL_EXP_CSP_RS485_PORT": "/dev/ttyS2",
    }
    try:
        fsw.start()

        def fresh_identity():
            response = json.loads(fsw.console.run("--json", "exp_csp_snapshot", "0"))
            return json.loads(response["text"])["fresh"]

        wait_until(fresh_identity, 8, "RS485 did not restore EXP identity")
        assert "EXP online hardware=1.1.0" in fsw.console.run("sys_info")
        lines = calls.read_text().splitlines()
        assert sum(line.startswith("can ping") for line in lines) >= 2, lines
        assert any(line.startswith("rs485 getall 11 12") for line in lines), lines
        assert any(line.startswith("rs485 getall 11 3") for line in lines), lines

        request = json.loads(fsw.console.run("--json", "exp_csp_set", "5", "apply_profile", "128"))
        assert request["ok"], request
        ticket = int.from_bytes(bytes.fromhex(request["payload_hex"]), "little")

        def done():
            response = json.loads(fsw.console.run("--json", "exp_csp_result", str(ticket)))
            return response["ok"] and "set accepted" in response.get("text", "")

        wait_until(done, 4, "RS485 write did not complete")
        assert sum(line.startswith("rs485 setval") for line in calls.read_text().splitlines()) == 1
        can_down.unlink()
        wait_until(lambda: any("CAN recovered" in line for line in fsw.log), 15,
                   "CAN recovery was not detected")
        request = json.loads(fsw.console.run("--json", "exp_csp_set", "5", "apply_profile", "128"))
        ticket = int.from_bytes(bytes.fromhex(request["payload_hex"]), "little")
        wait_until(done, 4, "CAN write did not complete after recovery")
        assert any(line.startswith("can setval") for line in calls.read_text().splitlines())
        print("CSP CAN/RS485 failover and recovery: OK")
    finally:
        fsw.stop()


if __name__ == "__main__":
    main()
