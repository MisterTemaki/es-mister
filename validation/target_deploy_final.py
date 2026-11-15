"""Deploy only the reviewed final update and record verified readiness/backup."""
import hashlib,json,pathlib,subprocess,tarfile
V=pathlib.Path(__file__).resolve().parent;A=V.parent
SSH='/mnt/c/Windows/System32/OpenSSH/ssh.exe';SCP='/mnt/c/Windows/System32/OpenSSH/scp.exe'
KEY=r'C:\Users\USER\.ssh\id_ed25519_mister';HOST='root@192.168.15.14'
BASE='/media/fat/emulationstation/codex-performance-20261113'
required=('final-navigation-current-v1-summary.json','final-controls-v1-summary.json','final-idle-v1-summary.json','final-animations-v1-transitions.json','startup-final-conditions.json','startup-full-warm-v1-summary.json','startup-full-cold-v1-summary.json')
assert all((V/n).exists() for n in required),'Acceptance is incomplete'
assert (V/'target-results/final-core-v1/lifecycle-summary.json').exists(),'Core lifecycle is not verified'
assert (V/'target-results/final-core-speed-v2/lifecycle-summary.json').exists(),'Normal core return is not measured'
assert json.loads((V/'target-results/final-core-speed-v2/lifecycle-summary.json').read_text())['useful_control_actions_verified'],'Post-core controls were not verified'
nav=json.loads((V/'final-navigation-current-v1-summary.json').read_text())
assert len(nav['runs'])==4 and all(r['responded']==1000 for r in nav['runs'])
candidate=[r for r in nav['runs'] if r['implementation']=='candidate']
reference=[r for r in nav['runs'] if r['implementation']=='reference']
import statistics
assert statistics.median(r['sdl_first_redraw_ms']['median'] for r in candidate)<statistics.median(r['sdl_first_redraw_ms']['median'] for r in reference),'No demonstrated median navigation improvement'
archive=A/'es-mister-1080p-atualizacao-final-20261115.zip';sha=hashlib.sha256(archive.read_bytes()).hexdigest()
win=subprocess.check_output(['wslpath','-w',str(archive)],text=True).strip()
subprocess.run([SCP,'-i',KEY,'-o','BatchMode=yes',win,HOST+':'+BASE+'/update-final-20261115.zip'],check=True)
code=f'''
import pathlib,zipfile,hashlib,subprocess,json
base=pathlib.Path({BASE!r});archive=base/'update-final-20261115.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=={sha!r}
dest=base/'install-final-20261115';assert not dest.exists(),'Installation staging already exists; inspect before retrying'
with zipfile.ZipFile(archive) as z:
 for entry in z.infolist():
  p=pathlib.PurePosixPath(entry.filename)
  assert not p.is_absolute() and '..' not in p.parts and p.parts[0]=='codex-update-final-1080p'
 z.extractall(dest)
installer=dest/'codex-update-final-1080p/install-update.py'
subprocess.run(['python3',str(installer),'--check'],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['python3',str(installer)],check=True)
'''
r=subprocess.run([SSH,'-T','-i',KEY,'-o','BatchMode=yes','-o','ConnectTimeout=8','-o','ServerAliveInterval=10','-o','ServerAliveCountMax=2',HOST,'python3 -'],input=code,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
print(r.stdout,flush=True)
if r.returncode:raise RuntimeError(r.stderr+'\nDo not repeat installation blindly; inspect installed hashes and backup first.')
record=json.loads(r.stdout)
assert record['installed_sha256']['emulationstation']==nav['candidate_sha']
assert record['api_ready'] and record['framebuffer']=='1920,1080'
(V/'deployment-final-20261115.json').write_text(json.dumps(record,indent=2))
