import pathlib,subprocess,time,base64,sys
v=pathlib.Path(__file__).resolve().parent
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe'
key=r'C:\Users\USER\.ssh\id_ed25519_mister'
root='/media/fat/emulationstation/codex-performance-20261113'
def remote(code):
    encoded=base64.b64encode(code.encode()).decode()
    r=subprocess.run([ssh,'-i',key,'-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{encoded}\'))"'],check=True,stdout=subprocess.PIPE,text=True)
    return r.stdout.strip()
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
def wait(label):
    for attempt in range(100):
        state=remote(f"import pathlib;p=pathlib.Path({(root+'/'+label+'/pid')!r});print(pathlib.Path('/proc/'+p.read_text().strip()).exists())")
        if state=='False':return
        time.sleep(3)
    raise RuntimeError('Run has not ended: '+label)
wait('quality-final');run('collect','quality-final')
print('QUALITY FINISHED',flush=True)
for label,options in [('threads-one',['--steps','24','--threads','1']),
                      ('startup-control-final',['--optimizations','0','--script',str(v/'startup-script.txt')]),
                      ('startup-fast-final',['--script',str(v/'startup-script.txt')])]:
    print('START',label,flush=True)
    remote(f"import pathlib,shutil;p=pathlib.Path({root!r})/'bin';shutil.copyfile(p/'sustained-fast',p/{label!r});(p/{label!r}).chmod(0o755)")
    run('run',label,*options);wait(label);run('collect',label)
    if label=='threads-one':
        with (v/'threads-one-summary.log').open('w') as log:
            subprocess.run([sys.executable,str(v/'trace_analyze.py'),str(v/'target-results'/label)],stdout=log,check=True)
    print('DONE',label,flush=True)
run('upload',str(v/'arm/audit-similarity'),'audit-similarity')
print(remote(f"import subprocess,os; r=subprocess.run([{(root+'/bin/audit-similarity')!r},'bench'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=90);print(r.stdout);assert r.returncode==0"),flush=True)
print('ALL TARGET CHECKS FINISHED',flush=True)
