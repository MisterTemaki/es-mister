import argparse,os,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--arm',action='store_true');a=p.parse_args()
v=pathlib.Path(__file__).resolve().parent;root=v.parent/'es-mister';out=v/'output-pair';out.mkdir(exist_ok=True)
sys.path.insert(0,str(root/'tests/audit'));from reference_source import read_reference
reference=out/'SoftOutput-reference.cpp';reference.write_bytes(read_reference('es-core/src/renderers/soft/SoftOutput.cpp'))
test=(root/'tests/audit/output_workload.cpp').read_text();core=root/'es-core/src/renderers/soft'
env=dict(os.environ,TMPDIR=str(v/'tmp'),ES_SOFT_PLD='256',ES_SOFT_COPY_PLD_PAIR='1',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
compiler=['g++','-O2','-g','-std=c++17','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer'];suffix='native'
if a.arm:
    work=pathlib.Path('/mnt/es-mister-codex-arm');env.update(TMPDIR=str(work/'tmp'),ZIG_GLOBAL_CACHE_DIR=str(work/'zig-cache'))
    compiler=[str(work/'python/ziglang/zig'),'c++','-target','arm-linux-musleabihf','-mcpu=cortex_a9','-O3','-std=c++17','-static','-pthread'];suffix='arm'
outputs=[]
for mode,source in [('reference',reference),('candidate',core/'SoftOutput.cpp')]:
    wrapper=out/(mode+'.cpp');wrapper.write_text(test.replace('#include "../../es-core/src/renderers/soft/SoftOutput.cpp"','#include "'+str(source)+'"'))
    binary=out/(mode+'-'+suffix)
    subprocess.run(compiler+['-I'+str(core),str(wrapper),'-o',str(binary)],env=env,check=True)
    if not a.arm:
        data=subprocess.check_output([str(binary)],env=env,text=True,timeout=30);(out/(mode+'-'+suffix+'.txt')).write_text(data);outputs.append(data)
if not a.arm:
    assert outputs[0]==outputs[1];print('Reference and candidate copy contract match; host ASan/UBSan/LSan passed')
