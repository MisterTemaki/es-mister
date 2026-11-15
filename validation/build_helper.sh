#!/usr/bin/env bash
set -euo pipefail
WORK=/mnt/es-mister-codex-arm
export ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache" TMPDIR="$WORK/tmp"
mkdir -p validation/arm
"$WORK/toolchain/zcc" -O2 -static -s es-mister/mister/es-mister-helper.c -o validation/arm/es-mister-helper
