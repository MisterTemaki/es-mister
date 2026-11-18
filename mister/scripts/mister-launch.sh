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

SIZE=auto
FILL=1
[ -f "$ES/es-mister.ini" ] && . "$ES/es-mister.ini"
# the framebuffer's size, as es-mister.sh chooses it
SCREEN=$SIZE
case "$FILL:$SIZE" in
1:auto*) ;;
1:*x*) SCREEN=fill:$SIZE ;;
esac

system=$1
game=$2
echo "== $(date '+%F %T') launch $system: $game"
# a step with the time since the MiSTer started (/proc/uptime, as es-mister.sh's lines): a launch's and the
# way back's steps on one clock with Main_MiSTer's and ES's
stamp() { local up _; read -r up _ </proc/uptime; echo "[$up] $*"; }
stamp "launch asked"

row=$(awk -F'\t' -v s="$system" '$1 == s' "$ES/systems.tsv")
if [ -z "$row" ]; then
	echo "unknown system $system"
	exit 1
fi
IFS=$'\t' read -r name full folder exts rbf setname method delay index reset platform theme <<<"$row"

# Minimig's CD32 (as Zaparoo starts it) plays the disc its configuration names: the path from /media
# (../fat/games/...) in AmigaCD32.cfg, 108 bytes from byte 3100, written here into a copy that then takes
# the file's place. Without that file (the CD32 never started yet) the core starts without the disc.
cd32_disc() {
	local LC_ALL=C cfg=/media/fat/config/AmigaCD32.cfg disc=../${1#/media/}
	if [ ! -f "$cfg" ] || [ "$(stat -c %s "$cfg")" -lt 3208 ]; then
		echo "no CD32 configuration ($cfg): the core starts without the disc"
	elif [ ${#disc} -gt 108 ]; then
		echo "the disc's path is longer than the CD32 configuration takes (108 bytes): the core starts without it"
	else
		cp "$cfg" "$cfg.new" &&
			dd if=/dev/zero of="$cfg.new" bs=1 seek=3100 count=108 conv=notrunc status=none &&
			printf '%s' "$disc" | dd of="$cfg.new" bs=1 seek=3100 conv=notrunc status=none &&
			mv "$cfg.new" "$cfg" && echo "CD32 disc: $disc"
		rm -f "$cfg.new"
	fi
}

ext=${game##*.}
ext=.${ext,,}
if [ "$ext" = ".mra" ] || [ "$ext" = ".mgl" ]; then
	target=$game # Main_MiSTer loads these itself
else
	media=$game
	# a game in a zip: Main_MiSTer loads it from inside, named as a file in a folder (game.zip/game.vb),
	# the zip's first file with one of the system's extensions
	if [ "$ext" = ".zip" ]; then
		inner=""
		while IFS= read -r entry; do
			e=.${entry##*.}
			e=${e,,}
			case " ${exts,,} " in
			*" $e "*) [ "$e" != .zip ] && inner=$entry && break ;;
			esac
		done < <(unzip -Z1 "$game" 2>/dev/null)
		if [ -n "$inner" ]; then media=$game/$inner; else echo "no $exts file in $game"; fi
	fi
	[ "$method" = cd32 ] && cd32_disc "$media"
	target=/tmp/es-mister.mgl
	path=$(printf '%s' "$media" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g')
	{
		echo "<mistergamedescription>"
		echo "	<rbf>$rbf</rbf>"
		[ "$setname" != "-" ] && echo "	<setname>$setname</setname>"
		[ "$method" != cd32 ] && echo "	<file delay=\"$delay\" type=\"$method\" index=\"$index\" path=\"../../../../..$path\"/>"
		# the core's reset held after the game is loaded (delay:hold, seconds), for one that starts it at a boot
		[ "$reset" != "-" ] && [ -n "$reset" ] && echo "	<reset delay=\"${reset%:*}\" hold=\"${reset#*:}\"/>"
		echo "</mistergamedescription>"
	} >"$target"
	cat "$target"
fi

# the controllers all Main_MiSTer's for the game: ES gives back those it holds while it is shown (the MiSTer
# menu's button asks first) when it lets go of the screen; again here, in case it could not
ES_PID=$(pidof emulationstation)
[ -n "$ES_PID" ] && "$ES/es-mister-helper" ungrab "$ES_PID"
# (through the helper: a Main_MiSTer starting again makes its command FIFO anew, and an echo opened meanwhile
# would wait on the old one for ever, ES with it)
"$ES/es-mister-helper" cmd "load_core $target"
stamp "load_core sent"

# the core loaded now, in $core, read by bash itself (no cat)
corename() { core=""; read -r core </tmp/CORENAME 2>/dev/null; }

# the core starts (Main_MiSTer starts again, and names it): a look every 0.1 s, about 1.5 s. (No read -t on a
# pipe in place of sleep here: bash 5.0's read -t can miss its timeout when a signal comes meanwhile, and then
# waits for ever)
started=0
for i in $(seq 300); do
	sleep 0.1
	corename
	if [ -n "$core" ] && [ "$core" != "MENU" ]; then
		started=1
		break
	fi
done

if [ $started = 1 ]; then
	stamp "core $core started"
	# back to ES from the controller too: Create + Options held for a second (a DualSense or DualShock 4,
	# read by the helper) load the menu core. The helper also waits for the game's end: it returns once the
	# menu core is loaded again (the OSD's System > Reboot, or those keys). Before, the helper ran beside a
	# loop here that looked at the core every 0.1 s with bash's read -t; the helper ending as the menu core
	# came, its signal could meet that read, which then waited for ever (seen once on the MiSTer: ES did not
	# come back).
	"$ES/es-mister-helper" exitkeys $$
	# the helper gone another way (it cannot run, say): the same wait, with sleep
	corename
	while [ "$core" != "MENU" ]; do
		sleep 0.2
		corename
	done
	stamp "back in the menu"
	# no wait for Main_MiSTer starting again: the helper waits until it has taken its keyboard, and presses
	# F9 until it switches to the Linux framebuffer (a fixed 2 s here kept the MiSTer menu on screen)
else
	echo "the core did not start"
fi

for attempt in 1 2 3; do
	DRAWN=$("$ES/es-mister-helper" console "$SCREEN") && break
	echo "the framebuffer is not on yet (try $attempt)"
	sleep 3
done
stamp "framebuffer $DRAWN"
