"""Independent old/new backend+rasters at the real upload API, native or ARM."""
import argparse,os,pathlib,shlex,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--arm',action='store_true');a=p.parse_args()
v=pathlib.Path(__file__).resolve().parent;root=v.parent/'es-mister';out=v/'renderer-upload-pair';out.mkdir(exist_ok=True)
sys.path.insert(0,str(root/'tests/audit'))
from reference_source import read_reference
reference=out/'reference';reference.mkdir(exist_ok=True)
for source in ('renderers/Renderer_Soft.cpp','renderers/soft/SoftRaster.cpp'):
    (reference/pathlib.Path(source).name).write_bytes(read_reference('es-core/src/'+source))
tests=(root/'tests/audit/renderer_frames.cpp').read_text()
for mode,backend in [('reference',reference/'Renderer_Soft.cpp'),('candidate',root/'es-core/src/renderers/Renderer_Soft.cpp')]:
    (out/(mode+'.cpp')).write_text(tests.replace('#include "../../es-core/src/renderers/Renderer_Soft.cpp"','#include "'+str(backend)+'"'))
core=root/'es-core/src';work=pathlib.Path('/mnt/es-mister-codex-arm')
env=dict(os.environ,TMPDIR=str(v/'tmp'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',ES_SOFT_UPLOAD_NEON='1',ES_SOFT_OPAQUE_FADE='1')
compiler=['g++','-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-std=c++17','-pthread']
suffix='native';includes=shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl2'],text=True));libs=shlex.split(subprocess.check_output(['pkg-config','--libs','sdl2'],text=True))
if a.arm:
    env.update(TMPDIR=str(work/'tmp'),ZIG_GLOBAL_CACHE_DIR=str(work/'zig-cache'))
    compiler=[str(work/'python/ziglang/zig'),'c++','-target','arm-linux-musleabihf','-mcpu=cortex_a9','-O2','-std=c++17','-static','-pthread']
    includes=['-I'+str(work/'prefix/include/SDL2')];libs=['-L'+str(work/'prefix/lib'),'-lSDL2','-lasound','-lm','-ldl'];suffix='arm'
outputs=[]
for mode,raster in [('reference',reference/'SoftRaster.cpp'),('candidate',core/'renderers/soft/SoftRaster.cpp')]:
    binary=out/(mode+'-'+suffix)
    sources=[out/(mode+'.cpp'),root/'tests/audit/support.cpp',raster,core/'renderers/soft/SoftOutput.cpp']+[core/('math/'+n+'.cpp') for n in ('Transform4x4f','Vector2f','Vector3f','Vector4f')]
    cmd=compiler+['-DUSE_SOFTWARE_RENDERER','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(core),'-I'+str(core/'renderers'),'-I'+str(core/'renderers/soft')]+includes+list(map(str,sources))+libs+['-o',str(binary)]
    subprocess.run(cmd,env=env,check=True)
    if not a.arm:
        data=subprocess.check_output([str(binary)],env=env,text=True,timeout=30);(out/(mode+'-'+suffix+'.txt')).write_text(data);outputs.append(data)
if not a.arm:
    assert outputs[0]==outputs[1],'Backend pixel hashes differ'
    print('Independent backend+raster hashes match; real upload API/partial clipping/opacity/lifecycle; native ASan/UBSan/LSan passed')
