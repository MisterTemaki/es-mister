import pathlib, subprocess
base=pathlib.Path(__file__).resolve().parent
subprocess.run(['cmake','-S',str(base.parent/'es-mister/tests/audit'),'-B',str(base/'regressions'),
                '-DCMAKE_PREFIX_PATH='+str(base/'tools/usr')],check=True)
subprocess.run(['cmake','--build',str(base/'regressions'),'-j4','--target','audit_settings'],check=True)
