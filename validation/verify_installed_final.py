"""Read-only final verification of installed hashes, normal environment and 1080p."""
import json,pathlib,subprocess
from target_guard import PROCESS_GUARD
V=pathlib.Path(__file__).resolve().parent
record=json.loads((V/'deployment-final-20261115.json').read_text())
code=PROCESS_GUARD+f'''
import json,hashlib,xml.etree.ElementTree as E
app=pathlib.Path('/media/fat/emulationstation');expected={record['installed_sha256']!r}
for name,sha in expected.items():assert hashlib.sha256((app/name).read_bytes()).hexdigest()==sha,name
owners,wrappers=owners_and_launchers();assert len(owners)==1 and len(wrappers)==1
pid,exe=owners[0];assert exe==str(app/'emulationstation')
env=dict(x.decode(errors='replace').split('=',1) for x in pathlib.Path('/proc',str(pid),'environ').read_bytes().split(b'\\0') if b'=' in x)
for key in ('ES_SOFT_TRACE','ES_TEXTURE_TRACE','ES_SOFT_DIRTY_CHECK','ES_SOFT_BENCH','ES_PROFILE','ES_SOFT_SLOW_DUMP','ES_SOFT_SCRIPT','ES_STARTUP_INPUT_PROBE','ES_AUDIT_FIXED_DT'):assert key not in env,key
assert env['ES_SOFT_STRIP']=='8' and env['ES_SOFT_PLD']=='256' and env['ES_SOFT_FPS']=='60'
assert pathlib.Path('/sys/class/graphics/fb0/virtual_size').read_text().strip()=='1920,1080'
assert pathlib.Path('/sys/class/graphics/fb0/bits_per_pixel').read_text().strip()=='32'
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
startup=(app/'logs/es-startup.log').read_text()
assert ('['+str(pid)+'] first interactive frame shown') in startup
settings=E.parse(app/'home/.emulationstation/es_settings.cfg').getroot()
selected={{x.get('name'):x.get('value') for x in settings if x.get('name') in ('ThemeSet','TransitionStyle','MaxVRAM','GamelistViewStyle')}}
memory={{line.split(':')[0]:line.split(':')[1].strip() for line in pathlib.Path('/proc/meminfo').read_text().splitlines() if line.startswith(('MemAvailable:','SwapTotal:','SwapFree:'))}}
result=dict(binary=expected['emulationstation'],pid=pid,wrapper=wrappers[0],framebuffer='1920,1080',bits_per_pixel=32,
    first_interactive_frame=True,diagnostic_modes_off=True,renderer_environment={{k:v for k,v in env.items() if k.startswith('ES_SOFT_')}},
    theme_settings=selected,memory=memory,boot_id=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
    cpufreq_policies=[str(p) for p in pathlib.Path('/sys/devices/system/cpu/cpufreq').glob('policy*')],
    oom_kill=[line for line in pathlib.Path('/proc/vmstat').read_text().splitlines() if line.startswith('oom_kill')])
print(json.dumps(result))
'''
r=subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-T','-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14','python3 -'],input=code,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
if r.returncode:raise RuntimeError(r.stderr)
result=json.loads(r.stdout);(V/'installed-final-verification.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
