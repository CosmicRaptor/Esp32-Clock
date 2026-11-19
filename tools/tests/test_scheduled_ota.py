import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ota_signing as signing
import scheduled_ota as ota


class ReleaseTests(unittest.TestCase):
    def test_monotonic_versions_across_usb_and_scheduled_releases(self):
        self.assertEqual(ota.next_version('0.11.1', '0.11.4', '0.11.2'), '0.11.5')
        self.assertEqual(ota.next_version('0.12.0', '0.11.4', '0.11.2'), '0.12.1')
        self.assertEqual(ota.next_version('0.11.1', running='0.13.5'), '0.13.6')
        for bad in ('1.2', '01.2.3', '1.2.3\n', '1.2.4294967296'):
            with self.assertRaises(ValueError): signing.version(bad)

    def test_change_detection_uses_published_tag_not_previous_run(self):
        with patch.object(ota, 'git', return_value=''):
            self.assertFalse(ota.changed('clock-ota/0.11.2'))
            self.assertTrue(ota.changed(None))
        with patch.object(ota, 'git', return_value='firmware/BhoomiClock/ui.cpp') as git:
            self.assertTrue(ota.changed('clock-ota/0.11.2'))
            self.assertIn('clock-ota/0.11.2', git.call_args.args)

    def test_unchanged_source_does_not_contact_portal(self):
        with patch.object(ota, 'latest_tag', return_value='clock-ota/0.11.2'), \
             patch.object(ota, 'changed', return_value=False), \
             patch.object(ota, 'connection') as connection, patch.object(ota, 'output') as output:
            ota.prepare()
            connection.assert_not_called()
            output.assert_called_once_with(changed='false')

    def test_unsigned_clock_blocks_release_before_version_changes(self):
        with patch.object(ota, 'latest_tag', return_value=None), \
             patch.object(ota, 'public_key', return_value=b'public key'), \
             patch.object(ota, 'connection', return_value=lambda _: {'diagnostics': {'firmware': '0.11.1'}}), \
             patch.object(ota, 'HEADER') as header:
            with self.assertRaisesRegex(ValueError, 'Activate signing'):
                ota.prepare()
            header.write_text.assert_not_called()

    def test_signatures_reject_tampered_hash_version_size_and_wrong_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            key, other, header = root/'key.pem', root/'other.pem', root/'key.h'
            header.write_text('constexpr const char OTA_SIGNING_PUBLIC_KEY[] = "";')
            with patch.object(signing, 'PUBLIC_HEADER', header):
                signing.keygen(key)
                self.assertEqual(key.stat().st_mode & 0o777, 0o600)
                with self.assertRaises(ValueError): signing.keygen(key)
                image = bytes(256)
                metadata = signing.sign(image, '0.11.2', key)
                pub, sig = root/'public.pem', root/'sig.der'
                pub.write_bytes(signing.public_key())
                sig.write_bytes(bytes.fromhex(metadata['signature']))
                payload = signing.manifest('0.11.2', hashlib.sha256(image).hexdigest(), 256)
                signing.openssl('dgst', '-sha256', '-verify', pub, '-signature', sig, data=payload)
                for tampered in (
                    signing.manifest('0.11.3', metadata['release'], 256),
                    signing.manifest('0.11.2', 'a'*64, 256),
                    signing.manifest('0.11.2', metadata['release'], 257),
                    payload.replace(b'bhoomi-clock', b'another-product'),
                ):
                    with self.assertRaises(RuntimeError):
                        signing.openssl('dgst', '-sha256', '-verify', pub, '-signature', sig, data=tampered)
                other.write_bytes(signing.openssl('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256'))
                with self.assertRaises(RuntimeError): signing.sign(image, '0.11.2', other)

    def test_no_tag_on_failed_approval(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'build').mkdir()
            (root/'build/BhoomiClock.ino.bin').write_bytes(bytes(256))
            data = {'version': '0.11.2', 'release': 'a'*64, 'size': 256, 'signature': '00'*70}
            def request(path, *args):
                if path == '/api/management/install': raise SystemExit('HTTP 500')
                return data
            with patch.object(ota, 'ROOT', root), patch.object(ota, 'sign', return_value=data), \
                 patch.object(ota, 'connection', return_value=request), \
                 patch.dict(ota.os.environ, OTA_SIGNING_PRIVATE_KEY='test'), \
                 patch.object(ota.time, 'sleep'), patch.object(ota.subprocess, 'run') as run:
                with self.assertRaises(SystemExit): ota.publish('0.11.2')
                run.assert_not_called()


if __name__ == '__main__': unittest.main()
