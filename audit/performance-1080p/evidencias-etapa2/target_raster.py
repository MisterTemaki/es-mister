import base64,subprocess
script='''import pathlib,subprocess,os,json,time
root=pathlib.Path('/media/fat/emulationstation/codex-performance-20261113')
assert not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip()
output=root/'raster-results';output.mkdir(exist_ok=True)
def run(name,mode,bench=False):
 env=dict(os.environ,ES_SOFT_OPTIMIZATIONS=str(mode),ES_SOFT_PLD='256')
 p=subprocess.run([str(root/'bin'/name)]+(['bench'] if bench else []),env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
 print(name,mode,'bench' if bench else 'pixels',p.returncode,p.stdout,flush=True)
 assert p.returncode==0
 return p.stdout
reference=run('raster-reference',0)
candidate=run('raster-alpha',1)
def hashes(text):return [line.split(' hash ')[1].split()[0] for line in text.splitlines() if ' hash ' in line]
assert hashes(reference)==hashes(candidate),'ARM PIXEL MISMATCH'
(output/'pixels-reference.txt').write_text(reference)
(output/'pixels-alpha.txt').write_text(candidate)
for repeat in range(3):
 for mode in (0,1):
  text=run('raster-alpha',mode,True)
  (output/('bench-'+str(mode)+'-'+str(repeat)+'.txt')).write_text(text)
print('ARM differential pixels identical; six kernel workloads completed.',flush=True)
'''
data=base64.b64encode(script.encode()).decode()
r=subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-i',r'C:\Users\USER\.ssh\id_ed25519_mister',
 '-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'])
raise SystemExit(r.returncode)
