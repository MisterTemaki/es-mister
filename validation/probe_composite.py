import pathlib,sys,subprocess,time,base64,csv
v=pathlib.Path(__file__).resolve().parent;root='/media/fat/emulationstation/codex-performance-20261113';label='stage3-composite-probe'
def remote(code):
 d=base64.b64encode(code.encode()).decode()
 return subprocess.check_output(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-n','-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{d}\'))"'],text=True).strip()
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
remote(f"import pathlib,shutil;p=pathlib.Path({root!r})/'bin';shutil.copyfile(p/'stage3-composite-source',p/{label!r});(p/{label!r}).chmod(0o755)")
try:
 run('run',label,'--composite','1','--detail','1','--script',str(v/'composite_probe.txt'))
 for _ in range(50):
  if remote(f"import pathlib;p=pathlib.Path({(root+'/'+label+'/pid')!r});print(pathlib.Path('/proc/'+p.read_text().strip()).exists())")=='False':break
  time.sleep(3)
 else:raise RuntimeError('probe still active')
 run('collect',label)
 rows=list(csv.DictReader((v/'target-results'/label/'trace.csv').open()))
 print('COMPOSITE CALLS',sum(int(r['composite_calls']) for r in rows),flush=True)
 for name in ('first-transition.txt','second-transition.txt'):
  print(name,'\n'.join((v/'target-results'/label/name).read_text().splitlines()[:12]),flush=True)
finally:run('restore')
