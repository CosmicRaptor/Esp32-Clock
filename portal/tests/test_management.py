import hashlib
import os
import sys
import unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))
import management
import clock_pb2 as pb
from model import Invalid
from test_wire import nanopb_size

class ManagementTests(unittest.TestCase):
    def test_intervals(self):
        for n in management.INTERVALS:
            self.assertEqual(management.settings({'interval': n}), {'interval': n})
        for value in ([], None, {'interval': True}, {'interval': 1}, {'interval': '300'}):
            with self.assertRaises(Invalid): management.settings(value)

    def test_firmware_header_hash_and_bounds(self):
        data = bytearray(256); data[0] = 0xe9; data[12] = 9; data[32:36] = b'\x32\x54\xcd\xab'
        r = management.firmware(bytes(data))
        self.assertEqual(r, {'release': hashlib.sha256(data).hexdigest(), 'size': 256})
        for bad in (b'', bytes(256), bytes(data[:255]), bytes(data[:12]+b'\x00'+data[13:]), bytes(data)+bytes(management.MAX_FIRMWARE)):
            with self.assertRaises(Invalid): management.firmware(bad)

    def test_diagnostics_drop_secrets_and_validate(self):
        r = management.report({'firmware': '0.9.0', 'rssi': -93, 'token': 'secret'})
        self.assertNotIn('token', r)
        self.assertEqual(r['rssi'], -93)
        for bad in ([], {'uptime': True}, {'heap': -2**40}, {'firmware': 'a\nb'}, {'running': 'x'*65}):
            with self.assertRaises(Invalid): management.report(bad)

    def test_management_wire_fits_fixed_buffer(self):
        msg = pb.Management(diagnostic_interval=21600, release='a'*64, size=management.MAX_FIRMWARE)
        self.assertLessEqual(len(msg.SerializeToString()), nanopb_size('Management'))
        self.assertLessEqual(nanopb_size('Management'), 128)

    def test_report_rate_guard_and_update_transitions(self):
        old = dict(management.report({'firmware': '0.9.0'}), at=1000)
        same = management.report({'firmware': '0.9.0', 'heap': 100})
        trial = management.report({'firmware': '0.9.1', 'ota_state': 1})
        self.assertFalse(management.report_due(old, same, 1299))
        self.assertTrue(management.report_due(old, same, 1300))
        self.assertFalse(management.report_due(old, trial, 1059))
        self.assertTrue(management.report_due(old, trial, 1060))
        self.assertTrue(management.report_due(dict(trial, at=1000), dict(trial, ota_state=0), 1060))
        self.assertTrue(management.report_due(None, same, 1000))
