"""Install only this task's deterministic fixture; refuse concurrent benchmarks."""
import json,pathlib,subprocess
v=pathlib.Path(__file__).resolve().parent
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe';scp='/mnt/c/Windows/System32/OpenSSH/scp.exe'
key=r'C:\Users\USER\.ssh\id_ed25519_mister';host='root@192.168.15.14'
base='/media/fat/emulationstation/codex-performance-20261113'
def remote(code):subprocess.run([ssh,'-T','-i',key,'-o','BatchMode=yes','-o','ConnectTimeout=8',host,'python3 -'],input=code,text=True,check=True)
remote(f"""import pathlib,os
p=pathlib.Path({base!r})
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
for entry in pathlib.Path('/proc').iterdir():
 if not entry.name.isdigit():continue
 try:exe=(entry/'exe').resolve(strict=True)
 except OSError:continue
 assert exe.parent!=p/'bin','A benchmark is running; do not copy a fixture now'
assert not (p/'cover-fixture').exists(),'Fixture already installed: verify before reusing'
s=os.statvfs(p);assert s.f_bavail*s.f_frsize>450*1024*1024,'Insufficient SD space'
""")
archive=v/'cover-fixture.tar'
win=subprocess.check_output(['wslpath','-w',str(archive)],text=True).strip()
subprocess.run([scp,'-i',key,'-o','BatchMode=yes',win,host+':'+base+'/cover-fixture.tar'],check=True)
manifest=json.loads((v/'cover-fixture-manifest.json').read_text())
remote(f"""import pathlib,tarfile,hashlib,json
base=pathlib.Path({base!r});dest=base/'cover-fixture'
assert not dest.exists()
with tarfile.open(base/'cover-fixture.tar') as t:
 for m in t.getmembers():
  target=(base/m.name).resolve()
  assert target==dest or dest in target.parents,'Archive escapes fixture directory'
  assert m.isfile() or m.isdir(),'Unexpected archive entry'
 t.extractall(base)
expected={manifest!r}
actual={{p.relative_to(dest).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.rglob('*') if p.is_file()}}
assert actual==expected,'Fixture hash mismatch'
(base/'cover-fixture-manifest.json').write_text(json.dumps(actual,indent=2))
print('Isolated fixture verified:',len(actual),'files')
""")
