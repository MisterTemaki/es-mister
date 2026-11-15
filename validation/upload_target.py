import pathlib,subprocess,sys
v=pathlib.Path(__file__).resolve().parent
code='''
import pathlib,subprocess,time,urllib.request
base=pathlib.Path('/media/fat/emulationstation/codex-performance-20261113')
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
dest=base/'upload-kernel';dest.mkdir(exist_ok=True)
unit=base/'bin/stage3-upload-unit'
r=subprocess.run([str(unit)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60,check=True)
(dest/'quality.txt').write_text(r.stdout);print(r.stdout,flush=True)
try:urllib.request.urlopen('http://127.0.0.1:1234/quit',timeout=5).read()
except Exception:pass
for _ in range(150):
 if not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip():break
 time.sleep(.2)
assert not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip()
time.sleep(2)
r=subprocess.run([str(unit),'bench'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180,check=True)
(dest/'bench.txt').write_text(r.stdout);print(r.stdout,flush=True)
'''
try:
    subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-T','-i',r'C:\Users\USER\.ssh\id_ed25519_mister',
                    '-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14','python3 -'],input=code,text=True,check=True)
finally:
    subprocess.run([sys.executable,str(v/'target_run.py'),'restore'],check=True)
