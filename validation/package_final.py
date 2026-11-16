"""Package accepted sources, reproducible tools and separate measured evidence."""
import argparse,hashlib,json,pathlib,shutil,subprocess,zipfile
V=pathlib.Path(__file__).resolve().parent;A=V.parent;R=A/'es-mister'
SHA='872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55'
p=argparse.ArgumentParser();p.add_argument('--prepare-update',action='store_true');args=p.parse_args()
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def archive(dest,items):
    with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=6,strict_timestamps=False) as z:
        for path,name in sorted(items,key=lambda item:item[1]):z.write(path,name)
    with zipfile.ZipFile(dest) as z:assert z.testzip() is None
    print(dest.name,dest.stat().st_size,digest(dest),flush=True)
package=V/'update-final-20261115';payload=package/'payload';payload.mkdir(parents=True,exist_ok=True)
binary=V/'arm/emulationstation-stage3-final-candidate-one-decoder-stripped'
assert digest(binary)==SHA
helper=V/'arm/es-mister-helper';assert digest(helper)=='c826dc9341aa47947e25230d8be51ca1b80882a4dc8e11296364d8bb785288b8'
files={'emulationstation':binary,'es-mister-helper':helper,'es-mister.sh':R/'mister/scripts/es-mister.sh',
       'mister-launch.sh':R/'mister/scripts/mister-launch.sh','README.txt':R/'mister/README.txt'}
for name,source in files.items():shutil.copy2(source,payload/name)
shutil.copy2(R/'mister/tools/install-update.py',package/'install-update.py')
(package/'manifest.json').write_text(json.dumps({n:digest(payload/n) for n in files},indent=2))
subprocess.run(['python3',str(package/'install-update.py'),'--check'],check=True,stdout=subprocess.DEVNULL)
update=A/'es-mister-1080p-update-final-20261115.zip'
if args.prepare_update:
    archive(update,[(path,'codex-update-final-1080p/'+path.relative_to(package).as_posix()) for path in package.rglob('*') if path.is_file() and '__pycache__' not in path.parts])
else:
    # Preserve the exact archive already installed, including its ZIP metadata.
    # Verify it still contains the current payload/installer before distributing.
    assert update.is_file(),'Prepare and deploy the accepted update first'
    with zipfile.ZipFile(update) as z:
        prefix='codex-update-final-1080p/'
        assert z.read(prefix+'manifest.json')==(package/'manifest.json').read_bytes()
        assert z.read(prefix+'install-update.py')==(package/'install-update.py').read_bytes()
        for name in files:assert hashlib.sha256(z.read(prefix+'payload/'+name)).hexdigest()==digest(payload/name)
if args.prepare_update:raise SystemExit(0)
required=['final-navigation-current-v1-summary.json','final-controls-v1-summary.json','final-idle-v1-summary.json','final-animations-v1-transitions.json','startup-final-conditions.json','startup-full-warm-v1-summary.json','startup-full-cold-v1-summary.json','deployment-final-20261115.json','installed-final-verification.json']
assert all((V/name).is_file() for name in required),'Do not label a partial acceptance complete'
quality={}
for name in ('upload-logical-v2-1-u0','upload-logical-v2-2-u1'):
    directory=V/'target-results'/name
    quality[name]={path.name:digest(path) for path in directory.glob('*.png')}
assert len(quality['upload-logical-v2-1-u0'])==18 and quality['upload-logical-v2-1-u0']==quality['upload-logical-v2-2-u1'],'Included quality captures are not equal'
(V/'quality-archive-verification.json').write_text(json.dumps(dict(png_sha256=quality,equal_states=18),indent=2))
tracked=subprocess.check_output(['git','-C',str(R),'ls-files','--cached','--others','--exclude-standard','-z']).decode().split('\0')
items=[]
for name in sorted(set(tracked)):
    path=R/name
    if name and path.is_file():items.append((path,'es-mister-audit/es-mister/'+name))
for path in V.iterdir():
    if path.is_file() and path.suffix in ('.py','.ps1','.sh','.txt'):
        items.append((path,'es-mister-audit/validation/'+path.name))
for name in ('codex-video-probe.nes','codex-video-probe.json','cover-fixture-manifest.json'):
    path=V/name
    if path.is_file():items.append((path,'es-mister-audit/validation/'+name))
for name in ('emulationstation-stage3-final-reference.source.json','emulationstation-stage3-final-candidate-one-decoder.source.json'):
    items.append((V/'arm'/name,'es-mister-audit/validation/arm/'+name))
source=A/'es-mister-1080p-sources-final-20261115.zip';archive(source,items)
compiled_manifest=json.loads((V/'arm/emulationstation-stage3-final-candidate-one-decoder.source.json').read_text())
compiled=[n for n in compiled_manifest if n.startswith(('es-app/','es-core/')) and pathlib.Path(n).suffix in ('.cpp','.h','.hpp')]
with zipfile.ZipFile(source) as z:
    assert len(z.namelist())==len(set(z.namelist()))
    for n in compiled:assert hashlib.sha256(z.read('es-mister-audit/es-mister/'+n)).hexdigest()==compiled_manifest[n],n
(V/'source-artifact-verification.json').write_text(json.dumps(dict(compiled_cpp_headers_matched=len(compiled),accepted_arm_sha256=SHA,source_zip_sha256=digest(source)),indent=2))
evidence=[]
for path in V.iterdir():
    if path.is_file() and path.suffix in ('.json','.log') and path.name not in ('cover-fixture-manifest.json','archive-original-timestamps.json'):
        evidence.append((path,'validation/'+path.name))
prefixes=('final-navigation-v1-','final-navigation-current-v1-','final-animations-v1-','final-controls-v1-','final-idle-v1-','startup-full-warm-v1-','startup-full-cold-v1-','covers-final-v2-','covers-source-final-v1-','covers-minimal-final-v1-','covers-selected-visible-v2-','final-core-v1','final-core-speed-v1','final-core-speed-v2')
for directory in (V/'target-results').iterdir():
    if not directory.is_dir() or not directory.name.startswith(prefixes):continue
    for path in directory.rglob('*'):
        if path.is_file() and path.suffix not in ('.png','.nes'):
            evidence.append((path,'validation/target-results/'+path.relative_to(V/'target-results').as_posix()))
for path in (V/'boot-events').glob('*.json'):evidence.append((path,'validation/boot-events/'+path.name))
for name in ('upload-logical-v2-1-u0','upload-logical-v2-2-u1'):
    directory=V/'target-results'/name
    for path in directory.glob('*'):
        if path.is_file():evidence.append((path,'validation/target-results/'+name+'/'+path.name))
legacy_times={name:path.stat().st_mtime_ns for path,name in evidence if path.stat().st_mtime<315532800}
(V/'archive-original-timestamps.json').write_text(json.dumps(dict(original_mtime_ns=legacy_times,note='ZIP clamps pre-1980 metadata to 1980. Original device timestamps are preserved here; measurement clocks inside logs remain unchanged.'),indent=2))
evidence.append((V/'archive-original-timestamps.json','validation/archive-original-timestamps.json'))
evidencezip=A/'es-mister-1080p-evidence-final-20261115.zip';archive(evidencezip,evidence)
with zipfile.ZipFile(evidencezip) as z:assert len(z.namelist())==len(set(z.namelist()))
outputs=(source,update,evidencezip)
record=dict(version='final-20261115',candidate_sha256=SHA,artifacts={x.name:dict(bytes=x.stat().st_size,sha256=digest(x)) for x in outputs},source_files=len(items),evidence_files=len(evidence))
(A/'es-mister-1080p-final-delivery-20261115.json').write_text(json.dumps(record,indent=2))
(A/'es-mister-1080p-SHA256SUMS-final-20261115.txt').write_text(''.join(digest(x)+'  '+x.name+'\n' for x in outputs))
