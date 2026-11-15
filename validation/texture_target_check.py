import subprocess,pathlib,base64,sys
v=pathlib.Path(__file__).resolve().parent;root='/media/fat/emulationstation/codex-performance-20261113'
for tag in ('reference','candidate'):
 subprocess.run([sys.executable,str(v/'target_run.py'),'upload',str(v/('arm/audit-texture-'+tag)),'audit-texture-'+tag],check=True)
code=f'''import subprocess,pathlib,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
p=pathlib.Path({root!r})/'bin'
r=subprocess.run([str(p/'audit-texture-reference'),'ram'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
print('REFERENCE_RAM',r.returncode,r.stdout,flush=True);assert r.returncode!=0
r=subprocess.run([str(p/'audit-texture-reference'),'ram-under-limit'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
print('REFERENCE_RAM_UNDER_LIMIT',r.returncode,r.stdout,flush=True);assert r.returncode==0
for mode in (['ram'],['ram-under-limit'],[]):
 r=subprocess.run([str(p/'audit-texture-candidate')]+mode,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=20)
 print('CANDIDATE',mode,r.returncode,r.stdout,flush=True);assert r.returncode==0
'''
d=base64.b64encode(code.encode()).decode()
subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-n','-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{d}\'))"'],check=True)
