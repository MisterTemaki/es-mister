"""Compile the shared texture accounting's GLES3 branch using the ARM headers."""
import os,pathlib,shlex,subprocess
work=pathlib.Path('/mnt/es-mister-codex-arm')
build=work/'obj/es'
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands'],text=True)
line=next(x for x in commands.splitlines() if '/resources/TextureData.cpp' in x and ' -c ' in x)
args=shlex.split(line);out=[];skip=False
for i,arg in enumerate(args):
    if skip:skip=False;continue
    if arg in ('-o','-MF','-MT'):skip=True;continue
    if arg in ('-c','-MD'):continue
    out.append(arg)
output=pathlib.Path(__file__).resolve().parent/'tmp/texture-gles3.o'
out+=['-DUSE_OPENGLES_30','-c','-o',str(output)]
env=dict(os.environ,TMPDIR=str(work/'tmp'),ZIG_GLOBAL_CACHE_DIR=str(work/'zig-cache'))
subprocess.run(out,cwd=build,env=env,check=True)
print('ARM syntax check: TextureData USE_OPENGLES_30 branch passed (not a GLES runtime test)')
