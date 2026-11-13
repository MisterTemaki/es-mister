#!/usr/bin/env bash
# The MiSTer package: what goes on the SD card, as a ZIP to extract at its root.
#
#   emulationstation/          the program, its scripts, resources and translations, its default
#                              settings and controllers, and the Carbon theme (trimmed to its systems)
#   Scripts/emulationstation.sh  starts it from the MiSTer's Scripts menu
#
# Run by build.sh after the binary is built (build-mister/out/emulationstation).

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=${MISTER_WORK:-$ROOT/build-mister}
SRC=$WORK/src
TOOLS=$WORK/toolchain
STAGE=$WORK/package
VERSION=${VERSION:-$(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo dev)}

# Batocera's theme and controller database, at the versions batocera.linux pins
THEME_COMMIT=a897233e9bf6b9cca5c248ffceebf443882de926
BATOCERA_COMMIT=${BATOCERA_COMMIT:-master}

[ -f "$WORK/out/emulationstation" ] || { echo "build the binary first: mister/build.sh es" >&2; exit 1; }

if [ ! -d "$SRC/es-theme-carbon" ]; then
	git clone -q https://github.com/fabricecaruso/es-theme-carbon.git "$SRC/es-theme-carbon"
fi
git -C "$SRC/es-theme-carbon" checkout -q "$THEME_COMMIT"

if [ ! -d "$SRC/batocera.linux" ]; then
	git clone -q --depth 1 --filter=blob:none --sparse -b "$BATOCERA_COMMIT" https://github.com/batocera-linux/batocera.linux.git "$SRC/batocera.linux"
	git -C "$SRC/batocera.linux" sparse-checkout set package/batocera/emulationstation/batocera-emulationstation/controllers
fi

rm -rf "$STAGE"
ES=$STAGE/emulationstation
mkdir -p "$ES/defaults" "$ES/logs" "$ES/home/.emulationstation/themes" "$STAGE/Scripts"

echo "== program"
llvm-strip -o "$ES/emulationstation" "$WORK/out/emulationstation"
"$TOOLS/zcc" -O2 -s -static -o "$ES/es-mister-helper" "$ROOT/mister/es-mister-helper.c"
cp "$ROOT/mister/scripts/es-mister.sh" "$ROOT/mister/scripts/mister-launch.sh" "$ROOT/mister/systems.tsv" "$ES/"
cp "$ROOT/mister/scripts/emulationstation.sh" "$STAGE/Scripts/"
cat >"$ES/es-mister.ini" <<'EOF'
# EmulationStation on the MiSTer: settings read by es-mister.sh and mister-launch.sh

# The size ES draws at. The FPGA scales it to the screen by whole steps: 640x360 fills 1080p (x3)
# and 720p (x2). Larger is sharper and slower.
SIZE=640x360

# The most frames a second ES draws (fewer leaves the ARM more time)
FPS=60

# 1: every frame drawn whole even when nothing changed, to measure the drawing's cost
# (logs/es-mister-stats.txt); 0 in normal use
BENCH=0

# Where games folders are looked for, in order (each system takes the first that has its folder)
GAMES_ROOTS="/media/fat /media/usb0 /media/usb1 /media/usb2 /media/usb3 /media/usb4 /media/usb5 /media/fat/cifs"
EOF

echo "== resources and translations"
cp -r "$ROOT/resources" "$ES/resources"
mkdir -p "$ES/resources/locale"
(cd "$ROOT/locale" && find lang -name '*.po' | while read -r po; do mkdir -p "$ES/resources/locale/$(dirname "$po")"; cp "$po" "$ES/resources/locale/$po"; done)

echo "== settings and controllers"
cat >"$ES/defaults/es_settings.cfg" <<'EOF'
<?xml version="1.0"?>
<config>
	<string name="ThemeSet" value="es-theme-carbon" />
	<string name="Language" value="pt_BR" />
	<bool name="DrawFramerate" value="false" />
	<bool name="VSync" value="true" />
</config>
EOF
cp "$SRC/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/controllers/es_input.cfg" "$ES/defaults/es_input.cfg"

echo "== theme"
python3 -I "$ROOT/mister/tools/trim-theme.py" "$SRC/es-theme-carbon" "$ES/home/.emulationstation/themes/es-theme-carbon" "$ROOT/mister/systems.tsv"

cp "$ROOT/mister/LEIA-ME.txt" "$ES/"
cp "$ROOT/LICENSE.md" "$ES/LICENSE-EmulationStation.md"

# every script with Unix line ends
find "$STAGE" -name '*.sh' -exec sed -i 's/\r$//' {} \;

mkdir -p "$WORK/dist"
ZIP=$WORK/dist/es-mister-$VERSION.zip
rm -f "$ZIP"
(cd "$STAGE" && python3 -I -c '
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    for top in ("emulationstation", "Scripts"):
        for folder, dirs, files in os.walk(top):
            dirs.sort()
            for name in sorted(files):
                path = os.path.join(folder, name)
                info = zipfile.ZipInfo.from_file(path)
                info.compress_type = zipfile.ZIP_DEFLATED
                if name in ("emulationstation", "es-mister-helper") or name.endswith(".sh"):
                    info.external_attr = 0o100755 << 16
                with open(path, "rb") as f:
                    z.writestr(info, f.read())
' "$ZIP")
du -sh "$STAGE/emulationstation" "$ZIP"
