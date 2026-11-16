#!/usr/bin/env bash
set -euo pipefail
IMAGE=/mnt/c/Users/USER/.codex/cache/es-mister-arm/build-cache.img
TARGET=/mnt/es-mister-codex-arm
mkdir -p "$(dirname "$IMAGE")" "$TARGET"
if [ ! -f "$IMAGE" ]; then
    truncate -s 4G "$IMAGE"
    mkfs.ext4 -q -F "$IMAGE"
fi
if ! mountpoint -q "$TARGET"; then mount -o loop "$IMAGE" "$TARGET"; fi
chown USER:USER "$TARGET"
for part in python src host debs; do
    if [ ! -d "$TARGET/$part" ] && [ -d "/dev/shm/es-mister-codex-arm/$part" ]; then
        cp -a "/dev/shm/es-mister-codex-arm/$part" "$TARGET/"
    fi
done
if [ "$#" -gt 0 ]; then exec runuser -u USER -- "$@"; fi
