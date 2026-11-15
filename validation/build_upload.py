import argparse,hashlib,json,os,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--arm',action='store_true');args=p.parse_args()
base=pathlib.Path(__file__).resolve().parent;root=base.parent/'es-mister';out=base/'upload';out.mkdir(exist_ok=True)
sys.path.insert(0,str(root/'tests/audit'))
from reference_source import read_reference
old=read_reference('es-core/src/renderers/Renderer_Soft.cpp').decode()
start=old.index('const uint8_t* line = src + (size_t)row * _width * 4;')
begin=old.index('for (unsigned int i = 0; i < count; i++)',start)
brace=old.index('{',begin);depth=1;end=brace+1
while depth:
    if old[end]=='{':depth+=1
    if old[end]=='}':depth-=1
    end+=1
loop=old[begin:end]
reference='#include <cstdint>\nusing std::uint32_t;using std::uint8_t;\nbool referenceUpload(uint32_t* dst,const uint8_t* line,unsigned int count,bool opaque)\n{\n'+loop+'\nreturn opaque;\n}\n'
(out/'reference.cpp').write_text(reference)
(out/'reference-source.json').write_text(json.dumps(dict(commit='cba2b668f2309d426ebce65f2c69e572088780d3',
    path='es-core/src/renderers/Renderer_Soft.cpp',original_loop_sha256=hashlib.sha256(loop.encode()).hexdigest()),indent=2))
env=dict(os.environ,TMPDIR=str(base/'tmp'))
compiler=['g++','-std=c++17','-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
name='upload-native'
if args.arm:
    env.update(TMPDIR='/mnt/es-mister-codex-arm/tmp',ZIG_GLOBAL_CACHE_DIR='/mnt/es-mister-codex-arm/zig-cache')
    compiler=['/mnt/es-mister-codex-arm/python/ziglang/zig','c++','-target','arm-linux-musleabihf','-mcpu=cortex_a9','-std=c++17','-O2','-static']
    name='upload-arm'
subprocess.run(compiler+['-I'+str(root/'es-core/src/renderers/soft'),str(root/'tests/audit/upload_workload.cpp'),str(out/'reference.cpp'),'-o',str(out/name)],env=env,check=True)
if not args.arm:
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(out/name)],env=env,check=True)
