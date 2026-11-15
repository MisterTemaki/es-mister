import pathlib,zipfile,hashlib,json,re,subprocess,tempfile,shutil,difflib,collections,statistics
work=pathlib.Path(__file__).resolve().parent.parent
original=pathlib.Path('/mnt/d/FPGA/Frontend/Mistercera')
source=original/'es-mister'
output=work/'es-mister/audit/performance-1080p'
output.mkdir(parents=True,exist_ok=True)
digest=lambda data:hashlib.sha256(data).hexdigest()
with zipfile.ZipFile(original/'es-mister-src-5b5c7bc.zip') as z:
    archived={i.filename.removeprefix('es-mister/'):z.read(i) for i in z.infolist() if not i.is_dir()}
comparison=[]
for name,data in archived.items():
    local=source/name
    if not local.exists() or local.read_bytes()!=data:
        comparison.append({'path':name,'zip_sha256':digest(data),'local_sha256':digest(local.read_bytes()) if local.exists() else None})
with zipfile.ZipFile(original/'es-mister-patch-29.zip') as z:
    patch=z.read(z.infolist()[0])
paths=re.findall(r'^diff --git a/(.+) b/',patch.decode(),re.M)
(output/'patch-29.patch').write_bytes(patch)
with tempfile.TemporaryDirectory(prefix='es-perf-reference-') as folder:
    stage=pathlib.Path(folder)
    for name in paths:
        assert '..' not in pathlib.PurePosixPath(name).parts and not name.startswith('/')
        dest=stage/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes((source/name).read_bytes())
    subprocess.run(['git','apply','--check',str(output/'patch-29.patch')],cwd=stage,check=True)
    subprocess.run(['git','apply',str(output/'patch-29.patch')],cwd=stage,check=True)
    diffs=[]
    for name in paths:
        dest=output/'referencia-instalada'/name
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(stage/name,dest)
        diffs.extend(difflib.unified_diff(archived[name].decode().splitlines(True),(stage/name).read_text().splitlines(True),fromfile='ZIP/'+name,tofile='instalada-reconstituida/'+name))
    (output/'comparacao-zip-instalada.diff').write_text(''.join(diffs))
for name in ('es-mister-stats.txt','es-startup.log','es-startup.prev.log','es-mister-bench.txt','es-mister.log',*[f'es-slow-frame-{i}.txt' for i in range(1,5)]):
    dest=output/'logs'/name;dest.parent.mkdir(exist_ok=True);shutil.copy2(original/'logs'/name,dest)
installed=(original/'es-mister-7ad64ca-18/emulationstation/emulationstation').read_bytes()
version={'local_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip(),
         'device_binary_sha256_observed':'253a15744237071c9bc136d94666957cc4567fbbe8ebf349ce1a10cd86daf404',
         'package18_binary_sha256':digest(installed),'patch29_sha256':digest(patch),
         'zip_vs_local_differences':comparison,'reconstructed_paths':paths}
assert version['device_binary_sha256_observed']==version['package18_binary_sha256']
(output/'comparacao.json').write_text(json.dumps(version,indent=2,ensure_ascii=False))

results={}
slow=[]
for line in (output/'logs/es-startup.log').read_text().splitlines():
    m=re.search(r'^\s*([\d.]+) \[(\d+)\] slow frame: ES ([\d.]+) ms before it, drawing ([\d.]+) ms \(([\d.]+)% of the screen\)',line)
    if m: slow.append(dict(zip(('time','pid','es_ms','drawing_ms','screen_pct'),map(float,m.groups()))))
results['slow_frames']=slow
results['slow_summary']={'count':len(slow),'full_frames':sum(s['screen_pct']==100 for s in slow),
                         'drawing_min':min(s['drawing_ms'] for s in slow) if slow else None,
                         'drawing_max':max(s['drawing_ms'] for s in slow) if slow else None}
stats=[]
for line in (output/'logs/es-mister-stats.txt').read_text().splitlines():
    m=re.search(r'(\d+) frames, (\d+) drawn; frame ([\d.]+) ms \(max ([\d.]+)\), ES before a drawn frame ([\d.]+) ms \(max ([\d.]+)\), drawing ([\d.]+) ms \(max ([\d.]+)\).*?copy ([\d.]+) ms',line)
    if m:
        row=dict(zip(('frames','drawn','frame_ms','frame_max','es_ms','es_max','drawing_ms','drawing_max','copy_ms'),map(float,m.groups())))
        row['timestamp']=line.split()[0]
        stats.append(row)
results['stats_windows']=stats
results['stats_summary']={'windows':len(stats),'frames':sum(s['frames'] for s in stats),'drawn':sum(s['drawn'] for s in stats),
                          'idle_windows':sum(s['drawn']==0 for s in stats),'active_windows':sum(s['drawn']>0 for s in stats)}
# The file is append-only and contains several old package versions. Package 18 was copied at 19:30.
results['package18_stats_windows']=[s for s in stats if s['timestamp']>='19:30:00']
dump_summary=[]
for i in range(1,5):
    text=(output/f'logs/es-slow-frame-{i}.txt').read_text()
    commands=[]
    for line in text.splitlines()[1:]:
        m=re.match(r'\s*(\d+)\s+([\d.]+) ms (.*?): (.*)',line)
        if m: commands.append({'id':int(m[1]),'serial_ms':float(m[2]),'description':m[3],'pixels':m[4]})
    top=sorted(commands,key=lambda c:c['serial_ms'],reverse=True)
    dump_summary.append({'dump':i,'header':text.splitlines()[0],'serial_sum_ms':sum(c['serial_ms'] for c in commands),
                         'commands':commands,'top3_serial_ms':sum(c['serial_ms'] for c in top[:3])})
results['dumps']=dump_summary
(output/'analise-dos-logs.json').write_text(json.dumps(results,indent=2,ensure_ascii=False))
print('ZIP vs local:',comparison)
print('Installed matches package 18:',version['package18_binary_sha256'])
print('Patch 29 applied to temporary reference only:',paths)
print('Slow:',results['slow_summary'])
print('Stats:',results['stats_summary'])
print('Package 18 windows:',results['package18_stats_windows'])
for d in dump_summary: print('Dump',d['dump'],d['header'],'serial_sum',round(d['serial_sum_ms'],3),'top3',round(d['top3_serial_ms'],3))
