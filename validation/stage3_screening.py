import pathlib,subprocess,sys,base64,time,json
v=pathlib.Path(__file__).resolve().parent
root='/media/fat/emulationstation/codex-performance-20261113'
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe';key=r'C:\Users\USER\.ssh\id_ed25519_mister'
def remote(code):
    data=base64.b64encode(code.encode()).decode()
    p=subprocess.run([ssh,'-n','-i',key,'-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'],stdout=subprocess.PIPE,text=True,check=True)
    return p.stdout.strip()
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
def wait(label):
    for _ in range(100):
        if remote(f"import pathlib;p=pathlib.Path({(root+'/'+label+'/pid')!r});print(pathlib.Path('/proc/'+p.read_text().strip()).exists())")=='False':return
        time.sleep(3)
    raise RuntimeError('Candidate did not finish')
try:
    run('upload',str(v/'arm/emulationstation-tiles-stripped'),'stage3-tiles-source')
    for label,tile,installed in [('stage3-base-profile',0,True),('stage3-tile256-profile',256,False),('stage3-tile512-profile',512,False)]:
        if not installed:
            remote(f"import pathlib,shutil;p=pathlib.Path({root!r})/'bin';shutil.copyfile(p/'stage3-tiles-source',p/{label!r});(p/{label!r}).chmod(0o755)")
        options=['--installed'] if installed else []
        print('START',label,flush=True)
        run('run',label,'--steps','48','--detail','16','--tile',str(tile),*options)
        wait(label);run('collect',label)
        with (v/(label+'-summary.log')).open('w') as log:
            subprocess.run([sys.executable,str(v/'trace_analyze.py'),str(v/'target-results'/label)],stdout=log,check=True)
        data=json.loads((v/'target-results'/label/'summary.json').read_text())
        print('RESULT',label,json.dumps({k:data[k] for k in ('input_sdl_event_ms','full_navigation','sampled_cache_navigation','sampled_command_groups_by_sum_worker_wall_time')}),flush=True)
finally:
    try:run('restore')
    except subprocess.CalledProcessError:print('An app remains active; inspect before restoring',flush=True)
print('SCREENING COMPLETE',flush=True)
