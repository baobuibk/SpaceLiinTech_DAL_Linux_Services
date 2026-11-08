"""Power-aware PDU reads: never let an unpowered TPS discard independent telemetry."""
import json
import struct
import time
from fsw_auto.fixtures import FswTestCase


class PduPowerStatus(FswTestCase):
    def setUp(self):
        super().setUp()
        self.fsw.pdu.reset()
        self.fsw.pdu.online=True
        with self.fsw.pdu.lock:
            self.fsw.pdu.requests.clear()

    def snapshot(self):
        p = self.link.txrx(1, 4, b"\x00", timeout=10).expect_ack(1, 4, 184)
        self.assertEqual(p[:2], b"\x00\x02")
        return p, struct.unpack_from("<HHBBhIHH", p, len(p)-16)

    def wait_status(self, predicate):
        deadline = time.monotonic()+13
        while time.monotonic()<deadline:
            p, status=self.snapshot()
            if predicate(status):
                return p, status
            time.sleep(.1)
        self.fail(f"PDU state not reached: {status}")

    def test_trans_off_skips_tps_and_preserves_input_status(self):
        self.fsw.pdu.fail_tps_read=True
        p, state=self.wait_status(lambda x: x[3]==1 and x[0]&4)
        self.assertEqual(state[4],251)
        self.assertTrue(state[0]&8)
        self.assertFalse(state[0]&(16|32))
        self.assertFalse(any(f==1 and o==0 for f,o,n in self.fsw.pdu.requests))
        self.assertEqual(self.fsw.pdu.writes(), [])
        errors=self.link.txrx(1,4,b"\x03").expect_ack(1,4)
        descriptors=[struct.unpack_from("<H",errors,at)[0]&127 for at in range(6,len(errors)-16,6)]
        self.assertNotIn(0x50,descriptors)  # Normal unpowered VOUT is not a control failure.
        fields=json.loads(self.fsw.console.run("gui_state"))["pdu"]
        self.assertIsNotNone(fields)
        self.assertIsNone(fields["vout_on"])
        self.assertEqual(fields["board_temp_c"],25.1)
        reply=json.loads(self.fsw.console.run("--json","pdu_vout","get"))
        self.assertTrue(reply["ok"])
        self.assertNotIn("flight error",reply["text"])

    def test_trans_on_tps_error_keeps_temperature_and_rails(self):
        self.fsw.pdu.trans_en=True
        self.fsw.pdu.fail_tps_read=True
        p,state=self.wait_status(lambda x:x[3]==4)
        self.assertTrue(state[0]&4)
        self.assertTrue(state[0]&64)
        self.assertFalse(state[0]&16)
        self.fsw.pdu.fail_tps_read=False
        p,state=self.wait_status(lambda x:x[3]==3)
        self.assertTrue(state[0]&16)

    def test_partial_feedback_and_offline_do_not_invent_validity(self):
        self.fsw.pdu.fail_input_read=True
        p,state=self.wait_status(lambda x:x[0]==8 and x[3]==1)
        self.assertEqual(state[4],-32768)
        self.fsw.pdu.online=False
        p,state=self.wait_status(lambda x:x[0]==0)
        self.assertEqual(state[3],0)
        self.assertEqual(state[5],0xFFFFFFFF)
