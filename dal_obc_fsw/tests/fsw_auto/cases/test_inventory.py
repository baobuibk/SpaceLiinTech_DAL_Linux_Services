"""Drift detection between the daemon source, the command inventory and `help`."""
import re

from fsw_auto import fixtures
from fsw_auto.fixtures import FswTestCase
from fsw_auto.inventory import CONSOLE_ONLY, IMPLEMENTED

SOURCE = fixtures.ROOT / "src/services/bee_dal_tmtc_service.c"


class SourceDrift(FswTestCase):
    def test_inv_01_dispatch_table_matches_inventory(self):
        text = SOURCE.read_text()
        body = text[text.index("static void dispatch("):text.index("static double elapsed_ms_since")]
        sys_cfg = body[body.index("case BEE_DAL_TYPE_SYS_CFG"):body.index("case BEE_DAL_TYPE_DATA_XFER")]
        found = {(1, int(a, 16)) for a in re.findall(r"case (0x[0-9a-fA-F]+):\s*handle_", sys_cfg)}
        exp = body[body.index("case BEE_DAL_TYPE_EXP_CTRL"):body.index("case BEE_DAL_TYPE_SFU")]
        found |= {(2, int(a, 16)) for a in re.findall(r"f->addr\s*==\s*(0x[0-9a-fA-F]+)", exp)}
        found |= {(2, 0x0A), (2, 0x0B), (2, 0x0C)} if "BEE_DAL_EXP_PING" in exp else set()
        protocol = (fixtures.ROOT / "lib/bee_dal_proto/include/bee_dal_proto.h").read_text()
        for name, value in re.findall(r"#define (BEE_DAL_EXP_CSP_\w+) (0x[0-9A-Fa-f]+)U", protocol):
            if re.search(r"f->addr\s*==\s*" + name + r"\b", exp):
                found.add((2, int(value, 16)))
        have = {k for k in IMPLEMENTED if k[0] in (1, 2)}
        self.assertEqual(sorted(found - have), [], "dispatched in source but missing from inventory.py "
                         "(add tests and update docs/flight-command-register.md)")
        self.assertEqual(sorted(have - found), [], "in inventory.py but not dispatched in source")

    def test_inv_02_help_lists_every_console_command(self):
        text = self.fsw.console.run("help")
        # `help` does not list itself, and pwr_adj_cfg is an undocumented alias of pdu_vout.
        names = (set(IMPLEMENTED.values()) | set(CONSOLE_ONLY) | {"sen_bme_cfg", "sen_imu_cfg"}) - {"help"}
        missing = sorted(n for n in names if not re.search(rf"\b{re.escape(n)}\b", text))
        self.assertEqual(missing, [], "commands implemented but absent from `help`")
