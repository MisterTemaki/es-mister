import os,pathlib,subprocess,sys
base=pathlib.Path(__file__).resolve().parent
temp=base/'tmp';temp.mkdir(exist_ok=True)
env=dict(os.environ,TMPDIR=str(temp))
targets=sys.argv[1:] or ['audit_renderer','audit_http_server']
subprocess.run(['cmake','-S',str(base.parent/'es-mister/tests/audit'),'-B',str(base/'regressions'),'-DCMAKE_PREFIX_PATH='+str(base/'tools/usr')],check=True,env=env)
subprocess.run(['cmake','--build',str(base/'regressions'),'-j4','--target',*targets],check=True,env=env)
