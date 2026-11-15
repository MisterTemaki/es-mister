"""Bounded 720p/1080p experiment; return to the installed native-resolution launcher."""
import base64,pathlib,subprocess,sys,time,json
v=pathlib.Path(__file__).resolve().parent
root='/media/fat/emulationstation/codex-performance-20261113'
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe'
key=r'C:\Users\USER\.ssh\id_ed25519_mister'
def remote(code):
    data=base64.b64encode(code.encode()).decode()
    p=subprocess.run([ssh,'-n','-i',key,'-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'],stdout=subprocess.PIPE,text=True,check=True)
    return p.stdout.strip()
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
def wait(label):
    for _ in range(90):
        if remote(f"import pathlib;p=pathlib.Path({(root+'/'+label+'/pid')!r});print(pathlib.Path('/proc/'+p.read_text().strip()).exists())")=='False': return
        time.sleep(3)
    raise RuntimeError('Test still active: '+label)
script='wait 14000\nlog navigation-start\nstats\n'
for i in range(48):script+='key '+('Right' if i%8<4 else 'Left')+'\nwait 450\n'
script+='wait 1500\nlog navigation-end\nstats\nshot final\nquit\n'
(v/'compare-720p-script.txt').write_text(script)
try:
    for label,size in [('compare720-20261114','1280x720'),('compare1080-20261114','1920x1080')]:
        print('START',label,size,flush=True)
        run('run',label,'--installed','--size',size,'--script',str(v/'compare-720p-script.txt'))
        wait(label);run('collect',label)
        with (v/(label+'-summary.log')).open('w') as output:
            subprocess.run([sys.executable,str(v/'trace_analyze.py'),str(v/'target-results'/label)],stdout=output,check=True)
        summary=json.loads((v/'target-results'/label/'summary.json').read_text())
        print('RESULT',label,json.dumps({k:summary[k] for k in ('input_dispatch_ms','input_sdl_event_ms','full_navigation')}),flush=True)
finally:
    active=remote(f"import pathlib;root=pathlib.Path({root!r}); found=[]\nfor p in pathlib.Path('/proc').iterdir():\n if not p.name.isdigit():continue\n try:exe=(p/'exe').resolve(strict=True)\n except OSError:continue\n if exe==pathlib.Path('/media/fat/emulationstation/emulationstation') or exe.parent==root/'bin':found.append(p.name)\nprint(' '.join(found))")
    if not active: run('restore')
    else: print('An app is active; inspect before restoring:',active,flush=True)
print('COMPARISON COMPLETE',flush=True)
