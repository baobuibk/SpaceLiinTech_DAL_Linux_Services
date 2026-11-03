"""CSP loss falls back to validated UART param_show, then reports offline."""

import json
import time

from fsw_auto.fixtures import Fsw
from test_exp_csp import ICD_ROWS, IDENTITY_ROWS, INVENTORY_ROWS, THERMAL_ROWS, ROWS


def cli_table(table, rows):
    lines = [f"=== Table {table}: DAL ===", "    Rows: 1 | Mem: 1 bytes | CRC: 0x0000", "---"]
    for row in rows.splitlines():
        _, name, kind, values = row.split(maxsplit=3)
        parts = [values] if kind == "STR" else values.split()
        for index, value in enumerate(parts):
            suffix = f"[{index}]" if len(parts) > 1 else ""
            lines.append(f"  {name:<13}{suffix:<6} = {value}")
    lines.extend(["===================", ""])
    return "\n".join(lines)


def wait_until(predicate, seconds, message):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError(message)


def main():
    fsw = Fsw()
    uart_down = fsw.tmp / "uart_down"
    can_up = fsw.tmp / "can_up"
    calls = fsw.tmp / "calls"
    tables = {0: IDENTITY_ROWS, 2: ROWS, 3: THERMAL_ROWS, 6: INVENTORY_ROWS, 12: ICD_ROWS}

    def respond(command):
        if not command.startswith("param_show ") or uart_down.exists():
            return "Error: parameter table is busy"
        table = int(command.split()[1])
        rows = tables[table]
        if table == 12:
            tick = int(time.monotonic() * 1000) & 0xffffffff
            rows = rows.replace("0x0002 seq U32 52", f"0x0002 seq U32 {tick}")
            rows = rows.replace("0x0006 tick_ms U32 124100", f"0x0006 tick_ms U32 {tick}")
            rows = rows.replace("124000 124000 124000 124000", " ".join([str(tick)] * 4))
        return cli_table(table, rows)

    fsw.exp_responder = respond
    client = fsw.tmp / "fake-lite-client"
    client.write_text(
        "#!/usr/bin/env python3\n"
        "import os,sys\n"
        f"UP={str(can_up)!r}\nCALLS={str(calls)!r}\n"
        "mode='rs485' if any(a.startswith('-k') for a in sys.argv) else 'can'\n"
        "verb=next(a for a in sys.argv if a in ('ping','getall','setval','getval','run'))\n"
        "with open(CALLS,'a') as f:f.write(mode+' '+verb+'\\n')\n"
        "if mode=='rs485' or not os.path.exists(UP):sys.exit(1)\n"
        "if verb=='ping':print('reply in 1 ms')\n"
        "elif verb=='getall':\n"
        f"    rows={{'0':{IDENTITY_ROWS!r},'2':{ROWS!r},'3':{THERMAL_ROWS!r},'6':{INVENTORY_ROWS!r},'12':{ICD_ROWS!r}}}\n"
        "    print(rows[sys.argv[-1]])\n"
        "else:print('ok')\n")
    client.chmod(0o755)
    fsw.extra_env = {"DAL_EXP_CSP_ENABLE": "1", "DAL_EXP_CSP_CLIENT": str(client)}
    try:
        fsw.start()

        def online():
            return "EXP online" in fsw.console.run("sys_info")

        try:
            wait_until(online, 10, "UART param_show did not restore EXP identity")
        except AssertionError as exc:
            raise AssertionError(f"{exc}; commands={fsw.exp.commands}; log={fsw.log_tail(20)}") from exc
        def fresh_table(table):
            result = json.loads(fsw.console.run("--json", "exp_csp_snapshot", str(table)))
            return json.loads(result["text"])["fresh"]

        for table in (0, 2, 3, 6, 12):
            wait_until(lambda table=table: fresh_table(table), 8,
                       f"UART table {table} unavailable; commands={fsw.exp.commands}; log={fsw.log_tail(20)}")
        assert "param_show 2" in fsw.exp.commands
        assert "param_show 12" in fsw.exp.commands
        assert "param_show 3" in fsw.exp.commands
        request = json.loads(fsw.console.run("--json", "exp_csp_set", "5", "apply_profile", "128"))
        ticket = int.from_bytes(bytes.fromhex(request["payload_hex"]), "little")

        def rejected():
            result = json.loads(fsw.console.run("--json", "exp_csp_result", str(ticket)))
            return "read-only" in result.get("text", "")

        wait_until(rejected, 3, "CSP write was not rejected on UART fallback")
        uart_down.touch()
        wait_until(lambda: "EXP offline" in fsw.console.run("sys_info"), 5,
                   "EXP was not marked offline after UART failed")
        snapshot = json.loads((fsw.tmp / "exp_data.json").read_text())
        assert snapshot["online"] == 0 and snapshot["data_valid"] == 0, snapshot
        uart_down.unlink()
        can_up.touch()
        wait_until(lambda: any("CAN recovered" in line for line in fsw.log), 15,
                   "CAN recovery was not detected from UART mode")
        print("CSP/UART fallback, offline state, and CAN recovery: OK")
    finally:
        fsw.stop()


if __name__ == "__main__":
    main()
