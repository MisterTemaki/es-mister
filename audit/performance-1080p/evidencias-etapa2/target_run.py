"""Run a bounded navigation script in an isolated HOME on the actual MiSTer.
The installed executable and real configuration are never overwritten by a test.
"""
import argparse, base64, json, pathlib, subprocess
SSH='/mnt/c/Windows/System32/OpenSSH/ssh.exe'
SCP='/mnt/c/Windows/System32/OpenSSH/scp.exe'
KEY=r'C:\Users\USER\.ssh\id_ed25519_mister'
HOST='root@192.168.15.14'
REMOTE='/media/fat/emulationstation/codex-performance-20261113'
def remote(code):
    data=base64.b64encode(code.encode()).decode()
    r=subprocess.run([SSH,'-i',KEY,'-o','BatchMode=yes','-o','ConnectTimeout=8',HOST,
        f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'],stdout=subprocess.PIPE,text=True)
    print(r.stdout,flush=True)
    if r.returncode: raise SystemExit('Remote operation failed; see error above')
    return r.stdout
def transfer(source,destination):
    win=subprocess.check_output(['wslpath','-w',str(pathlib.Path(source).resolve())],text=True).strip()
    subprocess.run([SCP,'-i',KEY,'-o','BatchMode=yes',win,HOST+':'+destination],check=True)
p=argparse.ArgumentParser();sub=p.add_subparsers(dest='action',required=True)
u=sub.add_parser('upload');u.add_argument('binary');u.add_argument('label')
r=sub.add_parser('run');r.add_argument('label');r.add_argument('--installed',action='store_true');r.add_argument('--detail',type=int,default=0);r.add_argument('--check',action='store_true');r.add_argument('--steps',type=int,default=32);r.add_argument('--optimizations',choices=('0','1'),default='1');r.add_argument('--threads',choices=('1','2'),default='2')
r.add_argument('--script',help='Local script replacing the standard navigation workload')
s=sub.add_parser('status');s.add_argument('label')
c=sub.add_parser('collect');c.add_argument('label')
sub.add_parser('restore')
args=p.parse_args()
if hasattr(args,'label') and not args.label.replace('-','').replace('_','').isalnum():raise ValueError('invalid label')
if args.action=='upload':
    remote(f'import pathlib;p=pathlib.Path({(REMOTE+"/bin")!r});p.mkdir(parents=True,exist_ok=True)')
    transfer(args.binary,REMOTE+'/bin/'+args.label)
    remote(f'import pathlib;p=pathlib.Path({(REMOTE+"/bin/"+args.label)!r});p.chmod(0o755);print(p.stat().st_size)')
elif args.action=='run':
    script='wait 14000\nlog navigation-start\nstats\n'
    for i in range(args.steps):script+='key '+('Right' if i%8<4 else 'Left')+'\nwait 450\n'
    script+='wait 1500\nlog navigation-end\nstats\nquit\n'
    if args.script:script=pathlib.Path(args.script).read_text()
    code=f'''
import pathlib,os,signal,time,subprocess,shutil,urllib.request,json
base=pathlib.Path({REMOTE!r});base.mkdir(exist_ok=True)
original=pathlib.Path('/media/fat/emulationstation')
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU','Core changed: no test started'
for entry in pathlib.Path('/proc').iterdir():
    if not entry.name.isdigit(): continue
    try: exe=(entry/'exe').resolve(strict=True)
    except OSError: continue
    if exe.parent==base/'bin': raise RuntimeError('A candidate is still running: '+str(exe))
# Close via the frontend API so it can save and shut down normally.
try: print(urllib.request.urlopen('http://127.0.0.1:1234/quit',timeout=2).read().decode())
except Exception as e: print('quit API',type(e).__name__)
time.sleep(3)
running=subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE,text=True).stdout.strip()
if running:
    raise RuntimeError('Frontend is still running; refusing a second framebuffer owner: '+running)
run=base/{args.label!r};run.mkdir(exist_ok=True)
home=pathlib.Path('/tmp')/('codex-perf-'+{args.label!r})
config=home/'.emulationstation';config.mkdir(parents=True,exist_ok=True)
for n in ('es_settings.cfg','es_input.cfg','es_systems.cfg'):
    shutil.copy2(original/'home/.emulationstation'/n,config/n)
themes=config/'themes'
if not themes.exists(): themes.symlink_to(original/'home/.emulationstation/themes',target_is_directory=True)
script=run/'navigation.txt';script.write_text({script!r})
for attempt in range(3):
    helper=base/'bin'/'helper'
    if not helper.exists(): helper=original/'es-mister-helper'
    console=subprocess.run([str(helper),'console','1920x1080','8'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
    print('console',attempt,console.returncode,console.stdout,flush=True)
    if console.returncode==0:break
    time.sleep(2)
if console.returncode: raise RuntimeError('Console unavailable')
env=dict(os.environ,HOME=str(home),SDL_AUDIODRIVER='alsa',SDL_EVDEV_IGNORE='MiSTer virtual input,Zaparoo',
    ES_SOFT_FPS='60',ES_SOFT_STRIP='8',ES_SOFT_PLD='256',ES_SOFT_STATS='600',
    ES_SOFT_STATS_FILE=str(run/'stats.txt'),ES_STARTUP_TRACE=str(run/'startup.log'),
    ES_SOFT_SCRIPT=str(script),ES_SOFT_SHOTS=str(run),ES_ARCADE_SKIP='cores _organized mame hbmame _alternatives')
for k in ('ES_SOFT_BENCH','ES_SOFT_SLOW_DUMP','ES_PROFILE','ES_SOFT_DUMP','ES_SOFT_DIRTY_CHECK','ES_SOFT_TRACE'):
    env.pop(k,None)
env['ES_SOFT_TRACE']=str(run/'trace.csv')
env['ES_SOFT_TRACE_DETAIL']={str(args.detail)!r}
env['ES_SOFT_OPTIMIZATIONS']={args.optimizations!r}
env['ES_SOFT_THREADS']={args.threads!r}
if {args.check!r}: env['ES_SOFT_DIRTY_CHECK']='1'
binary=original/'emulationstation' if {args.installed!r} else base/'bin'/{args.label!r}
out=(run/'stdout.txt').open('w')
proc=subprocess.Popen([str(binary)],cwd=original,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
(run/'pid').write_text(str(proc.pid))
print('started',proc.pid,'isolated HOME',home)
monitor="""import pathlib,time,json,sys
pid=int(sys.argv[1]);out=pathlib.Path(sys.argv[2]);rows=[];start=time.monotonic()
while pathlib.Path('/proc/'+str(pid)).exists():
 try:
  values={{}}
  for line in pathlib.Path('/proc/'+str(pid)+'/status').read_text().splitlines():
   key=line.split(':',1)[0]
   if key in ('VmRSS','VmHWM','Threads'):values[key]=int(line.split()[1])
  values['seconds']=round(time.monotonic()-start,2);rows.append(values)
 except (FileNotFoundError,ProcessLookupError):break
 time.sleep(1)
out.write_text(json.dumps(rows))
"""
subprocess.Popen(['python3','-c',monitor,str(proc.pid),str(run/'memory.json')],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
'''
    remote(code)
elif args.action=='status':
    remote(f'''import pathlib,os,json
p=pathlib.Path({(REMOTE+'/'+args.label)!r});pid=int((p/'pid').read_text());print('running',pathlib.Path('/proc/'+str(pid)).exists())
for name in ('stdout.txt','stats.txt','startup.log'):
 f=p/name
 print(name, '\\n'.join(f.read_text(errors='replace').splitlines()[-4:]) if f.exists() else 'missing')
print('trace', (p/'trace.csv').stat().st_size if (p/'trace.csv').exists() else 0)
''')
elif args.action=='collect':
    remote(f'''import pathlib,tarfile,shutil
p=pathlib.Path({(REMOTE+'/'+args.label)!r})
log=pathlib.Path('/tmp')/('codex-perf-'+{args.label!r})/'.emulationstation/es_log.txt'
if log.exists(): shutil.copy2(log,p/'application.log')
with tarfile.open(str(p)+'.tar.gz','w:gz') as tar: tar.add(p,arcname=p.name)
print('collected',p)
''')
    dest=pathlib.Path(__file__).resolve().parent/'target-results';dest.mkdir(exist_ok=True)
    win=subprocess.check_output(['wslpath','-w',str(dest)],text=True).strip()
    subprocess.run([SCP,'-i',KEY,'-o','BatchMode=yes',HOST+':'+REMOTE+'/'+args.label+'.tar.gz',win],check=True)
    import tarfile
    with tarfile.open(dest/(args.label+'.tar.gz')) as tar:tar.extractall(dest,filter='data')
elif args.action=='restore':
    remote('''import subprocess,pathlib
assert not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip(),'Frontend still running'
p=pathlib.Path('/media/fat/emulationstation')
out=(p/'logs/codex-restore.log').open('a')
proc=subprocess.Popen(['bash',str(p/'es-mister.sh')],stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
print('original launcher restored',proc.pid)
''')
