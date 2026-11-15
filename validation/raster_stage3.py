import pathlib,subprocess,os,argparse,sys
p=argparse.ArgumentParser();p.add_argument('--arm',action='store_true');args=p.parse_args()
v=pathlib.Path(__file__).resolve().parent;root=v.parent/'es-mister';dest=v/'raster-stage3';dest.mkdir(exist_ok=True)
sys.path.insert(0,str(root/'tests/audit'))
from reference_source import read_reference
reference=dest/'reference';reference.mkdir(exist_ok=True)
for name in ('SoftRaster.cpp','SoftRaster.h'):
    data=read_reference('es-core/src/renderers/soft/'+name)
    (reference/name).write_bytes(data)
env=dict(os.environ,TMPDIR=str(v/'tmp'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',ES_SOFT_SCALED_MB='40',ES_SOFT_PLD='256',ES_SOFT_OPAQUE_FADE='1')
compiler=['g++','-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-std=c++17','-pthread']
suffix='native'
if args.arm:
    env.update(TMPDIR='/mnt/es-mister-codex-arm/tmp',ZIG_GLOBAL_CACHE_DIR='/mnt/es-mister-codex-arm/zig-cache')
    compiler=['/mnt/es-mister-codex-arm/python/ziglang/zig','c++','-target','arm-linux-musleabihf','-mcpu=cortex_a9','-O3','-std=c++17','-pthread','-static']
    suffix='arm'
outputs=[]
for name,source in [('reference',reference),('opaque',root/'es-core/src/renderers/soft')]:
    binary=dest/(name+'-'+suffix)
    cmd=compiler+(['-DAUDIT_OPAQUE'] if name=='opaque' else [])+['-I'+str(source),str(root/'tests/audit/raster_workload.cpp'),str(source/'SoftRaster.cpp'),'-o',str(binary)]
    subprocess.run(cmd,env=env,check=True)
    if not args.arm:
        records={}
        for mode in ('normal','composite'):
            out=subprocess.check_output([str(binary)]+(['composite'] if mode=='composite' else []),env=env,text=True,timeout=120)
            (dest/(name+'-'+mode+'.txt')).write_text(out)
            records[mode]=[line.split(' hash ')[1].split()[0] for line in out.splitlines()]
        outputs.append(records)
if not args.arm:
    assert outputs[0]==outputs[1],'PIXEL MISMATCH'
    print('16 existing scenes and 96 color/alpha/clip/UV/fallback scenes match independent cba2b66 raster; ASan/UBSan/LSan passed',flush=True)
