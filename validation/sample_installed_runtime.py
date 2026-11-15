"""Ten-second read-only observation of the normal installed process, without an input script."""
import json,pathlib,subprocess
V=pathlib.Path(__file__).resolve().parent
code=r'''
from pathlib import Path
import json,time,os
owners=[]
for p in Path('/proc').iterdir():
 if not p.name.isdigit():continue
 try:exe=(p/'exe').resolve(strict=True)
 except OSError:continue
 if exe==Path('/media/fat/emulationstation/emulationstation'):owners.append(p)
assert len(owners)==1
p=owners[0];env=dict(x.split(b'=',1) for x in (p/'environ').read_bytes().split(bytes.fromhex('00')) if b'=' in x)
assert b'ES_SOFT_SCRIPT' not in env and b'ES_SOFT_TRACE' not in env and b'ES_TEXTURE_TRACE' not in env
samples=[];identity=None
for i in range(11):
 fields=(p/'stat').read_text().rsplit(')',1)[1].split()
 if identity is None:identity=fields[19]
 assert fields[19]==identity,'Process changed'
 status={line.split(':')[0]:int(line.split()[1]) for line in (p/'status').read_text().splitlines() if line.startswith(('VmRSS:','VmHWM:'))}
 samples.append(dict(at_ns=time.monotonic_ns(),cpu_ticks=int(fields[11])+int(fields[12]),rss_kib=status['VmRSS'],hwm_kib=status['VmHWM']))
 if i<10:time.sleep(1)
first,last=samples[0],samples[-1];seconds=(last['at_ns']-first['at_ns'])/1e9
print(json.dumps(dict(pid=int(p.name),duration_s=round(seconds,3),cpu_percent_one_core_units=round(100*(last['cpu_ticks']-first['cpu_ticks'])/os.sysconf('SC_CLK_TCK')/seconds,3),rss_min_mib=round(min(s['rss_kib'] for s in samples)/1024,3),rss_max_mib=round(max(s['rss_kib'] for s in samples)/1024,3),hwm_mib=round(max(s['hwm_kib'] for s in samples)/1024,3),samples=samples,note='Normal installed mode without a benchmark input script; no programmed interaction. This is one short observation, not a before/after idle-CPU gain or a long-session leak test.')))
'''
r=subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-T','-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14','python3 -'],input=code,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
if r.returncode:raise RuntimeError(r.stderr)
data=json.loads(r.stdout);(V/'normal-runtime-sample.json').write_text(json.dumps(data,indent=2));print(json.dumps({k:v for k,v in data.items() if k!='samples'}))
