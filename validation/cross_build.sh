#!/usr/bin/env bash
set -euo pipefail
AUDIT=$(cd "$(dirname "$0")/.." && pwd)
WORK=/mnt/es-mister-codex-arm
TAG=${1:-profile}
VARIANT=${2:-active}
# Staging and object output share this task's cache. Hold the lock across both.
exec 9>"$WORK/codex-build.lock"
flock 9
export TMPDIR="$WORK/tmp" ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache" PYTHONPATH="$WORK/python"
export MISTER_WORK="$WORK" JOBS=6
python3 - "$AUDIT/es-mister" "$WORK/source" "$VARIANT" <<'PY'
import pathlib,shutil,sys,subprocess,json,hashlib
src,dest=map(pathlib.Path,sys.argv[1:3])
sys.path.insert(0,str(src/'tests/audit'))
from reference_source import read_reference
overrides={}
if sys.argv[3] in ('diagnostic-reference','startup-only','strict-reference'):
    for name in ('es-app/src/SystemData.cpp','es-core/src/resources/TextureDataManager.cpp',
                 'es-core/src/resources/TextureDataManager.h','es-core/src/resources/TextureData.cpp','es-core/src/resources/TextureData.h'):
        if sys.argv[3]=='startup-only' and name=='es-app/src/SystemData.cpp':continue
        overrides[name]=read_reference(name)
    if sys.argv[3]=='strict-reference':
        for name in ('es-core/src/renderers/soft/SoftRaster.cpp','es-core/src/renderers/soft/SoftRaster.h','es-core/src/renderers/soft/SoftOutput.cpp'):
            data=read_reference(name)
            if name.endswith('.h'):
                # Same diagnostic record layout as the candidate; no raster implementation change.
                marker=b'struct CacheTrace\n\t\t{\n'
                assert data.count(marker)==1
                data=data.replace(marker,marker+b'\t\t\tuint64_t opaqueFadePixels = 0;\n')
            overrides[name]=data
elif sys.argv[3]=='renderer-only':
    name='es-core/src/resources/TextureDataManager.cpp'
    overrides[name]=read_reference(name)
elif sys.argv[3] not in ('active','alpha-verified'):raise ValueError('Unknown build variant')

if sys.argv[3]=='alpha-verified':
    # Quality-only build: compare the actual app's metadata with the independent
    # pre-change algorithm. Borrow pixels instead of copying a full surface.
    name='es-core/src/renderers/soft/SoftRaster.cpp'
    data=(src/'tests/audit/experiments/alpha-scan/SoftRaster.cpp').read_text()
    for header in ('SoftRaster.h','SoftAlphaScan.h'):
        overrides['es-core/src/renderers/soft/'+header]=(src/'tests/audit/experiments/alpha-scan'/header).read_bytes()
    old=read_reference(name).decode()
    marker='void findClearRuns(Texture& texture)'
    def function_end(code,start):
        opening=code.index('{',start);depth=1;end=opening+1
        while depth:
            depth+=(code[end]=='{')-(code[end]=='}');end+=1
        return opening,end
    begin=data.index(marker);opening,end=function_end(data,begin)
    old_begin=old.index(marker);old_open,old_end=function_end(old,old_begin)
    body=old[old_open:old_end]
    wrapper='''
        struct AlphaVerificationCount {
            unsigned long calls = 0;
            ~AlphaVerificationCount() { std::fprintf(stderr, "ALPHA_METADATA_VERIFIED %lu\\n", calls); }
        };
        void findClearRuns(Texture& texture)
        {
            findClearRunsUnchecked(texture);
            struct BorrowedTexture {
                int width, height; bool opaque;
                const std::vector<uint32_t>& pixels;
                std::vector<uint32_t> clearAt, clear;
            } expected{texture.width, texture.height, texture.opaque, texture.pixels, {}, {}};
            auto reference = [](BorrowedTexture& texture) OLD_BODY;
            reference(expected);
            if (texture.clearAt != expected.clearAt || texture.clear != expected.clear) {
                std::fprintf(stderr, "ALPHA_METADATA_MISMATCH %d %d\\n",texture.width,texture.height);
                std::abort();
            }
            static AlphaVerificationCount verified;
            ++verified.calls; // The software backend uploads on its UI thread.
        }
'''.replace('OLD_BODY',body)
    data=data[:begin]+data[begin:end].replace(marker,'void findClearRunsUnchecked(Texture& texture)',1)+wrapper+data[end:]
    overrides[name]=('#include <cstdio>\n'+data).encode()
    name='es-app/src/main.cpp'
    data=(src/name).read_text()
    marker='const int64_t updateStart = tracePhases ? StartupTrace::frameClock() : 0;'
    assert data.count(marker)==1
    # Test-only fixed logical steps: PNG compression and asset loading must not
    # determine the animation instant used by differential image comparisons.
    overrides[name]=data.replace(marker,'if (getenv("ES_AUDIT_FIXED_DT")) deltaTime = 100;\n\t\t'+marker).encode()
def copy_final(source,target):
    origin=pathlib.Path(source);target=pathlib.Path(target)
    relative=origin.relative_to(src).as_posix()
    data=overrides[relative] if relative in overrides else origin.read_bytes()
    # Variant switches must invalidate Ninja by CONTENT, even if the original
    # source's mtime is older than a previously compiled reference override.
    if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
    shutil.copymode(origin,target)
    return str(target)
shutil.copytree(src,dest,dirs_exist_ok=True,copy_function=copy_final,ignore=shutil.ignore_patterns('.git','audit','tests','validation'))
for relative,data in overrides.items():
    if not (src/relative).exists():
        target=dest/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
removed='es-core/src/renderers/soft/SoftAlphaScan.h'
if removed not in overrides and not (src/removed).exists():(dest/removed).unlink(missing_ok=True)
manifest={str(p.relative_to(dest)):hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.rglob('*') if p.is_file()}
(dest.parent/('staged-source-'+sys.argv[3]+'.json')).write_text(json.dumps(manifest,indent=2))
PY
bash "$WORK/source/mister/build.sh" es
mkdir -p "$AUDIT/validation/arm"
cp "$WORK/out/emulationstation" "$AUDIT/validation/arm/emulationstation-$TAG"
cp "$WORK/staged-source-$VARIANT.json" "$AUDIT/validation/arm/emulationstation-$TAG.source.json"
sha256sum "$AUDIT/validation/arm/emulationstation-$TAG"
