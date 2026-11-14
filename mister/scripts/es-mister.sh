#!/bin/bash
# EmulationStation on the MiSTer FPGA: started by Scripts/emulationstation.sh (in the background), it
# makes ES's configuration, asks Main_MiSTer for the Linux framebuffer, runs ES on it and, when ES
# closes, loads the MiSTer menu again. A game chosen in ES is started by mister-launch.sh.

ES=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
HOME_DIR=$ES/home
CONFIG=$HOME_DIR/.emulationstation
LOGS=$ES/logs
mkdir -p "$CONFIG" "$LOGS"
LOG=$LOGS/es-mister.log
exec >>"$LOG" 2>&1
# times since the machine started (as ES's startup trace has them): from here to ES on the screen
uptime() { cut -d' ' -f1 /proc/uptime; }
echo "== $(date '+%F %T') es-mister.sh (uptime $(uptime) s)"

# settings (es-mister.ini): SIZE, FPS, GAMES_ROOTS
SIZE=auto
FPS=60
BENCH=0
PROFILE=0
SLOW_DUMPS=4
STRIP=8
PLD=256
ARCADE_SKIP="cores _organized mame hbmame _alternatives"
INPUT_IGNORE="MiSTer virtual input,Zaparoo"
GAMES_ROOTS="/media/fat /media/usb0 /media/usb1 /media/usb2 /media/usb3 /media/usb4 /media/usb5 /media/fat/cifs"
[ -f "$ES/es-mister.ini" ] && . "$ES/es-mister.ini"

if [ -f /tmp/es-mister.pid ] && kill -0 "$(cat /tmp/es-mister.pid)" 2>/dev/null; then
	echo "already running"
	exit 0
fi
echo $$ >/tmp/es-mister.pid

# an ES left from a start that did not end well (it holds the framebuffer)
if pidof emulationstation >/dev/null 2>&1; then
	echo "an earlier ES is still running: closing it"
	killall emulationstation 2>/dev/null
	sleep 1
	killall -9 emulationstation 2>/dev/null
fi

# first start: the default settings and controllers
for file in es_settings.cfg es_input.cfg; do
	[ -f "$CONFIG/$file" ] || cp "$ES/defaults/$file" "$CONFIG/$file"
done

# the systems, from systems.tsv, each with the first games folder of its that exists (an
# es_systems.cfg of your own: create es_systems.cfg.keep beside it and it is left as it is)
xml() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'; }
if [ ! -f "$CONFIG/es_systems.cfg.keep" ]; then
	{
		echo '<?xml version="1.0"?>'
		echo '<systemList>'
		while IFS=$'\t' read -r name full folder exts rbf setname method delay index; do
			case "$name" in '' | '#'*) continue ;; esac
			path=""
			if [ "${folder:0:1}" = "@" ]; then
				path=/media/fat/${folder:1}
			else
				for root in $GAMES_ROOTS; do
					if [ -d "$root/games/$folder" ]; then
						path=$root/games/$folder
						break
					fi
				done
				[ -n "$path" ] || path=/media/fat/games/$folder
			fi
			echo "	<system>"
			echo "		<name>$name</name>"
			echo "		<fullname>$(echo "$full" | xml)</fullname>"
			echo "		<path>$(echo "$path" | xml)</path>"
			echo "		<extension>$exts $(echo "$exts" | tr '[:lower:]' '[:upper:]')</extension>"
			echo "		<command>bash $ES/mister-launch.sh %SYSTEM% %ROM%</command>"
			echo "		<platform>$name</platform>"
			echo "		<theme>$name</theme>"
			echo "	</system>"
		done <"$ES/systems.tsv"
		echo '</systemList>'
	} >"$CONFIG/es_systems.cfg"
fi

# the menu core is loaded (ES starts from the Scripts menu, or after a game)
for i in $(seq 30); do
	[ "$(cat /tmp/CORENAME 2>/dev/null)" = "MENU" ] && break
	sleep 1
done

# the framebuffer ES draws in: at SIZE, or with auto at the size Main_MiSTer gives it for the menu.
# Started from the Scripts menu, Main_MiSTer is still closing the script's screen and does not take F9
# yet: a moment first, then short tries (8 presses, about 4 s each) until it does, for up to ~30 s.
sleep 2
DRAWN=""
for attempt in 1 2 3 4 5 6 7; do
	DRAWN=$("$ES/es-mister-helper" console "$SIZE" 8) && break
	DRAWN=""
	echo "the framebuffer is not on yet (try $attempt)"
	sleep 1
done
if [ -z "$DRAWN" ]; then
	echo "no Linux framebuffer: ES does not start"
	rm -f /tmp/es-mister.pid
	exit 1
fi

cd "$ES"
rm -f /tmp/es-mister-stats.txt
export HOME=$HOME_DIR
export SDL_AUDIODRIVER=alsa
export ES_SOFT_FPS=$FPS
export ES_SOFT_STATS=600
export ES_SOFT_STATS_FILE=$LOGS/es-mister-stats.txt
# how long each step of ES's start takes (the previous start's is kept beside it)
[ -f "$LOGS/es-startup.log" ] && mv -f "$LOGS/es-startup.log" "$LOGS/es-startup.prev.log"
export ES_STARTUP_TRACE=$LOGS/es-startup.log
[ "$BENCH" = 1 ] && export ES_SOFT_BENCH=1
# where ES spends the processor (logs/es-profile.txt), for the port's tuning
[ "$PROFILE" = 1 ] && export ES_PROFILE=$LOGS/es-profile.txt
export ES_ARCADE_SKIP="$ARCADE_SKIP"
# rows drawn at a time through all the layers (they stay in the processor's cache); 0: a band at once
export ES_SOFT_STRIP="$STRIP"
# memory asked for ahead of the drawing (bytes); 0: not asked for
export ES_SOFT_PLD="$PLD"
# the first frames slow to draw, with each draw's time (logs/es-slow-frame-N.txt), to see what to make faster
if [ "$SLOW_DUMPS" -gt 0 ] 2>/dev/null; then
	rm -f "$LOGS"/es-slow-frame-*.txt
	export ES_SOFT_SLOW_DUMP=$LOGS/es-slow-frame- ES_SOFT_SLOW_DUMPS=$SLOW_DUMPS
fi
# keyboards ES leaves out: Main_MiSTer's and Zaparoo's virtual ones repeat a controller's presses as keys
export SDL_EVDEV_IGNORE="$INPUT_IGNORE"
echo "starting ES at $DRAWN (SIZE=$SIZE, uptime $(uptime) s)"
./emulationstation
echo "ES closed ($?)"

rm -f /tmp/es-mister.pid
# back to the MiSTer menu
echo "load_core /media/fat/menu.rbf" >/dev/MiSTer_cmd
