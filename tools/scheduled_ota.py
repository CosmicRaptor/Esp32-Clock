#!/usr/bin/env python3
"""Prepare a new source release, then sign, upload, approve and record it."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from firmware_release import connection
from ota_signing import ROOT, public_key, sign, version

PREFIX = 'clock-ota/'
INPUTS = ['firmware', 'proto', 'build.sh', 'tools/ci_dependencies.sh',
          'tools/scheduled_ota.py', 'tools/ota_signing.py', 'tools/firmware_release.py',
          '.github/workflows/ota.yml']
HEADER = ROOT / 'firmware/BhoomiClock/portal_client.h'


def git(*args, **kwargs):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True, **kwargs).strip()


def latest_tag():
    tags = git('tag', '--list', PREFIX + '*').splitlines()
    return max(tags, key=lambda tag: version(tag[len(PREFIX):])) if tags else None


def changed(tag):
    return tag is None or bool(git('diff', '--name-only', tag, 'HEAD', '--', *INPUTS))


def next_version(source, published=None, running=None):
    versions = [version(v) for v in (source, published, running) if v]
    major, minor, patch = max(versions)
    result = f'{major}.{minor}.{patch + 1}'
    version(result)
    return result


def output(**values):
    for key, value in values.items():
        print(f'{key}={value}')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
            for key, value in values.items():
                stream.write(f'{key}={value}\n')


def prepare():
    tag = latest_tag()
    if not changed(tag):
        output(changed='false')
        return
    trusted_key = public_key()  # Fail closed until the public trust anchor is committed.
    request = connection()
    status = request('/api/management')
    if (status.get('diagnostics') or {}).get('signing_key') != hashlib.sha256(trusted_key).hexdigest():
        raise ValueError('Activate signing on the clock first: its reported public key does not match this checkout.')
    current = re.search(r'FW_VERSION = "([^"]+)"', HEADER.read_text()).group(1)
    v = next_version(current, tag[len(PREFIX):] if tag else None,
                     (status.get('diagnostics') or {}).get('firmware'))
    HEADER.write_text(HEADER.read_text().replace(f'FW_VERSION = "{current}"', f'FW_VERSION = "{v}"'))
    output(changed='true', version=v, source=git('rev-parse', 'HEAD'))


def config():
    keys = ('PORTAL_URL', 'PORTAL_DEVICE_TOKEN', 'PORTAL_ACCESS_CLIENT_ID', 'PORTAL_ACCESS_CLIENT_SECRET')
    if not all(os.environ.get(k) for k in keys):
        raise ValueError('All portal build credentials are required.')
    path = ROOT / 'firmware/BhoomiClock/portal_config.h'
    with path.open('w') as stream:
        path.chmod(0o600)
        stream.write('#pragma once\n' + ''.join(f'#define {k} {json.dumps(os.environ[k])}\n' for k in keys))


def retry(operation):
    # Cloudflare KV is eventually consistent; retries reuse the exact signed image.
    for attempt in range(7):
        try:
            return operation()
        except SystemExit:
            if attempt == 6:
                raise
            time.sleep(15)


def publish(v):
    image = (ROOT / 'build/BhoomiClock.ino.bin').read_bytes()
    # Never persist the private signing key to the checkout or build artifacts.
    with tempfile.TemporaryDirectory(prefix='clock-signing-') as tmp:
        key = Path(tmp) / 'private.pem'
        with key.open('w') as stream:
            key.chmod(0o600)
            stream.write(os.environ['OTA_SIGNING_PRIVATE_KEY'])
        metadata = sign(image, v, key)
    request = connection()
    uploaded = retry(lambda: request('/api/management/firmware', 'PUT', image))
    if uploaded.get('release') != metadata['release'] or uploaded.get('size') != len(image):
        raise ValueError('Portal upload digest/size does not match the signed image.')
    sidecar = {k: metadata[k] for k in ('version', 'signature')}
    retry(lambda: request('/api/management/firmware/' + metadata['release'] + '/signature', 'PUT', sidecar))
    approved = retry(lambda: request('/api/management/install', 'POST', {'release': metadata['release']}))
    if approved.get('release') != metadata['release']:
        raise ValueError('Portal did not approve the signed release.')
    tag = PREFIX + v
    # Record only after upload + signature storage + install approval all succeed.
    subprocess.run(['git', '-c', 'user.name=Clock OTA', '-c', 'user.email=clock-ota@users.noreply.github.com',
                    'tag', '-a', tag, '-m', json.dumps(metadata, sort_keys=True)], cwd=ROOT, check=True)
    env = os.environ.copy()
    token = base64.b64encode(('x-access-token:' + env['GITHUB_TOKEN']).encode()).decode()
    env.update(GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='http.https://github.com/.extraheader',
               GIT_CONFIG_VALUE_0='AUTHORIZATION: basic ' + token)
    subprocess.run(['git', 'push', 'https://github.com/' + env['GITHUB_REPOSITORY'] + '.git',
                    'refs/tags/' + tag], cwd=ROOT, env=env, check=True)
    summary = (f'Firmware {v}: {len(image):,} bytes of the 6 MB slot.\n'
               f'Source: {git("rev-parse", "HEAD")}\nSHA-256: {metadata["release"]}\n'
               'Signature uploaded; automatic installation queued. Device success is reported separately in the portal.\n')
    print(summary)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream:
            stream.write(summary)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'config', 'publish'])
    parser.add_argument('--version')
    args = parser.parse_args()
    if args.action == 'prepare':
        prepare()
    elif args.action == 'config':
        config()
    else:
        version(args.version)
        publish(args.version)
