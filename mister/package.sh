#!/usr/bin/env bash
# The MiSTer package: what goes on the SD card, as a ZIP to extract at its root.
#
#   NAME/              the program, its scripts, resources and translations, its default settings
#                      and controllers, and the Carbon theme (trimmed to its systems)
#   Scripts/NAME.sh    starts it from the MiSTer's Scripts menu
#
# NAME is ES_NAME (emulationstation unless set). Run by build.sh after the binary is built
# (build-mister/out/emulationstation).

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
# ES_NAME=es-mister-720p (say): the folder on the SD card (/media/fat/NAME) and the script (Scripts/NAME.sh)
ES_NAME=${ES_NAME:-emulationstation}
# ES_TITLE: the package's name in README.txt
ES_TITLE=${ES_TITLE:-ES-MiSTer}
# ES_VERSION: the release's number ("0.10 beta 1"), in README.txt and in ES (its menu's footer and System
# information: version.info and about.info beside ES); none: ES shows its own (Batocera's) number
ES_VERSION=${ES_VERSION:-}
# ES_LANGUAGE: the language ES starts in, until changed in its menu (System settings > Language)
ES_LANGUAGE=${ES_LANGUAGE:-en_US}
case "$ES_NAME" in
	*[!A-Za-z0-9._-]*|"") echo "ES_NAME: letters, digits, '.', '_' and '-' only" >&2; exit 1 ;;
esac

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
ES=$STAGE/$ES_NAME
mkdir -p "$ES/defaults" "$ES/logs" "$ES/home/.emulationstation/themes" "$STAGE/Scripts"

echo "== program"
llvm-strip -o "$ES/emulationstation" "$WORK/out/emulationstation"
"$TOOLS/zcc" -O2 -s -static -o "$ES/es-mister-helper" "$ROOT/mister/es-mister-helper.c"
"$TOOLS/zcc" -O3 -s -static -pthread -o "$ES/es-mister-bench" "$ROOT/mister/es-mister-bench.c"
"$TOOLS/zc++" -O2 -s -static -pthread -std=c++17 -I "$ROOT/es-core/src" -o "$ES/es-mister-replay" "$ROOT/mister/tests/replay.cpp" \
	"$ROOT/es-core/src/renderers/soft/SoftCapture.cpp" "$ROOT/es-core/src/renderers/soft/SoftRaster.cpp"
cp "$ROOT/mister/scripts/es-mister.sh" "$ROOT/mister/scripts/mister-launch.sh" "$ROOT/mister/systems.tsv" "$ES/"
sed -e "s|^ES=/media/fat/emulationstation$|ES=/media/fat/$ES_NAME|" "$ROOT/mister/scripts/emulationstation.sh" >"$STAGE/Scripts/$ES_NAME.sh"
grep -q "^ES=/media/fat/$ES_NAME$" "$STAGE/Scripts/$ES_NAME.sh" || { echo "the Scripts launcher has no ES= line to set" >&2; exit 1; }
cat >"$ES/es-mister.ini" <<'EOF'
# EmulationStation on the MiSTer: settings read by es-mister.sh and mister-launch.sh

# The size ES draws at. auto: the size the MiSTer gives the Linux framebuffer for its menu, which
# follows the screen (video_mode in MiSTer.ini): the sharpest, and the slowest (a 1080p screen is nine
# times the pixels of 640x360). auto/2: half of it (960x540 on 1080p), auto/3: a third (640x360 on
# 1080p, the fastest). Or WIDTHxHEIGHT.
SIZE=__ES_SIZE__

# 1: the picture fills the screen with SIZE as WIDTHxHEIGHT: the framebuffer at the screen's size, and ES's
# frames, drawn at SIZE, enlarged to it as they are copied (1280x720 on a 1080p screen: 1.5 times,
# smoothed; the drawing costs the same, the copy a little more). Unless the MiSTer fills the screen with
# SIZE itself (960x540 on 1080p: twice). 0: the framebuffer at SIZE, which the MiSTer enlarges only by
# whole times (1280x720 on 1080p stays in the middle, at two thirds of the screen).
FILL=1

# 1: on a controller, the button that opens the MiSTer menu (the DualSense's PS, as the MiSTer's own button
# settings have it) asks first while EmulationStation is shown, and on yes EmulationStation closes and the
# MiSTer menu comes back; 0: it opens the MiSTer menu over EmulationStation at once, as F12 does
MENU_ASK=1

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

# 1: pictures kept decoded at the size shown, without loss, in cache/pictures (~70 KB a cover, written the
# first time a picture is shown, never while less than 1 GB is free on the SD card): a cover seen before
# comes back ~20 times faster; 0: none (the folder can be deleted at any time)
PICTURE_CACHE=1

# 1: each system's theme kept as the theme built it, in cache/themes (~130 KB a system, written after a start
# that had to read the theme): a start reads it back instead of reading the theme again for every system, while
# the theme's files, its settings and the art it found are the same; 0: none (the folder can be deleted at any
# time)
THEME_CACHE=1

# 1: what EmulationStation reads first (games folders, gamelists, theme, itself) read in the background while
# its start waits for the MiSTer menu, so that a start after switching the MiSTer on finds them in memory;
# only read, nothing written; 0: not. Only with SIZE=auto or FILL=0: otherwise EmulationStation starts at once
# and reads them itself meanwhile
PREFETCH=1

# What else runs on the MiSTer closed while ES runs (so that ES has the processor and the memory to itself) and
# started again when ES closes, as the MiSTer starts it at boot; take a name out to leave it running:
#   bgm      the MiSTer BGM (under ES its music skipped)
#   zaparoo  Zaparoo (NFC cards; ES's "write an NFC tag" is not offered meanwhile)
#   arcsnd   the arcade sound server (Konami Hornet, Hyper NeoGeo 64): paused while ES is on screen, running
#            during the games ES starts (it plays their sound)
# These can be added (left running by default): samba (the SD card shared on the network), ftp (the FTP server),
# ntp (the clock's network time), syslog (the system log), gpm (the Linux console's mouse).
# Always left running: the MiSTer's main program, Wi-Fi and the network, SSH, Bluetooth, udev (controllers and
# USB devices plugged in), the consoles' logins and the MiSTer's sync of the SD card every 5 s.
STOP_SERVICES="bgm zaparoo arcsnd"

# Folders of _Arcade that ES does not read (lower case, space apart). _alternatives has thousands of
# other versions in hundreds of folders: reading it from the SD card adds many seconds to ES's start.
# Take it out of the list to see them in ES.
ARCADE_SKIP="cores _organized mame hbmame _alternatives"

# Input devices ES leaves out, by name (comma apart; /proc/bus/input/devices lists them). Main_MiSTer's
# and Zaparoo's virtual keyboards repeat a controller's presses as keys: without this, each press moved
# twice. Physical keyboards and controllers are not affected. The controllers' touchpads (DualSense,
# DualShock 4) are always left out: pressing the pad was a mouse click.
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
	<string name="Language" value="__ES_LANGUAGE__" />
	<bool name="DrawFramerate" value="false" />
	<bool name="VSync" value="true" />
</config>
EOF
sed -i "s/__ES_LANGUAGE__/$ES_LANGUAGE/" "$ES/defaults/es_settings.cfg"
cp "$SRC/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/controllers/es_input.cfg" "$ES/defaults/es_input.cfg"

echo "== theme"
python3 -I "$ROOT/mister/tools/trim-theme.py" "$SRC/es-theme-carbon" "$ES/home/.emulationstation/themes/es-theme-carbon" "$ROOT/mister/systems.tsv" "$THEME_SIZE"

sed -e "s|__ES_DIR__|$ES_NAME|g" -e "s|__ES_SIZE__|${ES_SIZE:-auto}|g" -e "s|__ES_TITLE__|$ES_TITLE${ES_VERSION:+ $ES_VERSION}|g" "$ROOT/mister/README.txt" >"$ES/README.txt"
if [ -n "$ES_VERSION" ]; then
	printf '%s\n' "$ES_VERSION" >"$ES/version.info"
	printf '%s\n' "$ES_TITLE" >"$ES/about.info"
fi
cp "$ROOT/LICENSE.md" "$ES/LICENSE-EmulationStation.md"

# every script with Unix line ends
find "$STAGE" -name '*.sh' -exec sed -i 's/\r$//' {} \;

mkdir -p "$WORK/dist"
ZIP=$WORK/dist/$ES_NAME-$VERSION${ES_SIZE:+-$ES_SIZE}.zip
rm -f "$ZIP"
(cd "$STAGE" && python3 -I -c '
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    for top in (sys.argv[2], "Scripts"):
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
' "$ZIP" "$ES_NAME")
du -sh "$ES" "$ZIP"
