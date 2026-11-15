"""Final two renderer hypotheses: independent ARM contracts and component ABBA."""
import pathlib,subprocess,sys
v=pathlib.Path(__file__).resolve().parent
for family in ('clear-scan','output-pair'):
    for mode in ('reference','candidate'):
        label='stage3-last-'+family+'-'+mode
        subprocess.run([sys.executable,str(v/'target_run.py'),'upload',str(v/family/(mode+'-arm')),label],check=True)
code='''
import pathlib,subprocess,os,time,json,urllib.request,resource
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
base=pathlib.Path('/media/fat/emulationstation/codex-performance-20261113');original=pathlib.Path('/media/fat/emulationstation')
dest=base/'last-hypotheses-v2';assert not dest.exists(),'Use a fresh result directory';dest.mkdir()
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
for entry in pathlib.Path('/proc').iterdir():
 if not entry.name.isdigit():continue
 try:exe=(entry/'exe').resolve(strict=True)
 except OSError:continue
 assert exe.parent!=base/'bin','Another benchmark is running'
try:urllib.request.urlopen('http://127.0.0.1:1234/quit',timeout=5).read()
except Exception:pass
for _ in range(150):
 if not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip():break
 time.sleep(.2)
assert not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip()
records={};summary={}
env=dict(os.environ,ES_SOFT_PLD='256',ES_SOFT_OPTIMIZATIONS='1',ES_SOFT_ALPHA_SCAN='1',ES_SOFT_COPY_PLD_PAIR='1',ES_SOFT_TTY='/dev/tty0')
try:
 for family in ('clear-scan','output-pair'):
  quality=[]
  for mode in ('reference','candidate'):
   p=subprocess.run([str(base/'bin'/('stage3-last-'+family+'-'+mode))],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=120)
   records[family+'-'+mode+'-quality.txt']=p.stdout
   records[family+'-'+mode+'-stderr.txt']=p.stderr
   assert p.returncode==0,(family,mode,p.returncode,p.stderr[-1000:])
   quality.append(p.stdout)
  assert quality[0]==quality[1],family+' independent contract differs'
  print('INDEPENDENT ARM CONTRACT MATCH',family,flush=True)
  summary[family]=dict(independent_contract_match=True)
 for attempt in range(3):
  p=subprocess.run([str(original/'es-mister-helper'),'console','1920x1080','8'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
  if p.returncode==0:break
  time.sleep(2)
 assert p.returncode==0,'Console not ready'
 time.sleep(.5)
 for family,variable in [('clear-scan','ES_SOFT_ALPHA_SCAN'),('output-pair','ES_SOFT_COPY_PLD_PAIR')]:
  measurements=[]
  for index,choice in enumerate(('0','1','1','0')):
   assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
   env[variable]=choice
   p=subprocess.run([str(base/'bin'/('stage3-last-'+family+'-candidate')),'bench'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=120)
   records[family+'-'+str(index)+'-'+choice+'-stderr.txt']=p.stderr
   records[family+'-'+str(index)+'-'+choice+'.txt']=p.stdout
   assert p.returncode==0,(family,choice,p.returncode,p.stderr[-1000:])
   records[family+'-'+str(index)+'-'+choice+'.txt']=p.stdout
   measurements.append(dict(enabled=choice,result=p.stdout))
   print('COMPONENT',family,choice,p.stdout.strip(),flush=True)
  summary[family]['abba']=measurements
finally:
 for name,data in records.items():(dest/name).write_text(data)
 (dest/'summary.json').write_text(json.dumps(summary,indent=2))
 if pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU':
  for entry in pathlib.Path('/proc').iterdir():
   if not entry.name.isdigit():continue
   try:exe=(entry/'exe').resolve(strict=True)
   except OSError:continue
   assert exe.parent!=base/'bin','Do not restore over a running benchmark'
  out=(original/'logs/codex-restore.log').open('a')
  p=subprocess.Popen(['bash',str(original/'es-mister.sh')],stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
  print('original launcher restored',p.pid,flush=True)
'''
subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-T','-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14','python3 -'],input=code,text=True,check=True)
