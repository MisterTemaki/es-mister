"""Run a bounded navigation script in an isolated HOME on the actual MiSTer.
The installed executable and real configuration are never overwritten by a test.
"""
import argparse, base64, json, pathlib, subprocess
SSH='/mnt/c/Windows/System32/OpenSSH/ssh.exe'
SCP='/mnt/c/Windows/System32/OpenSSH/scp.exe'
KEY=r'C:\Users\USER\.ssh\id_ed25519_mister'
HOST='root@192.168.15.14'
REMOTE='/media/fat/emulationstation/codex-performance-20261113'
from target_guard import PROCESS_GUARD
def remote(code):
    # stdin avoids the Windows 32 KiB command-line limit for 1,000-command scripts.
    r=subprocess.run([SSH,'-T','-i',KEY,'-o','BatchMode=yes','-o','ConnectTimeout=8',HOST,
        'python3 -'],input=code,stdout=subprocess.PIPE,text=True)
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
r.add_argument('--size',choices=('1920x1080','1280x720'),default='1920x1080')
r.add_argument('--tile',type=int,default=0)
r.add_argument('--composite',choices=('0','1'),default='0')
r.add_argument('--packed',choices=('0','1'),default='0')
r.add_argument('--profile',action='store_true')
r.add_argument('--trace-off',action='store_true')
r.add_argument('--kernel-probe',action='store_true',help='Read-only 1 Hz /proc monitor; upload kernel_probe first')
r.add_argument('--binary-label',help='Use a previously uploaded binary without copying it for each run')
r.add_argument('--system-threads',choices=('1','2','4'))
r.add_argument('--texture-threads',choices=('1','2'))
r.add_argument('--texture-trace',action='store_true')
r.add_argument('--sample-neon',choices=('0','1'),default='0')
r.add_argument('--input-probe',action='store_true')
r.add_argument('--opaque-fade',choices=('0','1'),default='0')
r.add_argument('--upload-neon',choices=('0','1'),default='0')
r.add_argument('--cover-fixture',action='store_true',help='Own deterministic 1000-entry/160-cover catalog; never launches a ROM')
r.add_argument('--core-fixture',action='store_true',help='Only the original no-battery video probe, in an isolated catalog')
r.add_argument('--fixture-view',choices=('grid','detailed'),help='Only the isolated fixture HOME; production view remains unchanged')
r.add_argument('--scaled-mb',choices=('40','48','56','64'),help='Existing sampled-image cache budget; omitted preserves the renderer default')
r.add_argument('--copy-pld-pair',choices=('0','1'),default='0')
r.add_argument('--alpha-scan',choices=('0','1'),default='0')
r.add_argument('--trace-redraws-only',action='store_true')
r.add_argument('--quality-fixed-dt',action='store_true',help='Only the alpha-verified quality build; never a timing run')
r.add_argument('--cold-start',action='store_true',help='Do not read/hash the ELF before exec; verify known hash after exit')
r.add_argument('--expected-binary-sha',help='Required SHA-256 from the previously uploaded artifact for --cold-start')
r.add_argument('--wrapper',action='store_true',help='Run an isolated copy of the real wrapper, preserving its waits/recovery')
s=sub.add_parser('status');s.add_argument('label')
c=sub.add_parser('collect');c.add_argument('label')
sub.add_parser('restore')
args=p.parse_args()
if getattr(args,'fixture_view',None) and not args.cover_fixture:p.error('--fixture-view requires --cover-fixture')
if getattr(args,'core_fixture',False) and args.cover_fixture:p.error('Choose one isolated fixture')
if getattr(args,'cold_start',False):
    import re
    if not args.expected_binary_sha or not re.fullmatch('[0-9a-f]{64}',args.expected_binary_sha):p.error('--cold-start requires a known lowercase SHA-256')
if hasattr(args,'label') and not args.label.replace('-','').replace('_','').isalnum():raise ValueError('invalid label')
if getattr(args,'binary_label',None) and not args.binary_label.replace('-','').replace('_','').isalnum():raise ValueError('invalid binary label')
if args.action=='upload':
    remote(f'import pathlib;p=pathlib.Path({(REMOTE+"/bin")!r});p.mkdir(parents=True,exist_ok=True)')
    transfer(args.binary,REMOTE+'/bin/'+args.label)
    remote(f'import pathlib;p=pathlib.Path({(REMOTE+"/bin/"+args.label)!r});p.chmod(0o755);print(p.stat().st_size)')
elif args.action=='run':
    script='wait 14000\nlog navigation-start\nstats\n'
    for i in range(args.steps):script+='key '+('Right' if i%8<4 else 'Left')+'\nwait 450\n'
    script+='wait 1500\nlog navigation-end\nstats\nquit\n'
    if args.script:script=pathlib.Path(args.script).read_text()
    wrapper_ini='SIZE='+args.size+'\nFPS=60\nSTRIP=8\nPLD=256\nBENCH=0\nPROFILE=0\nSLOW_DUMPS=0\n'
    code=PROCESS_GUARD+f'''
import pathlib,os,signal,time,subprocess,shutil,urllib.request,json,hashlib
base=pathlib.Path({REMOTE!r});base.mkdir(exist_ok=True)
original=pathlib.Path('/media/fat/emulationstation')
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU','Core changed: no test started'
before_owners,before_launchers=owners_and_launchers()
if {args.cold_start!r}:assert not before_owners and not before_launchers,'Cold opening would follow an already-started frontend'
stop_production()
assert owners_and_launchers()==([],[]),'A frontend or delayed launcher appeared'
run=base/{args.label!r};run.mkdir(exist_ok=True)
launch_begin=time.monotonic_ns()
home=pathlib.Path('/tmp')/('codex-perf-'+{args.label!r})
config=home/'.emulationstation';config.mkdir(parents=True,exist_ok=True)
for n in ('es_settings.cfg','es_input.cfg','es_systems.cfg'):
    shutil.copy2(original/'home/.emulationstation'/n,config/n)
if {args.cover_fixture or args.core_fixture!r}:
    import xml.etree.ElementTree as E
    fixture=base/('core-fixture' if {args.core_fixture!r} else 'cover-fixture')
    if {args.core_fixture!r}:
        assert hashlib.sha256((fixture/'probe.nes').read_bytes()).hexdigest()=='ff4228a6e59bda2a4d018bbbe0f4e9f0c0a9f305760d51fd57213bf9b616c1e5','Wrong video probe'
        assert len(list(fixture.glob('*.nes')))==1,'Core probe catalog must have one original program'
        assert (base/'core-launcher/mister-launch.sh').is_file(),'Prepare the isolated core launcher first'
    else:
        assert (fixture/'gamelist.xml').is_file() and len(list((fixture/'images').glob('*.png')))==160,'Install the isolated fixture first'
    tree=E.parse(config/'es_settings.cfg');settings=tree.getroot()
    choices=[('string','StartupSystem','nes'),('bool','StartupOnGameList','true'),('bool','IgnoreGamelist','false')]
    if {args.fixture_view is not None!r}:choices.append(('string','nes.defaultView',{args.fixture_view!r}))
    for kind,name,value in choices:
        node=next((n for n in settings if n.get('name')==name),None)
        if node is None:node=E.SubElement(settings,kind,{{'name':name}})
        node.set('value',value)
    tree.write(config/'es_settings.cfg',encoding='utf-8',xml_declaration=True)
    systems=E.Element('systemList');system=E.SubElement(systems,'system')
    launch_command='bash '+str(base/'core-launcher/mister-launch.sh')+' %SYSTEM% %ROM%' if {args.core_fixture!r} else '/bin/false'
    for name,value in [('name','nes'),('fullname','NES performance fixture'),('path',str(fixture)),('extension','.nes'),('command',launch_command),('platform','nes'),('theme','nes')]:E.SubElement(system,name).text=value
    E.ElementTree(systems).write(config/'es_systems.cfg',encoding='utf-8',xml_declaration=True)
themes=config/'themes'
if not themes.exists(): themes.symlink_to(original/'home/.emulationstation/themes',target_is_directory=True)
script=run/'navigation.txt';script.write_text({script!r})
helper_begin=time.monotonic_ns()
if not {args.wrapper!r}:
    for attempt in range(3):
        helper=base/'bin'/'helper'
        if not helper.exists(): helper=original/'es-mister-helper'
        console=subprocess.run([str(helper),'console',{args.size!r},'8'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
        print('console',attempt,console.returncode,console.stdout,flush=True)
        if console.returncode==0:break
        time.sleep(2)
    if console.returncode: raise RuntimeError('Console unavailable')
helper_end=time.monotonic_ns()
env=dict(os.environ,HOME=str(home),SDL_AUDIODRIVER='alsa',SDL_EVDEV_IGNORE='MiSTer virtual input,Zaparoo',
    ES_SOFT_FPS='60',ES_SOFT_STRIP='8',ES_SOFT_PLD='256',ES_SOFT_STATS='600',
    ES_SOFT_STATS_FILE=str(run/'stats.txt'),ES_STARTUP_TRACE=str(run/'startup.log'),
    ES_SOFT_SCRIPT=str(script),ES_SOFT_SHOTS=str(run),ES_ARCADE_SKIP='cores _organized mame hbmame _alternatives')
for k in ('ES_SOFT_BENCH','ES_SOFT_SLOW_DUMP','ES_PROFILE','ES_SOFT_DUMP','ES_SOFT_DIRTY_CHECK','ES_SOFT_TRACE','ES_TEXTURE_THREADS','ES_SYSTEM_THREADS','ES_TEXTURE_TRACE','ES_STARTUP_INPUT_PROBE','ES_AUDIT_FIXED_DT'):
    env.pop(k,None)
if not {args.trace_off!r}: env['ES_SOFT_TRACE']=str(run/'trace.csv')
if {args.profile!r}: env['ES_PROFILE']=str(run/'cpu-profile.txt')
env['ES_SOFT_TRACE_DETAIL']={str(args.detail)!r}
env['ES_SOFT_OPTIMIZATIONS']={args.optimizations!r}
env['ES_SOFT_THREADS']={args.threads!r}
env['ES_SOFT_TILE_X']={str(args.tile)!r}
env['ES_SOFT_COMPOSITE']={args.composite!r}
env['ES_SOFT_PACKED']={args.packed!r}
env['ES_SOFT_SAMPLE_NEON']={args.sample_neon!r}
env['ES_SOFT_OPAQUE_FADE']={args.opaque_fade!r}
env['ES_SOFT_UPLOAD_NEON']={args.upload_neon!r}
env['ES_SOFT_COPY_PLD_PAIR']={args.copy_pld_pair!r}
env['ES_SOFT_ALPHA_SCAN']={args.alpha_scan!r}
if {args.scaled_mb is not None!r}:env['ES_SOFT_SCALED_MB']={args.scaled_mb!r}
if {args.trace_redraws_only!r}:env['ES_SOFT_TRACE_REDRAWS_ONLY']='1'
if {args.check!r}: env['ES_SOFT_DIRTY_CHECK']='1'
if {args.quality_fixed_dt!r}:
    assert {args.check!r},'Fixed logical time is restricted to quality verification'
    env['ES_AUDIT_FIXED_DT']='100'
if {args.system_threads is not None!r}: env['ES_SYSTEM_THREADS']={args.system_threads!r}
if {args.texture_threads is not None!r}: env['ES_TEXTURE_THREADS']={args.texture_threads!r}
if {args.texture_trace!r}: env['ES_TEXTURE_TRACE']=str(run/'textures.csv')
if {args.input_probe!r}: env['ES_STARTUP_INPUT_PROBE']='1'
if {args.core_fixture!r}: env['ES_WRAPPER_TRACE']=str(run/'core-wrapper.log')
binary=original/'emulationstation' if {args.installed!r} else base/'bin'/{(args.binary_label or args.label)!r}
binary_bytes=None if {args.cold_start!r} else binary.read_bytes()
if {args.quality_fixed_dt and not args.cold_start!r}:assert b'ES_AUDIT_FIXED_DT' in binary_bytes,'Missing quality-only logical clock'
if {args.system_threads is not None and args.system_threads!='4' and not args.cold_start!r}:
    assert b'ES_SYSTEM_THREADS' in binary_bytes,'This binary does not expose system worker control'
if {args.texture_threads=='1' and not args.cold_start!r}:
    assert b'ES_TEXTURE_THREADS' in binary_bytes,'This binary does not expose texture worker control'
if {args.input_probe and not args.cold_start!r}:assert b'input selection result' in binary_bytes,'Missing logical input probe'
if {args.sample_neon=='1' and not args.cold_start!r}:assert b'ES_SOFT_SAMPLE_NEON' in binary_bytes,'Missing sampling experiment'
if {args.opaque_fade=='1' and not args.cold_start!r}:assert b'ES_SOFT_OPAQUE_FADE' in binary_bytes,'Missing opaque fade experiment'
if {args.upload_neon=='1' and not args.cold_start!r}:assert b'ES_SOFT_UPLOAD_NEON' in binary_bytes,'Missing upload experiment'
if {args.copy_pld_pair=='1' and not args.cold_start!r}:assert b'ES_SOFT_COPY_PLD_PAIR' in binary_bytes,'Missing copy prefetch experiment'
if {args.alpha_scan=='1' and not args.cold_start!r}:assert b'ES_SOFT_ALPHA_SCAN' in binary_bytes,'Missing alpha scan experiment'
expected_sha={args.expected_binary_sha!r} if {args.cold_start!r} else hashlib.sha256(binary_bytes).hexdigest()
(run/'configuration.json').write_text(json.dumps(dict(binary=str(binary),
    binary_sha256=expected_sha,elf_read_before_spawn=not {args.cold_start!r},wrapper={args.wrapper!r},
    boot_id=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),uptime_before_spawn_s=float(pathlib.Path('/proc/uptime').read_text().split()[0]),
    frontend_owners_before_setup=before_owners,frontend_launchers_before_setup=before_launchers,
    config_sha256={{n:hashlib.sha256((config/n).read_bytes()).hexdigest() for n in ('es_settings.cfg','es_input.cfg','es_systems.cfg')}},
    renderer_environment={{k:v for k,v in env.items() if k.startswith(('ES_SOFT_','ES_TEXTURE_','ES_SYSTEM_','SDL_EVDEV_','ES_STARTUP_INPUT_','ES_AUDIT_'))}},
    requested_size={args.size!r},cover_fixture={args.cover_fixture!r},core_fixture={args.core_fixture!r},fixture_view={args.fixture_view!r},framebuffer_size=pathlib.Path('/sys/class/graphics/fb0/virtual_size').read_text().strip(),
    core=pathlib.Path('/tmp/CORENAME').read_text().strip()),indent=2))
out=(run/'stdout.txt').open('w')
command=[str(binary)];working=original
if {args.wrapper!r}:
    wrapper=base/'bin/es-mister-traced-wrapper'
    assert wrapper.is_file(),'Upload the active es-mister.sh as es-mister-traced-wrapper first'
    # The SD is exFAT. Runtime links belong to this owned tmpfs HOME.
    package=home/'package';package.mkdir(exist_ok=True)
    (config/'es_systems.cfg.keep').touch()
    for name,target in [('home',home),('logs',run),('emulationstation',binary),('es-mister.sh',wrapper),('es-mister-helper',original/'es-mister-helper'),('systems.tsv',original/'systems.tsv'),('defaults',original/'defaults'),('resources',original/'resources'),('locale',original/'locale')]:
        (package/name).symlink_to(target,target_is_directory=target.is_dir())
    # Mirror the remaining application assets/config descriptors too. Changing
    # the wrapper's cwd must not silently omit files such as es_features.cfg.
    for entry in original.iterdir():
        target=package/entry.name
        if entry.name in ('es-mister.ini',base.name) or target.exists() or target.is_symlink():continue
        target.symlink_to(entry,target_is_directory=entry.is_dir())
    (package/'es-mister.ini').write_text({wrapper_ini!r})
    env['ES_WRAPPER_TRACE']=str(run/'wrapper.log')
    command=['bash',str(package/'es-mister.sh')];working=package
spawn_begin=time.monotonic_ns()
proc=subprocess.Popen(command,cwd=working,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
wrapper_spawn=spawn_begin if {args.wrapper!r} else None
pid=proc.pid
if {args.wrapper!r}:
    (run/'wrapper-pid').write_text(str(proc.pid))
    deadline=time.monotonic()+70
    while time.monotonic()<deadline:
        owners,launchers=owners_and_launchers()
        assert len(owners)<=1,'Multiple owners while wrapper starts'
        if owners:
            assert owners[0][1]==str(binary),'Unexpected frontend while wrapper starts'
            pid=owners[0][0];break
        if proc.poll() is not None:raise RuntimeError('Wrapper exited before spawning the frontend')
        time.sleep(.02)
    else:raise RuntimeError('Wrapper did not start a frontend')
    # /proc starttime is clock ticks since boot. Align BOOTTIME with MONOTONIC;
    # precision is one kernel tick (10 ms here), not the per-frame trace clock.
    fields=pathlib.Path('/proc',str(pid),'stat').read_text().rsplit(')',1)[1].split()
    offset=time.monotonic_ns()-time.clock_gettime_ns(time.CLOCK_BOOTTIME)
    spawn_begin=int(int(fields[19])*1000000000/os.sysconf('SC_CLK_TCK'))+offset
    marks=[line.split(maxsplit=1) for line in (run/'wrapper.log').read_text().splitlines()]
    helper_begin=int(float(next(t for t,event in marks if event.startswith('helper_begin')))*1e9)+offset
    helper_end=int(float(next(t for t,event in marks if event.startswith('helper_end') and 'result=ok' in event))*1e9)+offset
    config_data=json.loads((run/'configuration.json').read_text())
    config_data['framebuffer_size']=pathlib.Path('/sys/class/graphics/fb0/virtual_size').read_text().strip()
    child_env=dict(pair.decode(errors='replace').split('=',1) for pair in pathlib.Path('/proc',str(pid),'environ').read_bytes().split(b'\\0') if b'=' in pair)
    config_data['renderer_environment']={{k:v for k,v in child_env.items() if k.startswith(('ES_SOFT_','ES_TEXTURE_','ES_SYSTEM_','SDL_EVDEV_','ES_STARTUP_INPUT_','ES_AUDIT_'))}}
    (run/'configuration.json').write_text(json.dumps(config_data,indent=2))
owners,launchers=owners_and_launchers()
if (launchers and not {args.wrapper!r}) or owners!=[(pid,str(binary))] or ({args.wrapper!r} and launchers!=[proc.pid]):
    proc.terminate()
    raise RuntimeError('Unexpected framebuffer owner after spawn: '+repr((owners,launchers)))
# Scripts must also see the isolated process and refuse an accidental second start.
if not {args.wrapper!r}:pathlib.Path('/tmp/es-mister.pid').write_text(str(pid))
(run/'launcher-timeline.json').write_text(json.dumps(dict(launch_begin_ns=launch_begin,helper_begin_ns=helper_begin,
    helper_end_ns=helper_end,spawn_begin_ns=spawn_begin,spawn_end_ns=time.monotonic_ns(),wrapper_spawn_ns=wrapper_spawn,
    process_start_precision_ns=int(1000000000/os.sysconf('SC_CLK_TCK')) if {args.wrapper!r} else None),indent=2))
(run/'pid').write_text(str(pid))
print('started',pid,'isolated HOME',home)
if {args.kernel_probe!r}:
    probe=base/'bin/kernel_probe'
    if not probe.is_file(): raise RuntimeError('Upload tests/audit/kernel_probe.py as kernel_probe first')
    subprocess.Popen(['python3',str(probe),'sample','--pid',str(pid),'--output',str(run/'kernel-samples.json')],
        stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
monitor="""import pathlib,time,json,sys,os,signal,hashlib
pid=int(sys.argv[1]);out=pathlib.Path(sys.argv[2]);rows=[];start=time.monotonic()
while pathlib.Path('/proc/'+str(pid)).exists():
 try:
  values={{}}
  for line in pathlib.Path('/proc/'+str(pid)+'/status').read_text().splitlines():
   key=line.split(':',1)[0]
   if key in ('VmRSS','VmHWM','Threads'):values[key]=int(line.split()[1])
  owners=[]
  for entry in pathlib.Path('/proc').iterdir():
   if not entry.name.isdigit():continue
   try:exe=(entry/'exe').resolve(strict=True)
   except OSError:continue
   if exe==pathlib.Path('/media/fat/emulationstation/emulationstation') or exe.parent==pathlib.Path('/media/fat/emulationstation/codex-performance-20261113/bin'):owners.append(int(entry.name))
  values['framebuffer_owners']=len(owners)
  values['seconds']=round(time.monotonic()-start,2);rows.append(values)
  if len(owners)>1:
   (out.parent/'framebuffer-collision.json').write_text(json.dumps(dict(owners=owners,at_ns=time.monotonic_ns())))
   os.kill(pid,signal.SIGTERM) # Stop only this owned benchmark, never the production process.
   break
 except (FileNotFoundError,ProcessLookupError):break
 time.sleep(1)
actual=hashlib.sha256(pathlib.Path(sys.argv[3]).read_bytes()).hexdigest()
configuration=out.parent/'configuration.json';config=json.loads(configuration.read_text())
config['binary_sha_verified_after_run']=actual==sys.argv[4]
configuration.write_text(json.dumps(config,indent=2))
if actual!=sys.argv[4]:(out.parent/'binary-hash-mismatch.txt').write_text(actual)
out.write_text(json.dumps(rows))
"""
subprocess.Popen(['python3','-c',monitor,str(pid),str(run/'memory.json'),str(binary),expected_sha],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
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
with tarfile.open(str(p)+'.tar.gz','w:gz') as tar:
 tar.add(p,arcname=p.name,recursive=False)
 for entry in p.iterdir():
  if entry.name=='package':continue # Runtime links to shared assets and home; no recursive link archive.
  source=entry
  if entry.is_symlink():
   source=entry.resolve(strict=True)
   assert source.parent==p and source.is_file(),'Unexpected diagnostic symlink'
  tar.add(source,arcname=p.name+'/'+entry.name)
print('collected',p)
''')
    dest=pathlib.Path(__file__).resolve().parent/'target-results';dest.mkdir(exist_ok=True)
    win=subprocess.check_output(['wslpath','-w',str(dest)],text=True).strip()
    subprocess.run([SCP,'-i',KEY,'-o','BatchMode=yes',HOST+':'+REMOTE+'/'+args.label+'.tar.gz',win],check=True)
    import tarfile
    with tarfile.open(dest/(args.label+'.tar.gz')) as tar:tar.extractall(dest,filter='data')
elif args.action=='restore':
    remote(PROCESS_GUARD+'''import subprocess,pathlib
owners,launchers=owners_and_launchers()
assert all(exe=='/media/fat/emulationstation/emulationstation' for pid,exe in owners),'Candidate still running'
p=pathlib.Path('/media/fat/emulationstation')
if not owners and not launchers:
    out=(p/'logs/codex-restore.log').open('a')
    proc=subprocess.Popen(['bash',str(p/'es-mister.sh')],stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
print('original frontend running; API ready',restored_ready())
''')
