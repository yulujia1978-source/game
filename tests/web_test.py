#!/usr/bin/env python3
import json, subprocess, tempfile, urllib.request, urllib.error, urllib.parse, socket, time
from pathlib import Path
binary=str(Path('web_host').resolve())
with tempfile.TemporaryDirectory(prefix='ps5-turbo-web-') as d:
    p=subprocess.Popen([binary],cwd=d,stdout=subprocess.PIPE,text=True)
    try:
        port=int(p.stdout.readline());base=f'http://127.0.0.1:{port}'
        def get(path):
            with urllib.request.urlopen(base+path,timeout=2) as r:return r.read()
        state=json.loads(get('/api/state'))
        assert state['mask']==0 and state['enabled']==1 and state['shortcuts']==1
        page=get('/').decode();assert '<footer>snkwz</footer>' in page and page.count('class="key"')==8
        assert '20 次/秒' in page and '<select id="speed">' not in page
        def post(fields):
            data=urllib.parse.urlencode(fields).encode()
            req=urllib.request.Request(base+'/api/settings',data=data)
            try:
                with urllib.request.urlopen(req,timeout=2) as r:return r.status,r.read()
            except urllib.error.HTTPError as e:return e.code,e.read()
        for bit in (0x4000,0x2000,0x8000,0x1000,0x400,0x800,0x100,0x200,0xff00,0):
            assert post(dict(enabled=1,mask=bit,half_us=25000,shortcuts=1,token=state['token']))[0]==200
            assert json.loads(get('/api/state'))['mask']==bit
            assert f'mask=0x{bit:04x}' in (Path(d)/'.web-tmp/settings-v2.txt').read_text()
        assert post(dict(enabled=1,mask=16384,half_us=25000,shortcuts=1,token='wrong'))[0]==400
        assert post(dict(enabled=1,mask=2,half_us=25000,shortcuts=1,token=state['token']))[0]==400
        assert json.loads(get('/api/state'))['mask']==0
        assert post(dict(enabled=1,mask=16384,half_us=12500,shortcuts=1,token=state['token']))[0]==400
        assert post(dict(enabled=1,mask=16384,half_us=25000,shortcuts=1,token=state['token']))[0]==200
        fast=json.loads(get('/api/state'))
        assert fast['mask']==16384 and fast['half_us']==25000
        assert 'half_us=25000' in (Path(d)/'.web-tmp/settings-v2.txt').read_text()
        assert page.count('row shortcut')==2
        for shortcuts in (0,1,2,3,0):
            assert post(dict(enabled=1,mask=0,half_us=25000,shortcuts=shortcuts,token=state['token']))[0]==200
            assert json.loads(get('/api/state'))['shortcuts']==shortcuts
            assert f'shortcuts={shortcuts}' in (Path(d)/'.web-tmp/settings-v2.txt').read_text()
        assert post(dict(enabled=1,mask=0,half_us=25000,shortcuts=4,token=state['token']))[0]==400
        assert json.loads(get('/api/state'))['shortcuts']==0
        # Incomplete requests time out, allowing the service loop to continue.
        s=socket.create_connection(('127.0.0.1',port));s.sendall(b'POST /api/settings HTTP/1.1\r\nContent-Length: 20\r\n\r\nx')
        start=time.monotonic();s.settimeout(1);assert b'400' in s.recv(512);s.close()
        assert time.monotonic()-start<0.8
        assert json.loads(get('/api/state'))['mask']==0
        p.terminate();p.wait(timeout=3)
        p=subprocess.Popen([binary],cwd=d,stdout=subprocess.PIPE,text=True)
        port=int(p.stdout.readline());base=f'http://127.0.0.1:{port}'
        assert json.loads(get('/api/state'))['shortcuts']==0 # survives service restart
        print('PASS: real HTTP page/state, all 8 button saves, persistence, clear-all, rejected writes, slow-client timeout')
    finally:
        p.terminate();p.wait(timeout=3)
