#!/bin/bash
# EmulationStation (Batocera's, drawn without a GPU) on the MiSTer: starts it on the Linux framebuffer.
# It runs on in the background after this script ends; leave it from its menu (Quit) to come back here.
# F12 shows the MiSTer menu over it, and it keeps running: F9, or this script again, brings it back.

ES=/media/fat/emulationstation

if [ ! -f "$ES/es-mister.sh" ]; then
	echo "EmulationStation is not installed in $ES."
	exit 1
fi

# This script stays while ES runs: Main_MiSTer shows the Linux framebuffer, with this script's console, as long
# as the script runs, and es-mister.sh (--scripts) shows ES there at once, with no F9 (after the script, Main_MiSTer
# takes F9 only once its screen has closed and the menu's background has been drawn again: 3-4 s with a
# wallpaper). It ends when ES closes, or sooner when es-mister.sh ends it (ES shown another way).
stay() {
	local session
	trap 'exit 0' TERM
	while :; do
		kill -0 "$1" 2>/dev/null && { sleep 0.5 & wait $!; continue; }
		session=""
		read -r session 2>/dev/null </tmp/es-mister.pid
		[ -n "$session" ] && kill -0 "$session" 2>/dev/null || break
		sleep 0.5 & wait $!
	done
}

# already running, hidden (F12 shows the MiSTer menu over it): back on the screen
if [ -f /tmp/es-mister.pid ] && kill -0 "$(cat /tmp/es-mister.pid)" 2>/dev/null; then
	echo "EmulationStation is already running: back to it..."
	setsid bash "$ES/es-mister.sh" --show --scripts $$ </dev/null >/dev/null 2>&1 &
	stay $!
	exit 0
fi

echo "Starting EmulationStation..."
setsid bash "$ES/es-mister.sh" --scripts $$ </dev/null >/dev/null 2>&1 &
stay $!
