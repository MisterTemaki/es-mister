#!/usr/bin/env bash
set -euo pipefail
AUDIT=$(cd "$(dirname "$0")/.." && pwd)
WORK=/mnt/es-mister-codex-arm
TAG=${1:-profile}
export TMPDIR="$WORK/tmp" ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache" PYTHONPATH="$WORK/python"
export MISTER_WORK="$WORK" JOBS=6
python3 - "$AUDIT/es-mister" "$WORK/source" <<'PY'
import pathlib,shutil,sys
src,dest=map(pathlib.Path,sys.argv[1:])
shutil.copytree(src,dest,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.git','audit','tests'))
PY
bash "$WORK/source/mister/build.sh" es
mkdir -p "$AUDIT/validation/arm"
cp "$WORK/out/emulationstation" "$AUDIT/validation/arm/emulationstation-$TAG"
sha256sum "$AUDIT/validation/arm/emulationstation-$TAG"
