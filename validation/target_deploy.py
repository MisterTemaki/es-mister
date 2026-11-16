import pathlib,base64,subprocess,hashlib,json
v=pathlib.Path(__file__).resolve().parent
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe';scp='/mnt/c/Windows/System32/OpenSSH/scp.exe'
key=r'C:\Users\USER\.ssh\id_ed25519_mister';host='root@192.168.15.14'
root='/media/fat/emulationstation/codex-performance-20261113'
def remote(code):
    data=base64.b64encode(code.encode()).decode()
    p=subprocess.run([ssh,'-i',key,'-o','BatchMode=yes','-o','ConnectTimeout=8',host,f'python3 -c "import base64;exec(base64.b64decode(\'{data}\'))"'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    print(p.stdout,flush=True)
    if p.returncode:raise SystemExit(p.returncode)
    return p.stdout
archive=v.parent/'es-mister-1080p-update-20261113.zip'
win=subprocess.check_output(['wslpath','-w',str(archive)],text=True).strip()
subprocess.run([scp,'-i',key,'-o','BatchMode=yes',win,host+':'+root+'/update-1080p.zip'],check=True)
sha=hashlib.sha256(archive.read_bytes()).hexdigest()
remote(f'''import pathlib,zipfile,hashlib,subprocess
root=pathlib.Path({root!r});archive=root/'update-1080p.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=={sha!r}
directory=root/'update-final';directory.mkdir(exist_ok=True)
with zipfile.ZipFile(archive) as z:
 for info in z.infolist():
  p=pathlib.PurePosixPath(info.filename)
  assert not p.is_absolute() and '..' not in p.parts and p.parts[0]=='codex-update-1080p'
 z.extractall(directory)
installer=directory/'codex-update-1080p/install-update.py'
subprocess.run(['python3',str(installer),'--check'],check=True)
subprocess.run(['python3',str(installer)],check=True)
''')
