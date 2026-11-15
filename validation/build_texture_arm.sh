#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/es-mister-codex-arm
export TMPDIR="$WORK/tmp" ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache"
PYTHONPATH=es-mister/tests/audit python3 -c "from reference_source import read_reference;import sys;sys.stdout.buffer.write(read_reference('es-core/src/resources/TextureDataManager.cpp'))" > validation/texture-manager-reference.cpp
for mode in reference candidate; do
  source=validation/texture-manager-reference.cpp
  if [ "$mode" = candidate ]; then source=es-mister/es-core/src/resources/TextureDataManager.cpp; fi
  "$WORK/python/ziglang/zig" c++ -target arm-linux-musleabihf -mcpu=cortex_a9 -O2 -std=c++17 -static -pthread -DMISTER \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I es-mister/es-core/src -I "$WORK/prefix/include/SDL2" \
    es-mister/tests/audit/texture_manager.cpp es-mister/tests/audit/support.cpp "$source" -o "validation/arm/audit-texture-$mode"
done
