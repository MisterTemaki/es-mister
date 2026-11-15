#!/bin/bash
# A game chosen in EmulationStation, started on its MiSTer core: ES runs this (es_systems.cfg's command)
# with the system and the game's file, and waits for it. It writes an MGL file (the core, and the game
# to load into it) and has Main_MiSTer load it; the game runs until the player leaves it from the OSD,
# which loads the menu core again; then the Linux framebuffer comes back and this ends, and ES shows
# itself again.
#
#   mister-launch.sh SYSTEM GAME

ES=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LOG=$ES/logs/es-mister.log
exec >>"$LOG" 2>&1
startup_mark() {
	if [ -n "${ES_WRAPPER_TRACE:-}" ]; then
		printf '%s %s\n' "$(cut -d' ' -f1 /proc/uptime)" "$*" >>"$ES_WRAPPER_TRACE"
	fi
}
startup_mark launch_begin

SIZE=auto
[ -f "$ES/es-mister.ini" ] && . "$ES/es-mister.ini"

system=$1
game=$2
echo "== $(date '+%F %T') launch $system: $game"

row=$(awk -F'\t' -v s="$system" '$1 == s' "$ES/systems.tsv")
if [ -z "$row" ]; then
	echo "unknown system $system"
	exit 1
fi
IFS=$'\t' read -r name full folder exts rbf setname method delay index <<<"$row"

ext=${game##*.}
ext=.${ext,,}
if [ "$ext" = ".mra" ] || [ "$ext" = ".mgl" ]; then
	target=$game # Main_MiSTer loads these itself
else
	target=/tmp/es-mister.mgl
	path=$(printf '%s' "$game" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g')
	{
		echo "<mistergamedescription>"
		echo "	<rbf>$rbf</rbf>"
		[ "$setname" != "-" ] && echo "	<setname>$setname</setname>"
		echo "	<file delay=\"$delay\" type=\"$method\" index=\"$index\" path=\"../../../../..$path\"/>"
		echo "</mistergamedescription>"
	} >"$target"
	cat "$target"
fi

startup_mark core_request
echo "load_core $target" >/dev/MiSTer_cmd

# the core starts (Main_MiSTer starts again, and names it)
started=0
for i in $(seq 30); do
	sleep 1
	core=$(cat /tmp/CORENAME 2>/dev/null)
	if [ -n "$core" ] && [ "$core" != "MENU" ]; then
		started=1
		break
	fi
done

if [ $started = 1 ]; then
	startup_mark "core_started name=$core"
	echo "core $core started"
	# the game runs until the menu core is loaded again (the OSD's Reset or Exit)
	while [ "$(cat /tmp/CORENAME 2>/dev/null)" != "MENU" ]; do
		sleep 1
	done
	echo "back in the menu"
	startup_mark return_fixed_wait_begin
	sleep 2 # Main_MiSTer starting again
	startup_mark return_fixed_wait_end
else
	echo "the core did not start"
	startup_mark core_not_started
fi

for attempt in 1 2 3; do
	startup_mark "return_helper_begin attempt=$attempt"
	DRAWN=$("$ES/es-mister-helper" console "$SIZE") && { startup_mark "return_helper_end attempt=$attempt result=ok"; break; }
	startup_mark "return_helper_end attempt=$attempt result=failed"
	echo "the framebuffer is not on yet (try $attempt)"
	sleep 3
done
echo "framebuffer $DRAWN"
startup_mark "return_complete framebuffer=$DRAWN"
