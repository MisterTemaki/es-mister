"""Package the Linux investigation, scripts and raw evidence; no frontend replacement."""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

base = Path(__file__).resolve().parent
root = base.parent
source = root / 'es-mister'
runs = ('kernel-nav-original-1', 'kernel-idle-pilot', 'kernel-idle-original-1', 'kernel-nav-original-2')
files = {}
for name in ('kernel_probe.py', 'idle_keyboard.py', 'kernel_analyze.py'):
    p = source / 'tests/audit' / name
    files['es-mister/tests/audit/' + name] = p.read_bytes()
for name in ('target_run.py', 'kernel_investigation.py', 'finalize_kernel.py', 'export_kernel.py',
             'kernel-investigation.log', 'kernel-investigation-resume.log', 'kernel-zone-readonly.log',
             'kernel-investigation-manual-completion.log', 'kernel-final-verification.log'):
    files['validation/' + name] = (base / name).read_bytes()
files['es-mister/audit/performance-1080p/LINUX.md'] = (source / 'audit/performance-1080p/LINUX.md').read_bytes()
for name in ('kernel-inventory-original.json', 'kernel-inventory-final.json', 'kernel-restoration.json'):
    p = base / 'target-results' / name
    files['validation/target-results/' + name] = p.read_bytes()
for name in runs:
    run = base / 'target-results' / name
    data = json.loads((run / 'kernel-summary.json').read_text())
    count = 1000 if '-nav-' in name else (2 if name.endswith('pilot') else 16)
    if data['input_sdl_to_first_redraw_ms']['n'] != count:
        raise RuntimeError('Unexpected response count: ' + name)
    for p in sorted(run.iterdir()):
        if p.is_file():
            files['validation/target-results/' + name + '/' + p.name] = p.read_bytes()
files['README.txt'] = '''Linux latency investigation on the MiSTer, 14/11/2026.
Open es-mister/audit/performance-1080p/LINUX.md for the method, results, limits and commands.
Contents: Python utilities, the bench's scripts and raw evidence.
Binary measured: installed version d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64.
Resolution 1920x1080, Carbon, 2 threads, STRIP=8, PLD=256, FPS=60.
No kernel tuning was applied. CPUFreq exposes no policies in this boot.
Use new labels to repeat a run without overwriting the previous results.
'''.encode('utf-8')
manifest = dict(date='2026-11-14',
                repository_commit_with_kernel_tools=subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
                measured_frontend_reference_commit='cba2b668f2309d426ebce65f2c69e572088780d3',
                measured_frontend_sha256='d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64',
                files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                       for name, data in sorted(files.items())})
files['manifest.json'] = json.dumps(manifest, indent=2, ensure_ascii=False).encode('utf-8')
files['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                               for name, data in sorted(files.items())).encode('utf-8')
out = root / 'es-mister-linux-latency-20261114.zip'
with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name, data in sorted(files.items()):
        info = zipfile.ZipInfo(name, (2026, 11, 14, 12, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, data)
with zipfile.ZipFile(out) as archive:
    if archive.testzip() is not None:
        raise RuntimeError('Archive verification failed')
    for name, data in files.items():
        if archive.read(name) != data:
            raise RuntimeError('Archive content mismatch: ' + name)
digest = hashlib.sha256(out.read_bytes()).hexdigest()
out.with_suffix('.zip.sha256').write_text(digest + '  ' + out.name + '\n')
print(json.dumps(dict(path=str(out), bytes=out.stat().st_size, sha256=digest, files=len(files)), indent=2))
