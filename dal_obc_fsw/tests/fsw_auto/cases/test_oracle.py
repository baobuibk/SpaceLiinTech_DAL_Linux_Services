"""Cross-check the fakes and expectations against the real firmware sources in service/ref.

Skipped when the reference checkout is absent. Purpose: if the EXP or PDU firmware changes a reply
format that dal_obc_fsw parses, this fails here before a fake silently drifts from the hardware.
"""
import pathlib
import re
import unittest

from fsw_auto import fixtures

REF = fixtures.ROOT.parent / "ref"
EXP_SRC = next(iter(REF.glob("*DAL_EXP*FW/src")), None)
PDU_DOC = next(iter(REF.glob("SpaceLiinTech_DAL_PDU_AVR_FW*/doc/docs/modbus_implementation.md")), None)


def corpus(root, patterns=("*.c", "*.h")):
    text = []
    for pattern in patterns:
        for f in root.rglob(pattern):
            if "TinyUSB" in f.parts or "ThirdParty" in f.parts:
                continue
            try:
                text.append(f.read_text(errors="replace"))
            except OSError:
                pass
    return "\n".join(text)


@unittest.skipUnless(EXP_SRC, "EXP firmware reference checkout not present")
class ExpFirmware(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = corpus(EXP_SRC)

    def test_orc_01_reply_literals_the_obc_matches_on(self):
        literals = [
            "DAL_EXP firmware v", " build ", "requested=", "observed=UNKNOWN", "REFUSED",
            "Power %s %s: %s", "PC_STAT ", "OK pc_activate", "OK pc_deactivate",
            "pc already activated", "pc already deactivated", "pc test sent", "pc replace sent",
            "pc circulation sent", "pc onground preset sent", "pc standby sent", "pc stop sent",
            "pc reset_error sent", "pc onground_control sent", "THP_CFG ", "THM_CTL ", "THM_STAT6 ",
            "THM_CFG ", "FLD_STAT ", "RBA_STAT ", "Pump request QUEUED id=", "OK pc_valve_conf_pulse",
            "ERROR THP_CFG invalid record", "ERROR THP_CFG refused", "THP_CTL ",
            "ERROR THP_CTL invalid selector", "ERROR THP_CTL refused",
            "ERROR THM_CTL INVALID", "ERROR THM_CTL REFUSED", "ERROR thm_cfg: use thm_cfg [64 hex digits]",
            "ERROR thm_cfg: EXP service refused setting", "ERROR THM_STAT UNAVAILABLE", "Pump type: ",
            "Pump type unconfigured", "Pump result replaced or unknown", "Pump enabled", "Pump4 enabled",
            "Pump value set", "STOP_REQUESTED", "DISABLE_REQUESTED", "request=%lu position=%ld",
            "NOT_FOUND", "NOT_READY",
        ]
        missing = [s for s in literals if s not in self.src]
        self.assertEqual(missing, [], "reply text the OBC depends on no longer appears in the EXP firmware")

    def test_orc_02_wire_commands_registered(self):
        names = ["thm_wire_ctl", "thm_wire_stat", "rba_wire_stat", "fld_wire_stat", "pc_wire_stat",
                 "thp_wire_cfg", "thp_wire_ctl", "power_all_get", "power_exp12v_on", "power_sln_hd_off", "power_motor_on",
                 "cfg_json_pc", "json_chunk", "thm_cfg", "pc_onground_control", "pump_enable", "pump_disable",
                 "pump4_enable", "pump4_disable", "pump_set_freq", "pump4_set_freq", "pump_set_volt",
                 "pump4_set_volt", "pump_type_set", "pump_detect", "pump4_detect", "pump_result",
                 "motor", "motor5_move", "motor5_goto", "motor5_run", "motor5_home", "motor5_stop",
                 "motor5_result", "version", "tlm_json"]
        missing = [n for n in names if f'"{n}"' not in self.src]
        self.assertEqual(missing, [], "EXP CLI command used by dal_obc_fsw is not registered")

    def test_orc_03_obc_commands_exist_in_the_daemon(self):
        daemon = (fixtures.ROOT / "src/services/bee_dal_tmtc_service.c").read_text()
        exp_cmds = set(re.findall(r'(?:"|snprintf\(command,[^"]*")((?:pc|thp|thm|fld|rba|pump|motor)[a-z0-9_]*)', daemon))
        registered = set(re.findall(r'"((?:pc|thp|thm|fld|rba|pump|pump4|motor5?)_[a-z0-9_]+)"', self.src))
        unknown = sorted(c for c in exp_cmds if c not in registered and not c.endswith("_") and
                         f'"{c}"' not in self.src)
        self.assertEqual(unknown, [], "dal_obc_fsw sends an EXP command the firmware does not register")

    def test_orc_04_firmware_limits_used_by_the_model(self):
        self.assertRegex(self.src, r"define IR_HARD_MAX_DUTY_PCT\s+25U")
        self.assertRegex(self.src, r"define PROFILE_NTC_COUNT\s+4u")
        self.assertRegex(self.src, r"define PROFILE_MAX_SLEW_PCT_PER_S\s+100u")
        self.assertIn("sample_timeout_ms>p->sample_period_ms", self.src.replace(" ", ""))


    def test_orc_05_motor_rules_used_by_the_model(self):
        """Constants behind the motor5 fake and the OBC's 200 sps / 5-state checks (see D21)."""
        self.assertRegex(self.src, r"define MOTOR5_MAX_SPS\s+1000U")
        self.assertRegex(self.src, r"MOTOR5_STOPPED, MOTOR5_MOVING, MOTOR5_RUNNING, MOTOR5_HOMING, MOTOR5_HOLDING, MOTOR5_ERROR")
        self.assertIn("actual_sps>255u ? 255u : m->actual_sps", self.src.replace(" ", " "))
        self.assertIn("held_move=!home && !jog && m->state==MOTOR5_HOLDING", self.src)
        self.assertRegex(self.src, r"finish\(c,id,\"move complete\",true\)")
        self.assertRegex(self.src, r"finish\(c,id,\"homed\",false\)")


@unittest.skipUnless(PDU_DOC, "PDU firmware reference checkout not present")
class PduFirmware(unittest.TestCase):
    def test_orc_10_register_map_used_by_the_obc(self):
        doc = PDU_DOC.read_text()
        for row in ("| `00001` | `VOUT_ENABLE`", "| `00002` | `TRANS_BRANCH_ENABLE`", "| `40001` | `VOUT_SETPOINT_MV`",
                    "| `40100`–`40101` | `PRODUCT_CODE`", "| `40102` | `FW_VERSION`", "| `40103` | `FW_PATCH`",
                    "| `40107`–`40108` | `FW_BUILD`", "| Input register | `30023` |", "| Input register | `30024` |",
                    "| Input register | `30025` |", "| Input register | `30026` |", "| Input register | `30028` |",
                    "| Input register | `30030` |", "| Input register | `30021` |", "| Discrete input | `10011` |"):
            self.assertIn(row, doc, f"PDU Modbus map no longer lists {row!r}")

    def test_orc_11_fake_follows_the_documented_rules(self):
        doc = PDU_DOC.read_text()
        self.assertIn("Enabling powers TRANS on first", doc)
        self.assertIn("Disabling TRANS also requests VOUT off", doc)
        self.assertIn("requires a setpoint programmed", doc)
        self.assertIn("A write powers TRANS on first", doc)
