#!/usr/bin/env python3
"""Exercise a LOCAL Worker. Never reads or modifies the deployed portal."""
import hashlib
import http.cookiejar
import json
import pathlib
import urllib.error
import urllib.request

base = 'http://localhost:8788'
root = pathlib.Path(__file__).resolve().parents[1]
secrets = {}
for line in (root/'portal/.dev.vars').read_text().splitlines():
    if '=' in line and not line.lstrip().startswith('#'):
        k,v=line.split('=',1); secrets[k.strip()]=v.strip().strip('"').strip("'")
jar=http.cookiejar.CookieJar()
client=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
def req(path, method='GET', data=None, device=False, expect=200):
    headers={}
    if device: headers['Authorization']='Bearer '+secrets['DEVICE_TOKEN']
    if isinstance(data, dict): headers['Content-Type']='application/json'; data=json.dumps(data).encode()
    request=urllib.request.Request(base+path, data=data, headers=headers, method=method)
    try:
        with client.open(request, timeout=30) as r: status=r.status; raw=r.read(); hdr=r.headers
    except urllib.error.HTTPError as e: status=e.code; raw=e.read(); hdr=e.headers
    assert status == expect, (path,status,raw[:200])
    return raw,hdr
req('/api/management',expect=401)
req('/api/device/management',expect=401)
req('/api/login','POST',{'password':secrets['ADMIN_PASSWORD']})
req('/api/management/settings','PUT',{'interval':300})
req('/api/management/settings','PUT',{'interval':1},expect=400)
raw,hdr=req('/api/device/management',device=True)
assert hdr['cache-control']=='no-store'
image=bytearray(256);image[0]=0xe9;image[12]=9;image[32:36]=b'\x32\x54\xcd\xab'
raw,_=req('/api/management/firmware','PUT',bytes(image));release=json.loads(raw)
assert release['release']==hashlib.sha256(image).hexdigest()
raw,_=req('/api/management');assert json.loads(raw)['approved'] is None
req('/api/management/install','POST',{'release':'wrong'},expect=400)
req('/api/management/install','POST',release)
raw,_=req('/api/device/firmware/'+release['release'],device=True);assert raw==image
req('/api/device/diagnostics','PUT',{'firmware':'test','rssi':-90,'token':'must-drop'},device=True)
req('/api/device/diagnostics','PUT',{},device=True,expect=429)
raw,_=req('/api/management');snapshot=json.loads(raw)['diagnostics'];assert 'token' not in snapshot and snapshot['at']>0
req('/api/management/install','DELETE')
req('/api/management/settings','PUT',{'interval':3600})
print('Local HTTP: auth gates, interval validation, uncached protobuf, upload/approval/download, diagnostics, rate guard and cancellation passed.')
