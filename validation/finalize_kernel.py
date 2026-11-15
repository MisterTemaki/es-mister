"""Verify the normal launcher and original kernel state after kernel measurements."""
import json
from pathlib import Path
import subprocess
import time

base = Path(__file__).resolve().parent
ssh = '/mnt/c/Windows/System32/OpenSSH/ssh.exe'
scp = '/mnt/c/Windows/System32/OpenSSH/scp.exe'
key = r'C:\Users\USER\.ssh\id_ed25519_mister'
host = 'root@192.168.15.14'
remote_root = '/media/fat/emulationstation/codex-performance-20261113'
assert (base / 'target-results/kernel-nav-original-2/kernel-summary.json').is_file(), 'Finish navigation 2 first'
code = '''
import hashlib,json,pathlib,subprocess,time,urllib.request
root=pathlib.Path('/media/fat/emulationstation')
base=root/'codex-performance-20261113'
expected='d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64'
deadline=time.monotonic()+45
while True:
    pids=subprocess.run(['pidof','emulationstation'],stdout=subprocess.PIPE,text=True).stdout.split()
    env={}
    if len(pids)==1:
        proc=pathlib.Path('/proc')/pids[0]
        env=dict(x.split('=',1) for x in (proc/'environ').read_text().split(chr(0)) if '=' in x)
        normal=env.get('HOME')==str(root/'home') and 'ES_SOFT_TRACE' not in env
        ready=any(line.rstrip().endswith(' ready') for line in (root/'logs/es-startup.log').read_text().splitlines())
        if normal and ready:break
    if time.monotonic()>deadline:raise RuntimeError('Normal launcher not confirmed; inspect active process before continuing')
    time.sleep(.5)
binary_hash=hashlib.sha256((root/'emulationstation').read_bytes()).hexdigest()
assert binary_hash==expected
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
assert pathlib.Path('/sys/class/graphics/fb0/virtual_size').read_text().strip()=='1920,1080'
assert not list(pathlib.Path('/sys/devices/system/cpu/cpufreq').glob('policy*'))
assert 'socfpga_cpufreq ' not in pathlib.Path('/proc/modules').read_text()
assert 'Codex performance keyboard' not in pathlib.Path('/proc/bus/input/devices').read_text()
for name in ('ES_SOFT_TRACE','ES_PROFILE','ES_SOFT_BENCH','ES_SOFT_SLOW_DUMP','ES_SOFT_DIRTY_CHECK'):
    assert name not in env,name
for name,value in (('ES_SOFT_FPS','60'),('ES_SOFT_STRIP','8'),('ES_SOFT_PLD','256')):
    assert env.get(name)==value,(name,env.get(name))
original=json.loads((base/'kernel-inventory-original.json').read_text())
assert pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()==original['boot_id']
for name,value in original['vm_read_only'].items():
    assert pathlib.Path('/proc/sys/vm',name).read_text().strip()==value
state=dict(frontend_pid=int(pids[0]),normal_home=env['HOME'],binary_sha256=binary_hash,
    helper_sha256=hashlib.sha256((root/'es-mister-helper').read_bytes()).hexdigest(),
    framebuffer_size='1920,1080',framebuffer_bpp=pathlib.Path('/sys/class/graphics/fb0/bits_per_pixel').read_text().strip(),
    core='MENU',startup_ready=True,cpufreq_policies=[],socfpga_cpufreq_loaded=False,test_keyboard_present=False,
    renderer_environment={k:v for k,v in env.items() if k.startswith(('ES_SOFT_','ES_TEXTURE_','ES_SYSTEM_','SDL_EVDEV_'))},
    config_sha256={n:hashlib.sha256((root/'home/.emulationstation'/n).read_bytes()).hexdigest()
                   for n in ('es_settings.cfg','es_input.cfg','es_systems.cfg')},
    vm_read_only=original['vm_read_only'],kernel_parameters_changed=False,
    same_boot_id=True,systems_api=urllib.request.urlopen('http://127.0.0.1:1234/systems',timeout=5).status)
(base/'kernel-restoration.json').write_text(json.dumps(state,indent=2))
subprocess.run(['python3',str(base/'bin/kernel_probe'),'inventory','--output',str(base/'kernel-inventory-final.json'),
                '--binary',str(root/'emulationstation')],check=True)
print(json.dumps(state,indent=2))
'''
subprocess.run([ssh, '-T', '-i', key, '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', host, 'python3 -'],
               input=code, text=True, check=True)
dest = subprocess.check_output(['wslpath', '-w', str(base / 'target-results')], text=True).strip()
for name in ('kernel-restoration.json', 'kernel-inventory-final.json'):
    subprocess.run([scp, '-i', key, '-o', 'BatchMode=yes', host + ':' + remote_root + '/' + name, dest], check=True)
