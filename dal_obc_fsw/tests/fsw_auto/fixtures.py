"""Start the host build of dal_obc_fsw fully sandboxed, wired to PTYs and temp dirs.

Safety: every path the daemon can touch is redirected into a temp dir, and a stub
`systemctl` is put first on PATH. `debug_handoff` runs `systemctl start
serial-getty@ttyS1.service`; without the stub that would act on the build host.
"""
import json
import math
import os
import pathlib
import pty
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import unittest

from . import transport
from .fake_pdu import FakePdu

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_DATA_DIR = "/tmp/DAL_DATA_TEST_DIR"
FSW = ROOT / "build-host/dal_obc_fsw"
CLIENT = ROOT / "build-host/dal_flight_console"
PROMPT = b"DEBUG@MCU:~ $ "


class FakeExp:
    """EXP UART0 CLI peer. `responder(command) -> str`; every command is logged."""

    def __init__(self, master_fd, responder=None):
        self.fd = master_fd
        self.responder = responder or (lambda command: "OK")
        self.commands = []
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        pending = b""
        try:
            while True:
                pending += os.read(self.fd, 512)
                while b"\r" in pending:
                    raw, pending = pending.split(b"\r", 1)
                    command = raw.decode(errors="replace")
                    self.commands.append(command)
                    os.write(self.fd, raw + b"\r\n" + self.responder(command).encode() +
                             b"\r\n" + PROMPT)
        except OSError:
            pass


def base_snapshot(now=None, **overrides):
    """A fully valid OBC housekeeping snapshot (schema 1)."""
    now = int(now or time.time())
    snap = {
        "board": "obc", "schema_version": 1, "online": 1, "boot_count": 3,
        "obc_timestamp": now,
        "temp_valid": 1, "temp_sample_unix_s": now, "temp_onboard_c": 25.25,
        "temp_ntc_c": [12.34, 30.5, 30, 40],
        "bme_valid": 1, "bme_sample_unix_s": now, "bme_temp_c": 24.0,
        "bme_pressure_pa": 101325, "bme_humidity_rh": 48.5,
        "bme_gas_valid": 1, "bme_gas_resistance_ohm": 123456,
        "pac_sample_unix_s": now, "pac_u6_valid": 1, "pac_u2_valid": 1,
        "pwr_rail_names": ["5V_SAMA7", "5V_FPGA", "3V3", "5V_AI",
                           "PMIC_3V3", "PMIC_DDR_1V35", "PMIC_CORE_1V15", "PMIC_CPU_1V15"],
        "pwr_volt_v": [5.01, 5.0, 3.3, 5.0, 3.3, 1.35, 1.15, 1.15],
        "pwr_curr_a": [-0.125, 0.2, 0.3, 0.4, 0.1, 0.2, 0.3, 0.4],
    }
    snap.update(overrides)
    return snap


def _round(x):
    return int(math.copysign(math.floor(abs(x) + 0.5), x))


def _f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def expected_obc_wire(snap):
    """Reference model of the 64-byte tm_obc_get payload (docs/flight-software.md)."""
    valid = 0
    t_ok, b_ok = bool(snap["temp_valid"]), bool(snap["bme_valid"])
    ntc = [None if v == "N/A" else _f32(v) for v in snap["temp_ntc_c"]]
    ntc_ok = [t_ok and v is not None and v >= -40.0 for v in ntc]
    valid |= int(t_ok)
    for i, ok in enumerate(ntc_ok):
        valid |= int(ok) << (i + 1)
    gas_ok = b_ok and bool(snap["bme_gas_valid"])
    valid |= int(b_ok) << 5 | int(gas_ok) << 6
    pac = [bool(snap["pac_u6_valid"]), bool(snap["pac_u2_valid"])]
    valid |= int(pac[0]) << 7 | int(pac[1]) << 8
    volts = [_round(_f32(v) * 1000) if pac[i // 4] else 0
             for i, v in enumerate(snap["pwr_volt_v"])]
    amps = [_round(_f32(v) * 1000) if pac[i // 4] else 0
            for i, v in enumerate(snap["pwr_curr_a"])]
    return struct.pack(
        "<HIIh4hhIHI8H8h", valid, snap["obc_timestamp"], snap["boot_count"],
        _round(_f32(snap["temp_onboard_c"]) * 100) if t_ok else 0,
        *[_round(v * 100) if ok else 0 for v, ok in zip(ntc, ntc_ok)],
        _round(_f32(snap["bme_temp_c"]) * 100) if b_ok else 0,
        _round(snap["bme_pressure_pa"]) if b_ok else 0,
        _round(_f32(snap["bme_humidity_rh"]) * 100) if b_ok else 0,
        _round(snap["bme_gas_resistance_ohm"]) if gas_ok else 0,
        *volts, *amps)


def _reset_data_dir(path):
    """Empty the file-backend test dir. Refuses any directory not named DAL_DATA_TEST*,
    so a mistyped FSW_DATA_DIR can never wipe real data."""
    path = pathlib.Path(path)
    if not path.name.startswith("DAL_DATA_TEST"):
        raise RuntimeError(f"refusing to manage {path}: directory name must start with DAL_DATA_TEST")
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True)
    return path


class Fsw:
    """One sandboxed daemon instance. Use as a context manager.

    The ICD file backend (dat_*) is redirected with DAL_DATA_DIR to $FSW_DATA_DIR, default
    /tmp/DAL_DATA_TEST_DIR, emptied at start and at stop. Production default is unchanged
    (/home/bee/data).
    """

    def __init__(self, exp_responder=None, with_snapshot=True, extra_env=None,
                 fsw_args=()):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="fsw-auto-"))
        self.log = []
        self.exp_responder = exp_responder
        self.with_snapshot = with_snapshot
        self.extra_env = extra_env or {}
        self.fsw_args = tuple(fsw_args)
        self.proc = None
        self.data_dir = pathlib.Path(os.environ.get("FSW_DATA_DIR", DEFAULT_DATA_DIR))

    # -- paths -------------------------------------------------------------
    @property
    def snapshot_path(self):
        return self.tmp / "obc_data.json"

    @property
    def socket_path(self):
        return self.tmp / "console.sock"

    @property
    def systemctl_log(self):
        return self.tmp / "systemctl.log"

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        for binary in (FSW, CLIENT):
            if not binary.exists():
                raise unittest.SkipTest(f"{binary} missing: run `make build-host/dal_obc_fsw "
                                        f"build-host/dal_flight_console` in service/dal_obc_fsw")
        self.rs_master, rs_slave = pty.openpty()
        exp_master, exp_slave = pty.openpty()
        pdu_master, pdu_slave = pty.openpty()
        self._slaves = [rs_slave, exp_slave, pdu_slave]
        self._masters = [self.rs_master, exp_master, pdu_master]
        self.exp = FakeExp(exp_master, self.exp_responder)
        self.pdu = FakePdu(pdu_master)  # Modbus slave on the PDU port; pdu.online=False silences it

        bindir = self.tmp / "bin"
        bindir.mkdir()
        stub = bindir / "systemctl"
        stub.write_text(f'#!/bin/sh\necho "$@" >> "{self.systemctl_log}"\nexit 0\n')
        stub.chmod(0o755)
        uvc = self.tmp / "fake_uvc.py"
        uvc.write_text("#!/usr/bin/env python3\nimport os, sys\nfrom pathlib import Path\n"
                       "profile = Path(os.environ['DAL_UVC_PROFILE_DIR']) / f'camera{sys.argv[1]}.bin'\n"
                       "if sys.argv[2] == '-o':\n"
                       "    Path(sys.argv[3]).write_bytes(b'\\xff\\xd8test\\xff\\xd9')\n"
                       "elif sys.argv[2] == '--uvc-set':\n"
                       "    data = bytes.fromhex(sys.argv[3])\n"
                       "    profile.write_bytes(data)\n"
                       "    print(data.hex())\n"
                       "else:\n"
                       "    print(profile.read_bytes().hex() if profile.exists() else "
                       "(bytes((int(sys.argv[1]), 1)) + bytes(30)).hex())\n")
        uvc.chmod(0o755)
        leds = self.tmp / "leds"
        leds.mkdir()
        for name in ("pow_onoff_ai", "pow_onoff_fpga", "usb_1_pwr_en", "usb_2_pwr_en",
                     "usb_3_pwr_en", "usb_4_pwr_en", "pow_mipicam_1", "pow_mipicam_2",
                     "en_i2c_mipicam_1", "en_i2c_mipicam_2", "sel_mipicam_on2_off1"):
            (leds / name).mkdir()
            (leds / name / "brightness").write_text("0\n")
        self.leds = leds

        env = dict(os.environ)
        env.pop("DAL_EXP_UART_POLL", None)
        env.pop("DAL_OBC_RS422_SOCKET", None)
        env.pop("DAL_OBC_VERBOSE", None)
        env.pop("DAL_OBC_QUIET_HOUSEKEEPING", None)
        env.pop("DAL_OBC_RTC_SIMULATE", None)
        _reset_data_dir(self.data_dir)
        env.update({
            "DAL_DATA_DIR": str(self.data_dir),
            "PATH": f"{bindir}:{env.get('PATH', '')}",
            "DAL_OBC_RS422_PORT": os.ttyname(rs_slave),
            "DAL_EXP_UART_PORT": os.ttyname(exp_slave),
            "DAL_EXP_UART_BAUD": "115200",
            "DAL_EXP_DATA_ENABLE": "0",
            "DAL_GUI_PDU_PORT": os.ttyname(pdu_slave),
            "DAL_FLIGHT_CONSOLE_SOCKET": str(self.socket_path),
            "DAL_OBC_HK_SNAPSHOT_PATH": str(self.snapshot_path),
            "DAL_EXP_HK_SNAPSHOT_PATH": str(self.tmp / "exp_data.json"),
            "DAL_EXP_CSP_ENABLE": "0",
            "DAL_OBC_HK_COLLECTOR_ENABLE": "0",
            "DAL_USB_CAM_TOOL": str(uvc),
            "DAL_UVC_DATA_DIR": str(self.tmp / "uvc"),
            "DAL_UVC_PROFILE_DIR": str(self.tmp),
            "DAL_GUI_LED_ROOT": str(leds),
            "DAL_GUI_FPGA_LOCK": str(self.tmp / "fpga.lock"),
        })
        env.update(self.extra_env)
        if self.with_snapshot:
            self.write_snapshot()
        self.proc = subprocess.Popen([str(FSW), *self.fsw_args], env=env, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, errors="replace")
        threading.Thread(target=self._pump, daemon=True).start()
        deadline = time.monotonic() + 10
        while not self.socket_path.exists():
            if self.proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("dal_obc_fsw did not come up:\n" + self.log_tail(40))
            time.sleep(0.05)
        self.link = transport.PtyLink(self.rs_master)
        self.console = transport.Console(CLIENT, self.socket_path)
        self.link.txrx(0, 0)  # RS-422 reader is running
        return self

    def _pump(self):
        for line in self.proc.stdout:
            self.log.append(line.rstrip("\n"))

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def log_tail(self, n=25):
        return "\n".join(self.log[-n:])

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        for fd in getattr(self, "_masters", []) + getattr(self, "_slaves", []):
            try:
                os.close(fd)
            except OSError:
                pass
        shutil.rmtree(self.tmp, ignore_errors=True)
        if self.data_dir.name.startswith("DAL_DATA_TEST"):
            shutil.rmtree(self.data_dir, ignore_errors=True)

    __enter__ = start

    def __exit__(self, *exc):
        self.stop()

    # -- housekeeping snapshot ---------------------------------------------
    def write_snapshot(self, snap=None, **overrides):
        snap = snap if snap is not None else base_snapshot(**overrides)
        tmp = self.snapshot_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(snap))
        os.replace(tmp, self.snapshot_path)  # atomic: the daemon never sees a partial file
        return snap

    def remove_snapshot(self):
        self.snapshot_path.unlink(missing_ok=True)

    def wait_tm_obc(self, want_ack=True, timeout=5.0):
        """Poll tm_obc_get (HK cache refreshes once per second) until it ACKs / errors."""
        deadline = time.monotonic() + timeout
        while True:
            tm = self.link.txrx(4, 2)
            if tm.is_err != want_ack:
                return tm
            if time.monotonic() > deadline:
                raise AssertionError(f"tm_obc_get did not become {'valid' if want_ack else 'invalid'}: {tm!r}")
            time.sleep(0.2)


class FswTestCase(unittest.TestCase):
    """Shares one daemon per test class; checks it is still alive after every case."""

    fsw = None
    exp_responder = None
    with_snapshot = True

    @classmethod
    def setUpClass(cls):
        cls.fsw = Fsw(cls.exp_responder, cls.with_snapshot).start()
        cls.link = cls.fsw.link

    @classmethod
    def tearDownClass(cls):
        if cls.fsw:
            cls.fsw.stop()

    def setUp(self):
        self.link.drain()

    def run(self, result=None):
        before = len(getattr(result, "failures", [])) + len(getattr(result, "errors", []))
        super().run(result)
        failed = len(getattr(result, "failures", [])) + len(getattr(result, "errors", [])) > before
        if failed and self.fsw:
            print(f"\n--- dal_obc_fsw log tail ({self.id()}) ---\n{self.fsw.log_tail()}\n---")
        return result

    def tearDown(self):
        self.assertTrue(self.fsw.alive(), "dal_obc_fsw died during the case\n" + self.fsw.log_tail())
