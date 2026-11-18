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

# the core loaded now, in $core, read by bash itself (no cat), and a wait of SECONDS without a sleep process
# (read's timeout on a pipe nothing writes to; sleep if that read ends any other way): both cost nothing,
# so the core's change is seen within 0.1 s
corename() { core=""; read -r core </tmp/CORENAME 2>/dev/null; }
exec {nap}<> <(:)
wait_s() { read -r -t "$1" -u "$nap" _; [ $? -gt 128 ] || sleep "$1"; }

# the core starts (Main_MiSTer starts again, and names it)
started=0
for i in $(seq 300); do
	wait_s 0.1
	corename
	if [ -n "$core" ] && [ "$core" != "MENU" ]; then
		started=1
		break
	fi
done

if [ $started = 1 ]; then
	echo "core $core started"
	# the game runs until the menu core is loaded again (the OSD's Reset or Exit)
	corename
	while [ "$core" != "MENU" ]; do
		wait_s 0.1
		corename
	done
	echo "back in the menu"
	sleep 2 # Main_MiSTer starting again
else
	echo "the core did not start"
fi

for attempt in 1 2 3; do
	DRAWN=$("$ES/es-mister-helper" console "$SCREEN") && break
	echo "the framebuffer is not on yet (try $attempt)"
	sleep 3
done
echo "framebuffer $DRAWN"
