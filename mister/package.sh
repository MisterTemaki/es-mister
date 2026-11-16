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
# ES_SIZE=1280x720 (say): ES drawn at that size (SIZE in es-mister.ini) and the theme's pictures made for it;
# unset, SIZE=auto (the MiSTer's own) and the pictures made for 1080p
ES_SIZE=${ES_SIZE:-}
THEME_SIZE=${ES_SIZE:-1920x1080}

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
"$TOOLS/zcc" -O3 -s -static -pthread -o "$ES/es-mister-bench" "$ROOT/mister/es-mister-bench.c"
"$TOOLS/zc++" -O2 -s -static -pthread -std=c++17 -I "$ROOT/es-core/src" -o "$ES/es-mister-replay" "$ROOT/mister/tests/replay.cpp" \
	"$ROOT/es-core/src/renderers/soft/SoftCapture.cpp" "$ROOT/es-core/src/renderers/soft/SoftRaster.cpp"
cp "$ROOT/mister/scripts/es-mister.sh" "$ROOT/mister/scripts/mister-launch.sh" "$ROOT/mister/systems.tsv" "$ES/"
cp "$ROOT/mister/scripts/emulationstation.sh" "$STAGE/Scripts/"
cat >"$ES/es-mister.ini" <<'EOF'
# EmulationStation on the MiSTer: settings read by es-mister.sh and mister-launch.sh

# The size ES draws at. auto: the size the MiSTer gives the Linux framebuffer for its menu, which
# follows the screen (video_mode in MiSTer.ini): the sharpest, and the slowest (a 1080p screen is nine
# times the pixels of 640x360). auto/2: half of it (960x540 on 1080p), auto/3: a third (640x360 on
# 1080p, the fastest). Or WIDTHxHEIGHT.
SIZE=__ES_SIZE__

# The most frames a second ES draws (fewer leaves the ARM more time)
FPS=60

# 1: every frame drawn whole even when nothing changed, to measure the drawing's cost
# (logs/es-mister-stats.txt); 0 in normal use
BENCH=0

# 1: where ES spends the processor, sampled and written to logs/es-profile.txt (for tuning the port,
# when asked for); 0 in normal use
PROFILE=0

# The first frames that take long to draw (up to this many each start) written with each draw's time to
# logs/es-slow-frame-N.txt, to see what to make faster; 0: none
SLOW_DUMPS=4

# Rows of the screen drawn at a time through all its layers, so they stay in the processor's cache
# between them (the memory is what a 1080p frame waits for). auto: by the screen's width, 24 rows at
# 1080p (the fastest measured on the DE10-Nano with real frames); or a number of rows, 0: each band whole
STRIP=auto

# Bytes of memory asked for ahead of the drawing and of the copy to the screen (256 by default, the
# DE10-Nano measured fastest from 64 to 1024); 0: not asked for, to compare
PLD=256

# 1: the slow frames dumped (SLOW_DUMPS) also captured whole, as logs/es-slow-frame-N.escf, to draw them
# again with es-mister-replay (a few MB each); 0 in normal use
CAPTURE=0

# Folders of _Arcade that ES does not read (lower case, space apart). _alternatives has thousands of
# other versions in hundreds of folders: reading it from the SD card adds many seconds to ES's start.
# Take it out of the list to see them in ES.
ARCADE_SKIP="cores _organized mame hbmame _alternatives"

# Input devices ES leaves out, by name (comma apart; /proc/bus/input/devices lists them). Main_MiSTer's
# and Zaparoo's virtual keyboards repeat a controller's presses as keys: without this, each press moved
# twice. Physical keyboards and controllers are not affected.
INPUT_IGNORE="MiSTer virtual input,Zaparoo"

# Where games folders are looked for, in order (each system takes the first that has its folder)
GAMES_ROOTS="/media/fat /media/usb0 /media/usb1 /media/usb2 /media/usb3 /media/usb4 /media/usb5 /media/fat/cifs"
EOF

sed -i "s/^SIZE=__ES_SIZE__$/SIZE=${ES_SIZE:-auto}/" "$ES/es-mister.ini"

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
python3 -I "$ROOT/mister/tools/trim-theme.py" "$SRC/es-theme-carbon" "$ES/home/.emulationstation/themes/es-theme-carbon" "$ROOT/mister/systems.tsv" "$THEME_SIZE"

cp "$ROOT/mister/LEIA-ME.txt" "$ES/"
cp "$ROOT/LICENSE.md" "$ES/LICENSE-EmulationStation.md"

# every script with Unix line ends
find "$STAGE" -name '*.sh' -exec sed -i 's/\r$//' {} \;

mkdir -p "$WORK/dist"
ZIP=$WORK/dist/es-mister-$VERSION${ES_SIZE:+-$ES_SIZE}.zip
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
                if name in ("emulationstation", "es-mister-helper", "es-mister-bench", "es-mister-replay") or name.endswith(".sh"):
                    info.external_attr = 0o100755 << 16
                with open(path, "rb") as f:
                    z.writestr(info, f.read())
' "$ZIP")
du -sh "$STAGE/emulationstation" "$ZIP"
