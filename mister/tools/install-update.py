#!/usr/bin/env python3
"""Install the reviewed 1080p update on MiSTer, preserving data and a rollback copy."""
import argparse, datetime, hashlib, json, os, pathlib, re, shutil, subprocess, time, urllib.request

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

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--check',action='store_true',help='Verify the payload without installing')
    args=parser.parse_args()
    package=pathlib.Path(__file__).resolve().parent
    manifest=json.loads((package/'manifest.json').read_text())
    expected={'emulationstation','es-mister-helper','es-mister.sh','LEIA-ME.txt'}
    assert set(manifest)==expected,'Unexpected payload files'
    for name,sha in manifest.items():
        assert digest(package/'payload'/name)==sha,'Payload hash mismatch: '+name
    if args.check:
        print('Payload verified:',json.dumps(manifest)); return
    assert os.geteuid()==0,'Run as root on the MiSTer'
    app=pathlib.Path('/media/fat/emulationstation').resolve(strict=True)
    assert str(app)=='/media/fat/emulationstation','Unexpected installation path'
    assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU','Return to the MiSTer menu first'
    stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
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
        try: urllib.request.urlopen('http://127.0.0.1:1234/quit',timeout=3).read()
        except Exception: pass
        for _ in range(300):
            if not running(app): break
            time.sleep(.1)
        assert not running(app),'Frontend still active; no installed files were replaced'
        backup.mkdir(parents=True)
        names=sorted(expected|{'es-mister.ini'})
        for name in names:
            assert (app/name).is_file(),'Missing installed file: '+name
            shutil.copy2(app/name,backup/name)
            assert digest(app/name)==digest(backup/name)
        ini=(app/'es-mister.ini').read_text()
        for key,value in dict(SIZE='auto',FPS='60',STRIP='8',PLD='256',BENCH='0',PROFILE='0',SLOW_DUMPS='0').items():
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
        except BaseException:
            for name in reversed(replaced):
                shutil.copy2(backup/name,stage/name)
                os.replace(stage/name,app/name)
            raise
        record=dict(backup=str(backup),installed_sha256=manifest,settings=dict(SIZE='auto',FPS=60,STRIP=8,PLD=256,BENCH=0,PROFILE=0,SLOW_DUMPS=0))
        (backup/'installation.json').write_text(json.dumps(record,indent=2))
        with (app/'logs/codex-update.log').open('a') as log:
            process=subprocess.Popen(['bash',str(app/'es-mister.sh')],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        record['launcher_pid']=process.pid
        print(json.dumps(record,indent=2),flush=True)
    finally:
        # Only the exclusive staging directory created by this invocation is eligible for removal.
        if stage.parent==app and stage.name=='.codex-stage-'+stamp: shutil.rmtree(stage)

if __name__=='__main__': main()
