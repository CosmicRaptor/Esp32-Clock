#!/usr/bin/env python3
"""Provision this clock repository's Actions secrets without displaying their values."""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = 'CosmicRaptor/Esp32-Clock'


def main():
    config = dict(re.findall(r'^#define\s+(PORTAL_\w+)\s+"([^"]*)"',
                  (ROOT / 'firmware/BhoomiClock/portal_config.h').read_text(), re.M))
    local = {}
    for line in (ROOT / 'portal/.dev.vars').read_text().splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            key, value = line.split('=', 1)
            local[key.strip()] = value.strip().strip('"').strip("'")
    secrets = {key: config[key] for key in (
        'PORTAL_URL', 'PORTAL_DEVICE_TOKEN', 'PORTAL_ACCESS_CLIENT_ID', 'PORTAL_ACCESS_CLIENT_SECRET')}
    secrets['ADMIN_PASSWORD'] = local['ADMIN_PASSWORD']
    secrets['OTA_SIGNING_PRIVATE_KEY'] = (ROOT / '.ota-secrets/signing.pem').read_text()
    if not all(secrets.values()):
        raise SystemExit('Missing local credentials; no secrets were changed.')
    for key, value in secrets.items():
        result = subprocess.run(['gh', 'secret', 'set', key, '--repo', REPO],
                                input=value.encode(), capture_output=True)
        if result.returncode:
            raise SystemExit('Failed to configure Actions secret: ' + key)
        print('Configured ' + key)


if __name__ == '__main__':
    main()
