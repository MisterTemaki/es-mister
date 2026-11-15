import base64,subprocess
script='''import os,pathlib,subprocess,time,shutil,hashlib,json
base=pathlib.Path('/media/fat/emulationstation')
root=base/'codex-performance-20261113'
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
assert not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip()
helper=root/'bin/helper'
results=[]
for mode,expected in [('1920x1080','1920,1080'),('auto/2','960,540'),('auto','1920,1080')]:
 start=time.monotonic()
 r=subprocess.run([str(helper),'console',mode,'8'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=10)
 elapsed=(time.monotonic()-start)*1000
 size=pathlib.Path('/sys/class/graphics/fb0/virtual_size').read_text().strip()
 print(mode,r.returncode,round(elapsed,1),size,r.stdout.strip(),flush=True)
 assert r.returncode==0 and size==expected,(mode,size)
 results.append(dict(mode=mode,ms=elapsed,size=size))
backup=root/'backup';backup.mkdir(exist_ok=True)
old=base/'es-mister-helper'
if not (backup/'es-mister-helper').exists():shutil.copy2(old,backup/'es-mister-helper')
before=hashlib.sha256(old.read_bytes()).hexdigest()
shutil.copy2(helper,base/'es-mister-helper.codex-new')
os.replace(base/'es-mister-helper.codex-new',old)
after=hashlib.sha256(old.read_bytes()).hexdigest()
(root/'helper-validation.json').write_text(json.dumps(dict(before=before,after=after,tests=results),indent=2))
os.sync()
print('Helper installed with original backup',before,after)
'''
data=base64.b64encode(script.encode()).decode()
subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-i',r'C:\Users\USER\.ssh\id_ed25519_mister',
 '-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'],check=True)
