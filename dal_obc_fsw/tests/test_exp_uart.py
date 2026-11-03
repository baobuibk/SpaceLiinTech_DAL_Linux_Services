#!/usr/bin/env python3
"""Exercise the EXP UART CLI and schema-2 chunk path using a PTY."""
import json
import os
import pathlib
import pty
import subprocess
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = ROOT / "build-host/test_exp_uart_probe"
PROMPT = b"DEBUG@MCU:~ $ "


def exchange(mode, argument, responder):
    master, slave = pty.openpty()
    commands = []

    def peer():
        pending = b""
        try:
            while True:
                pending += os.read(master, 512)
                while b"\r" in pending:
                    raw, pending = pending.split(b"\r", 1)
                    command = raw.decode()
                    commands.append(command)
                    response = responder(command)
                    os.write(master, raw + b"\r\n" + response.encode() + b"\r\n" + PROMPT)
        except OSError:
            pass

    thread = threading.Thread(target=peer, daemon=True)
    thread.start()
    try:
        env = dict(os.environ, DAL_EXP_UART_PORT=os.ttyname(slave),
                   DAL_EXP_UART_BAUD="115200")
        result = subprocess.run([str(PROBE), mode, argument], env=env,
                                text=True, capture_output=True, timeout=5, check=True)
        return result.stdout, commands
    finally:
        os.close(slave)
        os.close(master)
        thread.join(timeout=1)


def main():
    text = json.dumps({"schema_version": 2, "section": "sensors",
                       "data": {"note": "quote \" and slash \\",
                                "samples": list(range(30))}}, separators=(",", ":"))
    chunks = [text[i:i + 64] for i in range(0, len(text), 64)]

    def telemetry(command):
        if command == "tlm_json sensors":
            return json.dumps({"schema_version": 2, "status": "CHUNK_REQUIRED",
                               "report_id": 7, "total": len(text), "next": 0})
        _, report_id, offset = command.split()
        assert report_id == "7"
        offset = int(offset)
        chunk = text[offset:offset + 64]
        return json.dumps({"report_id": 7, "offset": offset,
                           "next": offset + len(chunk), "total": len(text),
                           "data": chunk})

    output, commands = exchange("tlm", "sensors", telemetry)
    status, body = output.split("\n", 1)
    assert status == "0", output
    assert json.loads(body) == json.loads(text), output
    assert commands == ["tlm_json sensors"] + [
        f"json_chunk 7 {i * 64}" for i in range(len(chunks))]

    output, commands = exchange("run", "motor5_status all",
                                lambda _: "MOTOR0 STOPPED position=10")
    assert output.startswith("0\n"), output
    assert "MOTOR0 STOPPED" in output
    assert commands == ["motor5_status all"]
    print("EXP UART command and chunked telemetry: OK")


if __name__ == "__main__":
    main()
