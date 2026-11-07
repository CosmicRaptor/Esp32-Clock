#!/usr/bin/env python3
"""Publish from this checkout without printing credentials. Upload alone never installs."""
import argparse
import subprocess
import tempfile
import json
from pathlib import Path
import re
import urllib.error
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
FIRMWARE=ROOT/'firmware'/'BhoomiClock'

def connection():
    config=dict(re.findall(r'^#define\s+(PORTAL_\w+)\s+"([^"]*)"', (FIRMWARE/'portal_config.h').read_text(), re.M))
    local={}
    for line in (ROOT/'portal/.dev.vars').read_text().splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            k,v=line.split('=',1);local[k.strip()]=v.strip().strip('"').strip("'")
    base=config['PORTAL_URL'].rstrip('/')
    if not base.startswith('https://'): raise SystemExit('Release publishing requires HTTPS.')
    storage=tempfile.TemporaryDirectory(prefix='clock-release-')
    cookie=Path(storage.name)/'cookies'
    headers={'CF-Access-Client-Id':config.get('PORTAL_ACCESS_CLIENT_ID',''), 'CF-Access-Client-Secret':config.get('PORTAL_ACCESS_CLIENT_SECRET','')}
    def request(path, method='GET', data=None):
        # curl uses the same trusted networking path as wrangler on this Mac.
        h=dict(headers)
        if isinstance(data,dict): data=json.dumps(data).encode();h['Content-Type']='application/json'
        elif isinstance(data,bytes): h['Content-Type']='application/octet-stream'
        lines=['url = '+json.dumps(base+path), 'silent', 'show-error', 'max-time = 90',
               'request = '+json.dumps(method), 'cookie = '+json.dumps(str(cookie)),
               'cookie-jar = '+json.dumps(str(cookie)), 'write-out = "\\n%{http_code}"']
        for key,value in h.items(): lines.append('header = '+json.dumps(key+': '+value))
        if data is not None:
            payload=Path(storage.name)/'payload';payload.write_bytes(data);payload.chmod(0o600)
            lines.append('data-binary = '+json.dumps('@'+str(payload)))
        result=subprocess.run(['curl','--config','-'],input=('\n'.join(lines)+'\n').encode(),capture_output=True)
        raw,_,code=result.stdout.rpartition(b'\n')
        if result.returncode or code != b'200':
            raise SystemExit('Portal request failed: HTTP '+code.decode(errors='replace'))
        return json.loads(raw)
    request('/api/login','POST',{'password':local['ADMIN_PASSWORD']})
    return request

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['status','upload','install','cancel','interval'])
    p.add_argument('value',nargs='?')
    args=p.parse_args();request=connection()
    if args.action=='upload':
        image=Path(args.value or ROOT/'build/BhoomiClock.ino.bin').read_bytes()
        print(json.dumps(request('/api/management/firmware','PUT',image)))
    elif args.action=='install':
        if not args.value or not re.fullmatch('[0-9a-f]{64}',args.value):p.error('install requires the uploaded SHA-256')
        print(json.dumps(request('/api/management/install','POST',{'release':args.value})))
    elif args.action=='cancel': print(json.dumps(request('/api/management/install','DELETE')))
    elif args.action=='interval': print(json.dumps(request('/api/management/settings','PUT',{'interval':int(args.value)})))
    else: print(json.dumps(request('/api/management'),indent=2))

if __name__=='__main__': main()
