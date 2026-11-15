"""Bounded reboot and original video-probe lifecycle checks; no real ROM/save."""
import argparse,csv,hashlib,json,pathlib,re,subprocess,sys,time
from target_guard import PROCESS_GUARD
from core_result import summarize
V=pathlib.Path(__file__).resolve().parent
SSH='/mnt/c/Windows/System32/OpenSSH/ssh.exe';SCP='/mnt/c/Windows/System32/OpenSSH/scp.exe'
KEY=r'C:\Users\USER\.ssh\id_ed25519_mister';HOST='root@192.168.15.14'
BASE='/media/fat/emulationstation/codex-performance-20261113'
RETURN_MENU=r'''
from pathlib import Path
import os,time,json
p=Path(__RUN__);sent=p/'return-request.json'
if sent.exists():print(sent.read_text());raise SystemExit(0)
core=Path('/tmp/CORENAME').read_text().strip()
if core=='MENU':print(json.dumps(dict(already_menu=True)));raise SystemExit(0)
assert core=='NES','Unexpected core; no return request sent'
pid=int((p/'pid').read_text())
assert Path('/proc',str(pid),'exe').resolve()==Path('/media/fat/emulationstation/codex-performance-20261113/bin/stage3-final-candidate')
at=time.monotonic_ns();payload=b'load_core /media/fat/menu.rbf\n'
fd=os.open('/dev/MiSTer_cmd',os.O_WRONLY|os.O_NONBLOCK)
try:count=os.write(fd,payload)
finally:os.close(fd)
assert count==len(payload)
record=dict(return_request_ns=at,boot_to_monotonic_ns=time.monotonic_ns()-time.clock_gettime_ns(time.CLOCK_BOOTTIME),boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip())
sent.write_text(json.dumps(record,indent=2));print(json.dumps(record))
'''
def remote(code,check=True,timeout=20):
    r=subprocess.run([SSH,'-T','-i',KEY,'-o','BatchMode=yes','-o','ConnectTimeout=4','-o','ServerAliveInterval=5','-o','ServerAliveCountMax=1',HOST,'python3 -'],input=code,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
    if check and r.returncode:raise RuntimeError(r.stderr or r.stdout)
    return r
def transfer(source,dest):
    win=subprocess.check_output(['wslpath','-w',str(pathlib.Path(source).resolve())],text=True).strip()
    subprocess.run([SCP,'-i',KEY,'-o','BatchMode=yes',win,HOST+':'+dest],check=True)
def bench(*args):subprocess.run([sys.executable,str(V/'target_run.py'),*args],check=True)
def request_menu(label):
    path=BASE+'/'+label
    state_code="from pathlib import Path;import json;p=Path("+repr(path)+");f=p/'return-request.json';print(json.dumps(dict(core=Path('/tmp/CORENAME').read_text().strip(),sent=json.loads(f.read_text()) if f.exists() else None)))"
    deadline=time.monotonic()+60
    while time.monotonic()<deadline:
        try:
            # Observe the actual core and delivery marker before every attempt.
            # A lost acknowledgement never causes an already-delivered request to repeat.
            state=remote(state_code,check=False,timeout=10)
            if state.returncode:time.sleep(2);continue
            observed=json.loads(state.stdout)
            if observed['sent'] or observed['core']=='MENU':return
            assert observed['core']=='NES','Another core is active; no reset requested'
            remote(RETURN_MENU.replace('__RUN__',repr(path)),timeout=10)
            return
        except (RuntimeError,subprocess.TimeoutExpired,ValueError) as error:
            print('Return transport interrupted; inspecting state/delivery before any retry:',str(error),flush=True)
            time.sleep(3)
    raise RuntimeError('Return request could not be confirmed; inspect NES/MENU and marker before continuing')
p=argparse.ArgumentParser();p.add_argument('mode',choices=('reboot','core'));p.add_argument('label');p.add_argument('--no-check',action='store_true',help='Core return timing pass without double pixel checking');a=p.parse_args()
assert re.fullmatch('[A-Za-z0-9_-]+',a.label)
if a.mode=='reboot':
    code=PROCESS_GUARD+'''
import pathlib,os,json,subprocess,time
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU','Do not interrupt another core'
stop_production()
record=dict(boot_id=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),uptime=pathlib.Path('/proc/uptime').read_text().split()[0])
print(json.dumps(record),flush=True)
os.sync()
subprocess.Popen(['/sbin/reboot'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
'''
    started=time.monotonic();r=remote(code,check=False,timeout=75)
    if not r.stdout.strip():raise RuntimeError('No reboot acknowledgement; do not repeat blindly: '+r.stderr)
    before=json.loads(r.stdout.strip().splitlines()[-1]);after=None
    deadline=time.monotonic()+150
    while time.monotonic()<deadline:
        time.sleep(2)
        try:q=remote("import pathlib,json;p=pathlib.Path('/tmp/CORENAME');print(json.dumps(dict(boot_id=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),core=p.read_text().strip() if p.exists() else None,uptime=pathlib.Path('/proc/uptime').read_text().split()[0])))",check=False,timeout=10)
        except subprocess.TimeoutExpired:continue # Read-only polling; never repeat the reboot request.
        if q.returncode:continue
        state=json.loads(q.stdout)
        if state['boot_id']!=before['boot_id'] and state['core']=='MENU':after=state;break
    if after is None:raise RuntimeError('New Linux boot/MENU not confirmed; stop dependent tests')
    ready=time.monotonic()-started
    time.sleep(20) # Same settling interval for the original background services in every boot.
    state=remote(PROCESS_GUARD+"import json;owners,launchers=owners_and_launchers();print(json.dumps(dict(owners=owners,launchers=launchers)))")
    no_frontend=json.loads(state.stdout)
    assert not no_frontend['owners'] and not no_frontend['launchers'],'Autostart warmed a frontend; do not label the next opening cold'
    result=dict(before=before,after=after,host_seconds_to_ssh_and_menu=ready,settle_seconds=20,frontend_before_first_opening=no_frontend)
    dest=V/'boot-events';dest.mkdir(exist_ok=True);(dest/(a.label+'.json')).write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)
else:
    rom=V/'codex-video-probe.nes'
    assert hashlib.sha256(rom.read_bytes()).hexdigest()=='ff4228a6e59bda2a4d018bbbe0f4e9f0c0a9f305760d51fd57213bf9b616c1e5'
    r=remote(PROCESS_GUARD+f'''
import pathlib,shutil
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
owners,launchers=owners_and_launchers()
assert all(exe=='/media/fat/emulationstation/emulationstation' for pid,exe in owners),'Another benchmark is active'
base=pathlib.Path({BASE!r});original=pathlib.Path('/media/fat/emulationstation')
assert not (base/{a.label!r}/'pid').exists(),'Use a fresh core-run label'
(base/'core-fixture').mkdir(exist_ok=True)
launch=base/'core-launcher';(launch/'logs').mkdir(parents=True,exist_ok=True)
for name in ('es-mister-helper','systems.tsv'):shutil.copy2(original/name,launch/name)
(launch/'es-mister.ini').write_text('SIZE=1920x1080\\n')
''')
    transfer(rom,BASE+'/core-fixture/probe.nes')
    transfer(V.parent/'es-mister/mister/scripts/mister-launch.sh',BASE+'/core-launcher/mister-launch.sh')
    try:
        options=['run',a.label,'--binary-label','stage3-final-candidate','--core-fixture','--script',str(V/'core-return.txt'),'--upload-neon','1','--opaque-fade','1','--copy-pld-pair','1','--system-threads','2','--texture-threads','1','--input-probe','--trace-redraws-only']
        if not a.no_check:options.append('--check')
        bench(*options)
        deadline=time.monotonic()+75;core=None;started=None
        while time.monotonic()<deadline:
            try:q=remote("from pathlib import Path;print(Path('/tmp/CORENAME').read_text().strip())",timeout=10)
            except (RuntimeError,subprocess.TimeoutExpired):time.sleep(1);continue
            name=q.stdout.strip()
            if name and name!='MENU':core=name;started=time.monotonic_ns();break
            time.sleep(.5)
        assert core=='NES','Expected only the original NES probe, got '+str(core)
        print('CORE_STARTED NES; holding the original video probe for eight seconds',flush=True)
        time.sleep(8)
        request_menu(a.label)
        returned=None;deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            try:q=remote(f"from pathlib import Path;import json;p=Path({(BASE+'/'+a.label)!r});print(json.dumps(dict(core=Path('/tmp/CORENAME').read_text().strip(),finished=not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists(),size=Path('/sys/class/graphics/fb0/virtual_size').read_text().strip())))",timeout=10)
            except (RuntimeError,subprocess.TimeoutExpired):time.sleep(1);continue # Read only; no core request repeated.
            state=json.loads(q.stdout)
            if state['core']=='MENU' and returned is None:returned=time.monotonic_ns()
            if state['finished']:
                assert state['core']=='MENU' and state['size']=='1920,1080'
                break
            time.sleep(1)
        else:raise RuntimeError('Core return/frontend shutdown timeout')
        bench('collect',a.label)
        d=V/'target-results'/a.label
        record=summarize(d,not a.no_check)
        record.update(host_observed_core_begin_ns=started,host_observed_menu_ns=returned)
        (d/'lifecycle-summary.json').write_text(json.dumps(record,indent=2));print(json.dumps(record),flush=True)
    finally:bench('restore')
