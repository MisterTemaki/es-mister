import pathlib,subprocess,sys,base64,time,json,re
v=pathlib.Path(__file__).resolve().parent;root='/media/fat/emulationstation/codex-performance-20261113'
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe';key=r'C:\Users\USER\.ssh\id_ed25519_mister'
def remote(code):
 d=base64.b64encode(code.encode()).decode()
 return subprocess.check_output([ssh,'-n','-i',key,'-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{d}\'))"'],text=True).strip()
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
def collect(label):
 for _ in range(150):
  if remote(f"import pathlib;p=pathlib.Path({(root+'/'+label+'/pid')!r});print(pathlib.Path('/proc/'+p.read_text().strip()).exists())")=='False':break
  time.sleep(3)
 else:raise RuntimeError('App still active')
 run('collect',label)
 with (v/(label+'-summary.log')).open('w') as out:subprocess.run([sys.executable,str(v/'trace_analyze.py'),str(v/'target-results'/label)],stdout=out,check=True)
 return json.loads((v/'target-results'/label/'summary.json').read_text())
def alias(label):remote(f"import pathlib,shutil;p=pathlib.Path({root!r})/'bin';shutil.copyfile(p/'stage3-packed-source',p/{label!r});(p/{label!r}).chmod(0o755)")
try:
 for name,source in [('stage3-raster-reference',v/'raster-stage3/reference-arm'),('stage3-raster-packed',v/'raster-stage3/packed-arm'),('stage3-packed-source',v/'arm/emulationstation-packed-isolated-stripped')]:run('upload',str(source),name)
 print(remote(f'''import pathlib,subprocess,os,json
p=pathlib.Path({root!r})/'bin';result={{}}
for mode in ('normal','composite'):
 records=[]
 for name in ('stage3-raster-reference','stage3-raster-packed'):
  r=subprocess.run([str(p/name)]+(['composite'] if mode=='composite' else []),env=dict(os.environ,ES_SOFT_PLD='256',ES_SOFT_PACKED='1'),stdout=subprocess.PIPE,text=True,timeout=90);assert r.returncode==0
  records.append([line.split(' hash ')[1].split()[0] for line in r.stdout.splitlines()])
 assert records[0]==records[1],'ARM PACKED PIXEL MISMATCH'
 result[mode]=len(records[0])
print('PACKED ARM PIXELS MATCH',json.dumps(result))
'''),flush=True)
 label='stage3-packed-quality';alias(label)
 run('run',label,'--packed','1','--check','--script',str(v/'quality-script.txt'));collect(label)
 text=(v/'target-results'/label/'stats.txt').read_text()
 assert re.search(r'0 of [1-9][0-9]* checked wrong',text) and not re.search(r'\b[1-9][0-9]* of [0-9]+ checked wrong',text)
 print('PACKED APP QUALITY PASSED',flush=True)
 for label,installed in [('stage3-ref-packed-short',True),('stage3-packed-short',False)]:
  if not installed:alias(label)
  run('run',label,'--steps','64',*(['--installed'] if installed else ['--packed','1']))
  s=collect(label);print('RESULT',label,json.dumps({k:s[k] for k in ('input_sdl_event_ms','full_navigation')}),flush=True)
finally:
 try:run('restore')
 except subprocess.CalledProcessError:print('App still active; inspect before restore',flush=True)
print('PACKED SCREENING COMPLETE',flush=True)
