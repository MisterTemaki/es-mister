import pathlib,subprocess,time,base64,sys
v=pathlib.Path(__file__).resolve().parent
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe'
key=r'C:\Users\USER\.ssh\id_ed25519_mister'
code="import pathlib;p=pathlib.Path('/media/fat/emulationstation/codex-performance-20261113/sustained-fast/pid');print(pathlib.Path('/proc/'+p.read_text().strip()).exists())"
encoded=base64.b64encode(code.encode()).decode()
for attempt in range(24):
    r=subprocess.run([ssh,'-i',key,'-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{encoded}\'))"'],check=True,stdout=subprocess.PIPE,text=True)
    if r.stdout.strip()=='False': break
    time.sleep(15)
else:raise SystemExit('Run still active; no collection or new run started')
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
print('Sustained run finished',flush=True)
run('collect','sustained-fast')
for name in ('sustained-control','sustained-fast'):
    with (v/(name+'-summary.log')).open('w') as out:
        subprocess.run([sys.executable,str(v/'trace_analyze.py'),str(v/'target-results'/name)],stdout=out,check=True)
run('upload',str(v/'arm/emulationstation-performance-stripped'),'quality-final')
run('run','quality-final','--check','--script',str(v/'quality-script.txt'))
