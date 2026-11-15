import pathlib,subprocess,sys,base64,time,json
v=pathlib.Path(__file__).resolve().parent;root='/media/fat/emulationstation/codex-performance-20261113'
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe';key=r'C:\Users\USER\.ssh\id_ed25519_mister'
def remote(code):
    data=base64.b64encode(code.encode()).decode()
    p=subprocess.run([ssh,'-n','-i',key,'-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'],stdout=subprocess.PIPE,text=True,check=True)
    return p.stdout.strip()
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
def wait(label):
    for _ in range(150):
        if remote(f"import pathlib;p=pathlib.Path({(root+'/'+label+'/pid')!r});print(pathlib.Path('/proc/'+p.read_text().strip()).exists())")=='False':return
        time.sleep(3)
    raise RuntimeError('Test did not finish')
def collect(label):
    wait(label);run('collect',label)
    with (v/(label+'-summary.log')).open('w') as out:subprocess.run([sys.executable,str(v/'trace_analyze.py'),str(v/'target-results'/label)],stdout=out,check=True)
    return json.loads((v/'target-results'/label/'summary.json').read_text())
try:
    for name,source in [('stage3-raster-reference',v/'raster-stage3/reference-arm'),('stage3-raster-composite',v/'raster-stage3/composite-arm'),('stage3-composite-source',v/'arm/emulationstation-composite1-stripped')]:run('upload',str(source),name)
    pixels=remote(f'''import pathlib,subprocess,os,json
p=pathlib.Path({root!r})/'bin';results={{}}
for mode in ('normal','composite'):
 records=[]
 for name in ('stage3-raster-reference','stage3-raster-composite'):
  r=subprocess.run([str(p/name)]+(['composite'] if mode=='composite' else []),env=dict(os.environ,ES_SOFT_PLD='256'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=90)
  assert r.returncode==0,r.stdout
  records.append([line.split(' hash ')[1].split()[0] for line in r.stdout.splitlines()])
 assert records[0]==records[1],mode+' ARM PIXEL MISMATCH'
 results[mode]=len(records[0])
print(json.dumps(results))
''')
    print('ARM PIXELS MATCH',pixels,flush=True)
    label='stage3-composite-quality'
    remote(f"import pathlib,shutil;p=pathlib.Path({root!r})/'bin';shutil.copyfile(p/'stage3-composite-source',p/{label!r});(p/{label!r}).chmod(0o755)")
    run('run',label,'--composite','1','--check','--script',str(v/'quality-script.txt'))
    collect(label)
    text=(v/'target-results'/label/'stats.txt').read_text()
    import re
    assert re.search(r'0 of [1-9][0-9]* checked wrong',text),'Missing successful quality check'
    assert not re.search(r'\b[1-9][0-9]* of [0-9]+ checked wrong',text),'APP PIXEL MISMATCH'
    print('APP QUALITY PASSED',flush=True)
    summaries=[]
    for label,installed in [('stage3-current-short',True),('stage3-composite-short',False)]:
        if not installed:remote(f"import pathlib,shutil;p=pathlib.Path({root!r})/'bin';shutil.copyfile(p/'stage3-composite-source',p/{label!r});(p/{label!r}).chmod(0o755)")
        options=['--installed'] if installed else ['--composite','1']
        run('run',label,'--steps','64',*options)
        summary=collect(label);summaries.append(summary)
        print('RESULT',label,json.dumps({k:summary[k] for k in ('input_sdl_event_ms','full_navigation')}),flush=True)
    print('SHORT COMPARISON COMPLETE',flush=True)
finally:
    try:run('restore')
    except subprocess.CalledProcessError:print('App still active; inspect before restoring',flush=True)
