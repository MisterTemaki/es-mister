import base64,subprocess
script='''import os,time,pathlib
assert pathlib.Path('/tmp/CORENAME').read_text().strip()=='MENU'
p=pathlib.Path('/sys/class/graphics/fb0/virtual_size')
print('before',p.read_text().strip())
try:
 f=os.open('/dev/MiSTer_cmd',os.O_WRONLY|os.O_NONBLOCK)
 os.write(f,b'fb_cmd1 8888 1 1919 1080\\n');os.close(f)
 time.sleep(.3)
 print('probe',p.read_text().strip())
finally:
 f=os.open('/dev/MiSTer_cmd',os.O_WRONLY|os.O_NONBLOCK)
 os.write(f,b'fb_cmd1 8888 1 1920 1080\\n');os.close(f)
 time.sleep(.3)
 print('restored',p.read_text().strip())
'''
payload=base64.b64encode(script.encode()).decode()
r=subprocess.run(['/mnt/c/Windows/System32/OpenSSH/ssh.exe','-i',r'C:\Users\USER\.ssh\id_ed25519_mister',
 '-o','BatchMode=yes','root@192.168.15.14',f'python3 -c "import base64;exec(base64.b64decode(\'{payload}\'))"'])
raise SystemExit(r.returncode)
