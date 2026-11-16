#!/usr/bin/env python3
"""Install the reviewed 1080p update on MiSTer, preserving data and a rollback copy."""
import argparse, fcntl, hashlib, json, os, pathlib, re, shutil, subprocess, time, urllib.request

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def running(app):
    found=[]
    for proc in pathlib.Path('/proc').iterdir():
        if not proc.name.isdigit(): continue
        try: exe=(proc/'exe').resolve(strict=True)
        except (OSError, RuntimeError): continue
        if exe==app/'emulationstation' or exe.parent==app/'codex-performance-20261113/bin':
            found.append(int(proc.name))
    return found

def launchers(app):
    wrapper=str(app/'es-mister.sh').encode()
    found=[]
    for proc in pathlib.Path('/proc').iterdir():
        if not proc.name.isdigit(): continue
        try: arguments=(proc/'cmdline').read_bytes().split(b'\0')
        except OSError: continue
        if wrapper in arguments: found.append(int(proc.name))
    return found

def stop(app):
    deadline=time.monotonic()+60
    requested=set();stable=0
    while time.monotonic()<deadline:
        owners=running(app)
        # An isolated benchmark is never an installation target.
        assert all((pathlib.Path('/proc')/str(pid)/'exe').resolve()==app/'emulationstation' for pid in owners),'Benchmark active'
        wrappers=launchers(app)
        if not owners and not wrappers:
            stable+=1
            if stable>=2:return
        else:stable=0
        if any(pid not in requested for pid in owners):
            try:
                urllib.request.urlopen('http://127.0.0.1:1234/quit',timeout=2).read()
                requested.update(owners)
            except OSError:pass
        time.sleep(.2)
    raise RuntimeError('Frontend or wrapper still active; no concurrent start permitted')

def start_ready(app,interactive=True):
    assert not running(app) and not launchers(app),'Another frontend/launcher is active'
    with (app/'logs/codex-update.log').open('a') as log:
        wrapper=subprocess.Popen(['bash',str(app/'es-mister.sh')],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    deadline=time.monotonic()+90
    while time.monotonic()<deadline:
        owners=running(app)
        assert len(owners)<=1,'Multiple frontend processes'
        trace=app/'logs/es-startup.log'
        text=trace.read_text(errors='replace') if trace.exists() else ''
        marker='first interactive frame shown' if interactive else 'ready'
        if len(owners)==1 and ('['+str(owners[0])+'] '+marker) in text:
            try:
                urllib.request.urlopen('http://127.0.0.1:1234/',timeout=2).read(1)
                assert pathlib.Path('/sys/class/graphics/fb0/virtual_size').read_text().strip()=='1920,1080'
                return dict(launcher_pid=wrapper.pid,frontend_pid=owners[0],api_ready=True,framebuffer='1920,1080')
            except OSError:pass
        if wrapper.poll() is not None:raise RuntimeError('Wrapper exited before readiness')
        time.sleep(.2)
    raise RuntimeError('Frontend did not become ready')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--check',action='store_true',help='Verify the payload without installing')
    args=parser.parse_args()
    package=pathlib.Path(__file__).resolve().parent
    manifest=json.loads((package/'manifest.json').read_text())
    expected={'emulationstation','es-mister-helper','es-mister.sh','mister-launch.sh','README.txt'}
    assert set(manifest)==expected,'Unexpected payload files'
    for name,sha in manifest.items():
        assert digest(package/'payload'/name)==sha,'Payload hash mismatch: '+name
    if args.check:
        print('Payload verified:',json.dumps(manifest)); return
    assert os.geteuid()==0,'Run as root on the MiSTer'
    app=pathlib.Path('/media/fat/emulationstation').resolve(strict=True)
    assert str(app)=='/media/fat/emulationstation','Unexpected installation path'
    assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU','Return to the MiSTer menu first'
    install(package,manifest,app,expected)

def install(package,manifest,app,expected):
    lock=(app/'.codex-update.lock').open('a')
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    # The MiSTer's civil clock can be unsynchronised; use the package version and uptime.
    stamp='final-20261115-'+str(time.monotonic_ns())
    stage=app/('.codex-stage-'+stamp)
    backup=app/'backups'/('codex-1080p-'+stamp)
    stage.mkdir()
    # Prepare complete files on the same filesystem before replacing any installed file.
    for name in expected:
        with (package/'payload'/name).open('rb') as src,(stage/name).open('wb') as dst:
            shutil.copyfileobj(src,dst)
            dst.flush();os.fsync(dst.fileno())
        (stage/name).chmod(0o644 if name.endswith('.txt') else 0o755)
        assert digest(stage/name)==manifest[name]
    try:
        stop(app)
        backup.mkdir(parents=True)
        names=sorted(expected|{'es-mister.ini'})
        for name in names:
            assert (app/name).is_file(),'Missing installed file: '+name
            shutil.copy2(app/name,backup/name)
            assert digest(app/name)==digest(backup/name)
        ini=(app/'es-mister.ini').read_text()
        for key,value in dict(SIZE='1920x1080',FPS='60',STRIP='8',PLD='256',BENCH='0',PROFILE='0',SLOW_DUMPS='0').items():
            pattern=r'^'+key+r'=.*$'
            if re.search(pattern,ini,re.M): ini=re.sub(pattern,key+'='+value,ini,flags=re.M)
            else: ini+='\n'+key+'='+value+'\n'
        with (stage/'es-mister.ini').open('w') as output:
            output.write(ini);output.flush();os.fsync(output.fileno())
        replaced=[]
        try:
            for name in names:
                os.replace(stage/name,app/name);replaced.append(name)
            for name in expected: assert digest(app/name)==manifest[name]
            readiness=start_ready(app)
        except BaseException:
            stop(app)
            for name in reversed(replaced):
                shutil.copy2(backup/name,stage/name)
                os.replace(stage/name,app/name)
            start_ready(app,interactive=False)
            raise
        record=dict(backup=str(backup),original_sha256={name:digest(backup/name) for name in names},installed_sha256=manifest,settings=dict(SIZE='1920x1080',FPS=60,STRIP=8,PLD=256,BENCH=0,PROFILE=0,SLOW_DUMPS=0),**readiness)
        (backup/'installation.json').write_text(json.dumps(record,indent=2))
        print(json.dumps(record,indent=2),flush=True)
    finally:
        # Only the exclusive staging directory created by this invocation is eligible for removal.
        if stage.parent==app and stage.name=='.codex-stage-'+stamp: shutil.rmtree(stage)

if __name__=='__main__': main()
