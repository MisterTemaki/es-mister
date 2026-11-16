#!/bin/bash
# EmulationStation (Batocera's, drawn without a GPU) on the MiSTer: starts it on the Linux framebuffer.
# It runs on in the background after this script ends; leave it from its menu (Quit) to come back here.
# F12 shows the MiSTer menu over it, and it keeps running: F9, or this script again, brings it back.

ES=/media/fat/emulationstation

if [ ! -f "$ES/es-mister.sh" ]; then
	echo "EmulationStation is not installed in $ES."
	exit 1
fi

# already running, hidden (F12 shows the MiSTer menu over it): back on the screen
if [ -f /tmp/es-mister.pid ] && kill -0 "$(cat /tmp/es-mister.pid)" 2>/dev/null; then
	echo "EmulationStation is already running: back to it..."
	setsid bash "$ES/es-mister.sh" --show </dev/null >/dev/null 2>&1 &
	sleep 1
	exit 0
fi

echo "Starting EmulationStation..."
setsid bash "$ES/es-mister.sh" </dev/null >/dev/null 2>&1 &
sleep 1
