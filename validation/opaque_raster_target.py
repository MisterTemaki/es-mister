"""Independent ARM pixels and isolated cold sampling ABBA; GUI is closed."""
import pathlib,subprocess,sys
v=pathlib.Path(__file__).resolve().parent
def run(*args):subprocess.run([sys.executable,str(v/'target_run.py'),*args],check=True)
for source,label in [('reference-arm','stage3-opaque-reference'),('opaque-arm','stage3-opaque-candidate')]:
    run('upload',str(v/'raster-stage3'/source),label)
code='''
import pathlib,subprocess,os,json,time,urllib.request
base=pathlib.Path('/media/fat/emulationstation/codex-performance-20261113')
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
for p in pathlib.Path('/proc').iterdir():
 if not p.name.isdigit():continue
 try:exe=(p/'exe').resolve(strict=True)
 except OSError:continue
 assert exe.parent!=base/'bin','Candidate frontend still running'
try:urllib.request.urlopen('http://127.0.0.1:1234/quit',timeout=5).read()
except Exception:pass
for _ in range(150):
 if not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip():break
 time.sleep(.2)
assert not subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE).stdout.strip()
dest=base/'opaque-raster';dest.mkdir(exist_ok=True)
env=dict(os.environ,ES_SOFT_PLD='256',ES_SOFT_SCALED_MB='40',ES_SOFT_OPAQUE_FADE='1')
quality={}
for mode in ('normal','composite'):
 records=[]
 for binary in ('stage3-opaque-reference','stage3-opaque-candidate'):
  result=subprocess.run([str(base/'bin'/binary)]+(['composite'] if mode=='composite' else []),env=env,stdout=subprocess.PIPE,text=True,timeout=120,check=True)
  (dest/(binary+'-'+mode+'.txt')).write_text(result.stdout)
  records.append([line.split(' hash ')[1].split()[0] for line in result.stdout.splitlines()])
 assert records[0]==records[1],'INDEPENDENT ARM PIXELS DIFFER'
 quality[mode]=len(records[0])
print('INDEPENDENT ARM PIXELS MATCH',json.dumps(quality),flush=True)
bench=[]
for index,enabled in enumerate(('0','1','1','0')):
 env['ES_SOFT_OPAQUE_FADE']=enabled
 result=subprocess.run([str(base/'bin/stage3-opaque-candidate'),'bench'],env=env,stdout=subprocess.PIPE,text=True,timeout=180,check=True)
 (dest/('cold-'+str(index)+'-opt'+enabled+'.txt')).write_text(result.stdout)
 rows=[dict(scene=int(line.split()[1]),hash=line.split(' hash ')[1].split()[0],ms=float(line.split('median_ms ')[1])) for line in result.stdout.splitlines()]
 if bench:assert [r['hash'] for r in rows]==[r['hash'] for r in bench[0]['rows']]
 bench.append(dict(enabled=enabled,rows=rows));print('WARM RESULT',enabled,json.dumps(rows),flush=True)
(dest/'summary.json').write_text(json.dumps(dict(quality=quality,cold_abba=bench),indent=2))
'''
try:
    subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-T','-i',r'C:\Users\USER\.ssh\id_ed25519_mister',
                    '-o','BatchMode=yes','-o','ConnectTimeout=8','root@192.168.15.14','python3 -'],input=code,text=True,check=True)
finally:
    run('restore')
