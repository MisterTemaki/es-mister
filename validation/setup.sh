#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
for audit_deb in debs/*.deb; do dpkg-deb -x "$audit_deb" tools; done
tools/usr/bin/cppcheck --version
tools/usr/bin/shellcheck --version
ln -sf /usr/lib/x86_64-linux-gnu/libpugixml.so.1.14 tools/usr/lib/x86_64-linux-gnu/libpugixml.so.1.14
export PATH="$PWD/tools/usr/bin:$PATH"
cmake -S ../es-mister -B native -G Ninja -DMISTER=ON -DCEC=OFF \
  -DUSE_SYSTEM_PUGIXML=ON -DCMAKE_PREFIX_PATH="$PWD/tools/usr" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_BUILD_TYPE=Debug >configure.log 2>&1
cmake --build native -j 6 --target emulationstation >build-native.log 2>&1
