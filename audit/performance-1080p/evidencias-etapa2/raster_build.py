import argparse, pathlib, shutil, subprocess, os
p=argparse.ArgumentParser();p.add_argument('tag');p.add_argument('--arm',action='store_true');args=p.parse_args()
work=pathlib.Path(__file__).resolve().parent
root=work.parent/'es-mister'
ref=work/'raster-reference';ref.mkdir(exist_ok=True)
if not (ref/'SoftRaster.cpp').exists():
    shutil.copy2(root/'audit/performance-1080p/referencia-instalada/es-core/src/renderers/soft/SoftRaster.cpp',ref)
    data=subprocess.check_output(['git','show','e469321:es-core/src/renderers/soft/SoftRaster.h'],cwd=root)
    (ref/'SoftRaster.h').write_bytes(data)
out=work/'raster';out.mkdir(exist_ok=True)
compiler=['g++','-O3','-std=c++17','-pthread']
env=dict(os.environ)
if args.arm:
    env.update(TMPDIR='/mnt/es-mister-codex-arm/tmp',ZIG_GLOBAL_CACHE_DIR='/mnt/es-mister-codex-arm/zig-cache')
    compiler=['/mnt/es-mister-codex-arm/python/ziglang/zig','c++','-target','arm-linux-musleabihf','-mcpu=cortex_a9','-O3','-std=c++17','-pthread','-static']
for name,source in [('reference',ref),(args.tag,root/'es-core/src/renderers/soft')]:
    dest=out/(name+('-arm' if args.arm else '-native'))
    subprocess.run([*compiler,'-I'+str(source),str(root/'tests/audit/raster_workload.cpp'),str(source/'SoftRaster.cpp'),'-o',str(dest)],check=True,env=env)
    if not args.arm:
        data=subprocess.check_output([str(dest)],text=True)
        (out/(name+'.txt')).write_text(data)
        print(name, data, flush=True)
if not args.arm:
    def hashes(name):return [line.split(' hash ')[1].split()[0] for line in (out/(name+'.txt')).read_text().splitlines()]
    assert hashes('reference')==hashes(args.tag),'PIXEL MISMATCH'
    print('16 differential scenes: identical pixel hashes',flush=True)
