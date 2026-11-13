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
echo "== $(date '+%F %T') es-mister.sh"

# settings (es-mister.ini): SIZE, FPS, GAMES_ROOTS
SIZE=640x360
FPS=60
BENCH=0
GAMES_ROOTS="/media/fat /media/usb0 /media/usb1 /media/usb2 /media/usb3 /media/usb4 /media/usb5 /media/fat/cifs"
[ -f "$ES/es-mister.ini" ] && . "$ES/es-mister.ini"

if [ -f /tmp/es-mister.pid ] && kill -0 "$(cat /tmp/es-mister.pid)" 2>/dev/null; then
	echo "already running"
	exit 0
fi
echo $$ >/tmp/es-mister.pid

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

if ! "$ES/es-mister-helper" console "$SIZE"; then
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
[ "$BENCH" = 1 ] && export ES_SOFT_BENCH=1
echo "starting ES at $SIZE"
./emulationstation
echo "ES closed ($?)"

rm -f /tmp/es-mister.pid
# back to the MiSTer menu
echo "load_core /media/fat/menu.rbf" >/dev/MiSTer_cmd
