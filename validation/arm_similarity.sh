#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/es-mister-codex-arm
export TMPDIR="$WORK/tmp" ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache"
"$WORK/python/ziglang/zig" c++ -target arm-linux-musleabihf -mcpu=cortex_a9 -O3 -std=c++17 -static -I es-mister/es-core/src es-mister/tests/audit/similarity.cpp -o validation/arm/audit-similarity
