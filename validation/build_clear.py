import argparse,os,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--arm',action='store_true');a=p.parse_args()
v=pathlib.Path(__file__).resolve().parent;root=v.parent/'es-mister';out=v/'clear-scan';out.mkdir(exist_ok=True)
sys.path.insert(0,str(root/'tests/audit'));from reference_source import read_reference
reference=out/'reference';reference.mkdir(exist_ok=True)
for n in ('SoftRaster.cpp','SoftRaster.h'):(reference/n).write_bytes(read_reference('es-core/src/renderers/soft/'+n))
env=dict(os.environ,TMPDIR=str(v/'tmp'),ES_SOFT_ALPHA_SCAN='1',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
compiler=['g++','-O2','-g','-std=c++17','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer'];suffix='native'
if a.arm:
    work=pathlib.Path('/mnt/es-mister-codex-arm');env.update(TMPDIR=str(work/'tmp'),ZIG_GLOBAL_CACHE_DIR=str(work/'zig-cache'))
    compiler=[str(work/'python/ziglang/zig'),'c++','-target','arm-linux-musleabihf','-mcpu=cortex_a9','-O3','-std=c++17','-static','-pthread'];suffix='arm'
outputs=[]
for mode,source in [('reference',reference),('candidate',root/'tests/audit/experiments/alpha-scan')]:
    binary=out/(mode+'-'+suffix)
    subprocess.run(compiler+(['-DAUDIT_CLEAR'] if mode=='candidate' else [])+['-I'+str(source),str(root/'tests/audit/clear_workload.cpp'),str(source/'SoftRaster.cpp'),'-o',str(binary)],env=env,check=True)
    if not a.arm:
        data=subprocess.check_output([str(binary)],env=env,text=True,timeout=60);(out/(mode+'-'+suffix+'.txt')).write_text(data);outputs.append(data)
if not a.arm:
    assert outputs[0]==outputs[1],'INDEPENDENT CLEAR METADATA DIFFER'
    print('Independent cba2b66 clear metadata match; host ASan/UBSan/LSan and protected boundaries passed')
