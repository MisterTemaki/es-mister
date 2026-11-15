import pathlib,subprocess,time,base64,sys
v=pathlib.Path(__file__).resolve().parent;root='/media/fat/emulationstation/codex-performance-20261113';label='stage3-startup-cpu-profile'
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
try:
 run('run',label,'--installed','--profile','--script',str(v/'startup-profile.txt'))
 code=f"import pathlib;p=pathlib.Path({(root+'/'+label+'/pid')!r});print(pathlib.Path('/proc/'+p.read_text().strip()).exists())"
 d=base64.b64encode(code.encode()).decode()
 for _ in range(80):
  out=subprocess.check_output(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-n','-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{d}\'))"'],text=True).strip()
  if out=='False':break
  time.sleep(3)
 else:raise RuntimeError('Profile still active')
 run('collect',label)
 print('\n'.join((v/'target-results'/label/'cpu-profile.txt').read_text().splitlines()[:12]),flush=True)
finally:run('restore')
