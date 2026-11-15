import pathlib,subprocess,base64,json
v=pathlib.Path(__file__).resolve().parent
manifest=json.loads((v/'update-1080p/manifest.json').read_text())
code=f'''import pathlib,subprocess,json,hashlib,time,urllib.request
root=pathlib.Path('/media/fat/emulationstation')
manifest={manifest!r}
for _ in range(60):
 time.sleep(.5)
 if (root/'logs/es-startup.log').exists() and '] ready' in (root/'logs/es-startup.log').read_text(): break
pids=subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE,text=True).stdout.split()
assert len(pids)==1,'Expected exactly one installed frontend'
pid=pids[0];proc=pathlib.Path('/proc')/pid
assert (proc/'exe').resolve()==root/'emulationstation'
actual={{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in manifest}}
assert actual==manifest,'Installed hash mismatch'
environment=dict(item.split('=',1) for item in (proc/'environ').read_bytes().decode().split('\\0') if '=' in item)
assert environment['ES_SOFT_STRIP']=='8' and environment['ES_SOFT_PLD']=='256'
assert environment['ES_SOFT_FPS']=='60'
assert not any(environment.get(k) for k in ('ES_SOFT_TRACE','ES_SOFT_BENCH','ES_PROFILE','ES_SOFT_SLOW_DUMP','ES_SOFT_DIRTY_CHECK','ES_SOFT_SCRIPT'))
with urllib.request.urlopen('http://127.0.0.1:1234/systems',timeout=5) as api: systems=api.read().decode()
startup=(root/'logs/es-startup.log').read_text()
assert '] ready' in startup
fb=pathlib.Path('/sys/class/graphics/fb0/virtual_size').read_text().strip()
assert fb=='1920,1080',fb
record=dict(pid=int(pid),framebuffer=fb,core=pathlib.Path('/tmp/CORENAME').read_text().strip(),sha256=actual,
 settings=(root/'es-mister.ini').read_text(),startup_last_lines=startup.splitlines()[-12:],
 process_status=[line for line in (proc/'status').read_text().splitlines() if line.startswith(('VmRSS:','VmHWM:','Threads:'))],
 systems_api_bytes=len(systems),backups=sorted(str(p) for p in (root/'backups').glob('codex-1080p-*')))
print(json.dumps(record,indent=2))
'''
data=base64.b64encode(code.encode()).decode()
p=subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'],stdout=subprocess.PIPE,text=True)
print(p.stdout)
if p.returncode:raise SystemExit(p.returncode)
record=json.loads(p.stdout)
(v/'deployment-verification.json').write_text(json.dumps(record,indent=2))
