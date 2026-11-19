#!/usr/bin/env python3
"""Detached ECDSA P-256/SHA-256 signatures. Requires the OpenSSL CLI, no Python packages."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_HEADER = ROOT / 'firmware/BhoomiClock/ota_signing_key.h'
VERSION_RE = r'(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})'


def version(value):
    if not isinstance(value, str) or not re.fullmatch(VERSION_RE, value):
        raise ValueError('Expected a canonical major.minor.patch version.')
    parts = tuple(map(int, value.split('.')))
    if max(parts) > 0xffffffff:
        raise ValueError('Version component too large.')
    return parts


def manifest(v, digest, size):
    version(v)
    if not re.fullmatch('[0-9a-f]{64}', digest) or not 256 <= size <= 6291456:
        raise ValueError('Invalid application digest or size.')
    return f'bhoomi-clock-ota-v1\n{v}\n{digest}\n{size}\n'.encode('ascii')


def openssl(*args, data=None):
    result = subprocess.run(['openssl', *map(str, args)], input=data, capture_output=True)
    if result.returncode:
        raise RuntimeError('OpenSSL operation failed: ' + str(args[0]))
    return result.stdout


def public_key():
    text = PUBLIC_HEADER.read_text()
    match = re.search(r'R"KEY\((-----BEGIN PUBLIC KEY-----.*?-----END PUBLIC KEY-----\n)\)KEY"', text, re.S)
    if not match:
        raise ValueError('OTA public key has not been provisioned.')
    return match.group(1).encode('ascii')


def sign(image, v, key):
    digest = hashlib.sha256(image).hexdigest()
    payload = manifest(v, digest, len(image))
    signature = openssl('dgst', '-sha256', '-sign', key, data=payload)
    # Refuse a private key that does not match the trust anchor in this checkout.
    with tempfile.TemporaryDirectory() as tmp:
        pub, sig = Path(tmp) / 'public.pem', Path(tmp) / 'signature.der'
        pub.write_bytes(public_key()); sig.write_bytes(signature)
        openssl('dgst', '-sha256', '-verify', pub, '-signature', sig, data=payload)
    return {'version': v, 'release': digest, 'size': len(image), 'signature': signature.hex()}


def keygen(private_path):
    # Never replace a key accidentally; key rotation must preserve the existing trust chain.
    if private_path.exists() or 'BEGIN PUBLIC KEY' in PUBLIC_HEADER.read_text():
        raise ValueError('A signing key already exists. Rotate deliberately; do not overwrite it.')
    private_path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with private_path.open('xb') as stream:
        private_path.chmod(0o600)
        stream.write(openssl('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256'))
    pub = openssl('pkey', '-in', private_path, '-pubout').decode('ascii')
    PUBLIC_HEADER.write_text('#pragma once\n\n// Public trust anchor; never put the private signing key here.\n'
                             'constexpr const char OTA_SIGNING_PUBLIC_KEY[] = R"KEY(' + pub + ')KEY";\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    gen = sub.add_parser('keygen')
    gen.add_argument('--private-key', type=Path, default=ROOT / '.ota-secrets/signing.pem')
    signing = sub.add_parser('sign')
    signing.add_argument('image', type=Path)
    signing.add_argument('--version', required=True)
    signing.add_argument('--private-key', type=Path, required=True)
    signing.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'keygen':
        keygen(args.private_key)
        print('Created private key (mode 600) and public firmware trust anchor. Back up the private key securely.')
    else:
        args.output.write_text(json.dumps(sign(args.image.read_bytes(), args.version, args.private_key), indent=2) + '\n')


if __name__ == '__main__':
    main()
