import pathlib,shutil,hashlib,json,zipfile
v=pathlib.Path(__file__).resolve().parent
root=v.parent/'es-mister'
package=v/'update-1080p'
payload=package/'payload';payload.mkdir(parents=True,exist_ok=True)
files={'emulationstation':v/'arm/emulationstation-performance-stripped',
       'es-mister-helper':v/'arm/es-mister-helper',
       'es-mister.sh':root/'mister/scripts/es-mister.sh',
       'README.txt':root/'mister/README.txt'}
manifest={}
for name,source in files.items():
    data=source.read_bytes().replace(b'\r\n',b'\n') if name.endswith(('.sh','.txt')) else source.read_bytes()
    (payload/name).write_bytes(data)
    manifest[name]=hashlib.sha256(data).hexdigest()
(package/'manifest.json').write_text(json.dumps(manifest,indent=2))
shutil.copy2(root/'mister/tools/install-update.py',package/'install-update.py')
(package/'INSTALL.txt').write_text('''MiSTer update at 1080p — 13/11/2026

The device 192.168.15.14 already received this update when stated in the deployment report.
To apply it again: extract this folder to /media/fat/codex-update-1080p, go back to the MiSTer's MENU and run:
  python3 /media/fat/codex-update-1080p/install-update.py --check
  python3 /media/fat/codex-update-1080p/install-update.py

Requires Python 3 on the MiSTer. Do not copy the payload over the executable in use.
The installer validates the hashes, closes the interface through the local API, prepares complete files,
backs up to /media/fat/emulationstation/backups/codex-1080p-DATE and restarts the launcher.
It keeps themes, games, controllers and metadata. It sets SIZE=auto, FPS=60, STRIP=8, PLD=256,
BENCH=0, PROFILE=0, SLOW_DUMPS=0. FPS is a limit, not guaranteed performance in full transitions.

Restoring: close the interface through the /quit API. Copy the five files from the backup given
in installation.json to /media/fat/emulationstation and start bash /media/fat/emulationstation/es-mister.sh.
A power failure during the update is not equivalent to the operation failures tested.
''',encoding='utf8')
dest=v.parent/'es-mister-1080p-update-20261113.zip'
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(package.rglob('*')):
        if path.is_file():z.write(path,'codex-update-1080p/'+path.relative_to(package).as_posix())
print(dest,hashlib.sha256(dest.read_bytes()).hexdigest())
print(json.dumps(manifest,indent=2))
