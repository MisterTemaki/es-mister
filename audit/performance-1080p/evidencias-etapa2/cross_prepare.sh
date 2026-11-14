#!/usr/bin/env bash
set -euo pipefail
AUDIT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$AUDIT/es-mister"
WORK=/mnt/es-mister-codex-arm
mkdir -p "$WORK"
mkdir -p "$WORK/tmp" "$WORK/zig-cache"
export TMPDIR="$WORK/tmp" ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache"
export PYTHONPATH="$WORK/python"
python3 - "$WORK/python" <<'PY'
import pathlib,sys,urllib.request,json,zipfile,io,platform
target=pathlib.Path(sys.argv[1])
if not (target/'ziglang/zig').exists():
    metadata=json.load(urllib.request.urlopen('https://pypi.org/pypi/ziglang/0.15.2/json'))
    wheel=next(x for x in metadata['urls'] if 'manylinux' in x['filename'] and 'x86_64' in x['filename'])
    data=urllib.request.urlopen(wheel['url']).read()
    import hashlib
    assert hashlib.sha256(data).hexdigest()==wheel['digests']['sha256']
    target.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z: z.extractall(target)
    (target/'ziglang/zig').chmod(0o755)
print('Zig compiler ready',flush=True)
PY
HOST="$WORK/host"
if [ ! -x "$HOST/usr/bin/autoreconf" ]; then
    mkdir -p "$WORK/debs" "$HOST"
    (cd "$WORK/debs" && apt-get download autoconf automake libtool)
    for deb in "$WORK"/debs/*.deb; do dpkg-deb -x "$deb" "$HOST"; done
    python3 - "$HOST" <<'PY'
import pathlib,sys
root=pathlib.Path(sys.argv[1])
for top in (root/'usr/bin',root/'usr/share/autoconf',root/'usr/share/automake-1.18'):
    if not top.exists(): continue
    for p in top.rglob('*'):
        if p.is_file() and not p.is_symlink():
            data=p.read_bytes()
            if b'\0' not in data:
                p.write_bytes(data.replace(b'/usr/share/',(str(root)+'/usr/share/').encode()))
PY
fi
ln -sf aclocal-1.18 "$HOST/usr/bin/aclocal"
ln -sf automake-1.18 "$HOST/usr/bin/automake"
export PATH="$HOST/usr/bin:$AUDIT/validation/tools/usr/bin:$PATH"
export ACLOCAL_PATH="$HOST/usr/share/aclocal"
export AUTOM4TE="$HOST/usr/bin/autom4te" AUTOCONF="$HOST/usr/bin/autoconf"
export AUTOHEADER="$HOST/usr/bin/autoheader" ACLOCAL="$HOST/usr/bin/aclocal"
export AUTOMAKE="$HOST/usr/bin/automake" LIBTOOLIZE="$HOST/usr/bin/libtoolize"
if [ ! -f "$SRC/external/pugixml/src/pugixml.cpp" ]; then
    git clone --no-checkout https://github.com/zeux/pugixml.git "$SRC/external/pugixml"
    git -C "$SRC/external/pugixml" checkout --detach 61c944895e071872370219cadade8423dc6e04dc
fi
export MISTER_WORK="$WORK" JOBS=6
python3 "$AUDIT/validation/cross_sources.py"
if [ ! -f "$WORK/src/alsa-lib/Makefile.in" ]; then
    cp "$WORK/src/SDL2/build-scripts/config.guess" "$WORK/src/SDL2/build-scripts/config.sub" "$WORK/src/alsa-lib/"
    (cd "$WORK/src/alsa-lib" && autoreconf -fi)
fi
bash -x "$SRC/mister/build.sh" deps
