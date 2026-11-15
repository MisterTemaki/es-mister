import base64, pathlib, subprocess
ssh='/mnt/c/Windows/System32/OpenSSH/ssh.exe'
script='''import xml.etree.ElementTree as E
import pathlib, json
p=pathlib.Path('/media/fat/emulationstation/home/.emulationstation')
r=E.parse(p/'es_input.cfg').getroot()
print('keyboard',[(n.attrib,[x.attrib for x in n]) for n in r if n.get('type')=='keyboard'])
r=E.parse(p/'es_settings.cfg').getroot()
print('settings',[n.attrib for n in r if n.get('name') in ('ThemeSet','StartupSystem','TransitionStyle','ThreadedLoading','PowerSaverMode')])
'''
payload=base64.b64encode(script.encode()).decode()
subprocess.run([ssh,'-i',r'C:\Users\USER\.ssh\id_ed25519_mister','-o','BatchMode=yes','-o','ConnectTimeout=8',
                'root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{payload}\'))"'],check=True)
