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
uptime() { local u rest; read -r u rest </proc/uptime; echo "$u"; }
echo "== $(date '+%F %T') es-mister.sh (uptime $(uptime) s)"

# settings (es-mister.ini): SIZE, FILL, MENU_ASK, FPS, GAMES_ROOTS
SIZE=auto
FILL=1
MENU_ASK=1
FPS=60
BENCH=0
PROFILE=0
SLOW_DUMPS=4
STRIP=auto
PLD=256
CAPTURE=0
PICTURE_CACHE=1
PREFETCH=1
ARCADE_SKIP="cores _organized mame hbmame _alternatives"
INPUT_IGNORE="MiSTer virtual input,Zaparoo"
GAMES_ROOTS="/media/fat /media/usb0 /media/usb1 /media/usb2 /media/usb3 /media/usb4 /media/usb5 /media/fat/cifs"
[ -f "$ES/es-mister.ini" ] && . "$ES/es-mister.ini"

# the framebuffer's size: SIZE, or with FILL=1 and SIZE as WIDTHxHEIGHT the screen's (unless the MiSTer
# fills the screen with SIZE itself), ES's frames enlarged to it as they are copied (ES_SOFT_DRAW_SIZE)
SCREEN=$SIZE
case "$FILL:$SIZE" in
1:auto*) ;;
1:*x*) SCREEN=fill:$SIZE ;;
esac

# ES on the screen again (the Scripts menu's emulationstation.sh, with ES running hidden: after F12 the
# MiSTer menu covers it). The script's screen closes first, then F9 until Main_MiSTer shows ES's console,
# at the size ES draws at (as after a game).
show() {
	local drawn
	drawn=$(cat "$LOGS/es-mister.size" 2>/dev/null)
	[ -n "$drawn" ] || drawn=$SCREEN
	echo "== $(date '+%F %T') ES running: on the screen again at $drawn"
	sleep 2
	for attempt in 1 2 3 4 5 6 7; do
		"$ES/es-mister-helper" console "$drawn" 8 >/dev/null && return 0
		echo "the framebuffer is not on yet (try $attempt)"
		sleep 1
	done
	echo "ES could not be shown again"
	return 1
}

if [ -f /tmp/es-mister.pid ] && kill -0 "$(cat /tmp/es-mister.pid)" 2>/dev/null; then
	if [ "${1:-}" = "--show" ]; then
		show
		exit $?
	fi
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
	# killed, it left the console in graphics mode, where no console switch (F9) happens
	"$ES/es-mister-helper" text
fi

# first start: the default settings and controllers
for file in es_settings.cfg es_input.cfg; do
	[ -f "$CONFIG/$file" ] || cp "$ES/defaults/$file" "$CONFIG/$file"
done

# ES's settings (its environment), and the files a quit of ES's menu leaves for below cleared
es_settings() {
	cd "$ES"
	export HOME=$HOME_DIR
	# the framebuffer kept at ES's size whenever ES's console is shown (F9 after F12 or after a script shows
	# it at the console's size)
	export ES_SOFT_MISTER_MODE=1
	# ES drawn at SIZE whatever the framebuffer's size, its frames enlarged to it (1280x720 on a 1080p screen:
	# 1.5 times, filling it, where the MiSTer's scaler enlarges only by whole times)
	case "$SCREEN" in fill:*) export ES_SOFT_DRAW_SIZE=$SIZE ;; esac
	# the MiSTer menu's button on a controller (the DualSense's PS) asks first while ES is shown, and on yes
	# ES closes (the MiSTer menu loaded below)
	export ES_MISTER_MENU_ASK=$MENU_ASK
	export SDL_AUDIODRIVER=alsa
	export ES_SOFT_FPS=$FPS
	# the drawing times, a line every 600 frames (the previous start's kept beside it: ES adds to the file);
	# ES_SOFT_STATS set before (a measurement: 0 for the lines a script asks for only) stays
	export ES_SOFT_STATS=${ES_SOFT_STATS:-600}
	[ -f "$LOGS/es-mister-stats.txt" ] && mv -f "$LOGS/es-mister-stats.txt" "$LOGS/es-mister-stats.prev.txt"
	export ES_SOFT_STATS_FILE=$LOGS/es-mister-stats.txt
	# how long each step of ES's start takes (the previous start's is kept beside it)
	[ -f "$LOGS/es-startup.log" ] && mv -f "$LOGS/es-startup.log" "$LOGS/es-startup.prev.log"
	export ES_STARTUP_TRACE=${ES_STARTUP_TRACE:-$LOGS/es-startup.log}
	[ "$BENCH" = 1 ] && export ES_SOFT_BENCH=1
	# where ES spends the processor (logs/es-profile.txt), for the port's tuning
	[ "$PROFILE" = 1 ] && export ES_PROFILE=$LOGS/es-profile.txt
	export ES_ARCADE_SKIP="$ARCADE_SKIP"
	# rows drawn at a time through all the layers (they stay in the processor's cache); 0: a band at once
	export ES_SOFT_STRIP="$STRIP"
	# memory asked for ahead of the drawing and the copy to the screen, in bytes (0: not)
	export ES_SOFT_PLD="$PLD"
	# the first frames slow to draw, with each draw's time (logs/es-slow-frame-N.txt), to see what to make faster
	if [ "$SLOW_DUMPS" -gt 0 ] 2>/dev/null; then
		rm -f "$LOGS"/es-slow-frame-*.txt
		rm -f "$LOGS"/es-slow-frame-*.escf
		export ES_SOFT_SLOW_DUMP=$LOGS/es-slow-frame- ES_SOFT_SLOW_DUMPS=$SLOW_DUMPS
		# and the frames themselves, to draw again with es-mister-replay
		[ "$CAPTURE" = 1 ] && export ES_SOFT_SLOW_CAPTURE=1
	fi
	# pictures kept decoded and at their size, without loss (cache/pictures; ~70 KB a cover), so that a cover
	# seen once comes back ~20 times faster; PICTURE_CACHE=0: none
	[ "$PICTURE_CACHE" = 1 ] && export ES_PICTURE_CACHE=${ES_PICTURE_CACHE:-$ES/cache/pictures}
	# keyboards ES leaves out: Main_MiSTer's and Zaparoo's virtual ones repeat a controller's presses as keys
	export SDL_EVDEV_IGNORE="$INPUT_IGNORE"
	# RESTART SYSTEM and SHUTDOWN OPERATING SYSTEM in ES's menu leave these (ES's processQuitMode) for
	# below
	rm -f /tmp/reboot.please /tmp/shutdown.please
}

# With FILL=1 and SIZE as WIDTHxHEIGHT the size ES draws at is known before the framebuffer is on: ES starts
# now, and loads its systems while es_systems.cfg is made and the framebuffer is asked for below (the
# moment and the F9 presses, ~4 s). It reads es_systems.cfg once SYSTEMS_READY is there (ES_SYSTEMS_WAIT),
# and opens the framebuffer, the console and the controllers only once SCREEN_READY is there (written when
# Main_MiSTer shows the framebuffer: ES_SOFT_OUTPUT_WAIT). Otherwise it starts then, as before.
SCREEN_READY=/tmp/es-mister.screen
SYSTEMS_READY=/tmp/es-mister.systems
rm -f "$SCREEN_READY" "$SYSTEMS_READY"
ES_PID=""
EARLY=""
case "$SCREEN" in
fill:*x*)
	EARLY=1
	es_settings
	export ES_SOFT_OUTPUT_WAIT=$SCREEN_READY ES_SYSTEMS_WAIT=$SYSTEMS_READY
	echo "starting ES: it loads while the framebuffer is asked for (SIZE=$SIZE FILL=$FILL, uptime $(uptime) s)"
	./emulationstation &
	ES_PID=$!
	;;
esac

# the systems, from systems.tsv, each with the first games folder of its that exists (an
# es_systems.cfg of your own: create es_systems.cfg.keep beside it and it is left as it is)
# $1 with XML's special characters escaped, in REPLY: bash's own expansions, no sed or subshell (a fork and
# an exec cost ~10-20 ms each on the MiSTer's ARM: with three a system, this file took ~3.9 s)
xml() {
	local s=$1
	s=${s//&/\&amp;}
	s=${s//</\&lt;}
	s=${s//>/\&gt;}
	s=${s//\"/\&quot;}
	REPLY=$s
}
SYSTEM_PATHS=()
if [ ! -f "$CONFIG/es_systems.cfg.keep" ]; then
	# made in memory and written at once: the SD card is mounted sync, so each line written on its own was a
	# write to the card (~4 ms each, ~350 lines: ~1.5 s)
	cfg=""
	{
		cfg+='<?xml version="1.0"?>'$'\n'
		cfg+='<systemList>'$'\n'
		while IFS=$'\t' read -r name full folder exts rbf setname method delay index reset platform theme; do
			case "$name" in '' | '#'*) continue ;; esac
			path=""
			if [ "${folder:0:1}" = "@" ]; then
				path=/media/fat/${folder:1}
			else
				# the first of the folders ("|" apart) that exists, in their order, on the first card or drive
				# that has it
				rest=$folder
				while [ -z "$path" ] && [ -n "$rest" ]; do
					one=${rest%%|*}
					if [ "$one" = "$rest" ]; then rest=""; else rest=${rest#*|}; fi
					for root in $GAMES_ROOTS; do
						if [ -d "$root/games/$one" ]; then
							path=$root/games/$one
							break
						fi
					done
				done
				[ -n "$path" ] || path=/media/fat/games/${folder%%|*}
			fi
			SYSTEM_PATHS+=("$path")
			cfg+="	<system>"$'\n'
			cfg+="		<name>$name</name>"$'\n'
			xml "$full"
			cfg+="		<fullname>$REPLY</fullname>"$'\n'
			xml "$path"
			cfg+="		<path>$REPLY</path>"$'\n'
			cfg+="		<extension>$exts ${exts^^}</extension>"$'\n'
			cfg+="		<command>bash $ES/mister-launch.sh %SYSTEM% %ROM%</command>"$'\n'
			# the platform as ES's PlatformIds knows it (its scrapers and the scraper's "systems included" go by
			# it) and the theme's name for the system (its art): systems.tsv's, else the system's own name
			case $platform in '' | -) platform=$name ;; esac
			case $theme in '' | -) theme=$name ;; esac
			cfg+="		<platform>$platform</platform>"$'\n'
			cfg+="		<theme>$theme</theme>"$'\n'
			cfg+="	</system>"$'\n'
		done <"$ES/systems.tsv"
		cfg+='</systemList>'$'\n'
	}
	# one write (dd gathers what printf hands it a line at a time); without dd, printf to the file
	printf '%s' "$cfg" | dd of="$CONFIG/es_systems.cfg" bs=1M iflag=fullblock status=none 2>/dev/null ||
		printf '%s' "$cfg" >"$CONFIG/es_systems.cfg"
fi
echo "es_systems.cfg ready (uptime $(uptime) s)"
: >"$SYSTEMS_READY"

# What ES reads first, read now in the background while this script waits for Main_MiSTer below (the moment
# and the F9 presses, ~3 s): the games folders (as ES walks them, less the folders it leaves out), their
# gamelist.xml, the theme's XML files, ES's own resources and ES itself. On a cold start (after the MiSTer
# is switched on) ES then finds them in memory (the page cache) instead of on the SD card. Only read: nothing
# written or changed. PREFETCH=0: not.
prefetch() {
	local skip=() name i
	for name in $ARCADE_SKIP palettes; do
		skip+=(-iname "$name" -o)
	done
	unset 'skip[${#skip[@]}-1]'
	cat "$ES/emulationstation" >/dev/null 2>&1 &
	{
		find "$ES/resources" -type f \( -name "*.xml" -o -name "*.png" -o -name "*.ttf" \) -exec cat {} + >/dev/null 2>&1
		find "$CONFIG/themes" -type f \( -name "*.xml" -o -name "*.ttf" -o -name "*.otf" \) -exec cat {} + >/dev/null 2>&1
	} &
	# the folders in three walks at once (the SD card answers several reads better than one at a time)
	for i in 0 1 2; do
		{
			local n
			for ((n = i; n < ${#SYSTEM_PATHS[@]}; n += 3)); do
				local dir=${SYSTEM_PATHS[$n]}
				[ -d "$dir" ] || continue
				[ -f "$dir/gamelist.xml" ] && cat "$dir/gamelist.xml" >/dev/null 2>&1
				find "$dir" -mindepth 1 \( "${skip[@]}" \) -prune -o -type d -print >/dev/null 2>&1
			done
		} &
	done
}
# Not when ES starts before the framebuffer is on (FILL=1, SIZE as WIDTHxHEIGHT, below): ES reads all this
# itself meanwhile, and both reading at once on the SD card made each slower (a cold start 8.6 s without,
# 10.8 s with)
[ -z "$EARLY" ] && [ "$PREFETCH" = 1 ] && [ ${#SYSTEM_PATHS[@]} -gt 0 ] && prefetch </dev/null >/dev/null 2>&1

# the menu core is loaded (ES starts from the Scripts menu, or after a game)
for i in $(seq 30); do
	[ "$(cat /tmp/CORENAME 2>/dev/null)" = "MENU" ] && break
	sleep 1
done

# ES's settings, for an ES started early (above) or now
[ -n "$EARLY" ] || es_settings


# the framebuffer ES draws in: at SIZE, with auto at the size Main_MiSTer gives it for the menu, with fill
# at the screen's (ES enlarges its frames to it).
# Started from the Scripts menu, Main_MiSTer is still closing the script's screen and does not take F9
# yet: a moment first, then short tries (8 presses, about 4 s each) until it does, for up to ~30 s.
echo "the menu core is loaded (uptime $(uptime) s)"
sleep 2
echo "asking for the framebuffer (uptime $(uptime) s)"
framebuffer_on() {
	DRAWN=""
	for attempt in 1 2 3 4 5 6 7; do
		DRAWN=$("$ES/es-mister-helper" console "$SCREEN" 8) && return 0
		DRAWN=""
		echo "the framebuffer is not on yet (try $attempt)"
		sleep 1
	done
	return 1
}
if ! framebuffer_on; then
	# Main_MiSTer did not take F9 (seen with its framebuffer left on without ES): the menu core loaded again,
	# so Main_MiSTer starts afresh (as after any game), and the same tries once more
	echo "the menu core loaded again, then the same tries"
	"$ES/es-mister-helper" cmd "load_core /media/fat/menu.rbf"
	sleep 3
	for i in $(seq 30); do
		[ "$(cat /tmp/CORENAME 2>/dev/null)" = "MENU" ] && break
		sleep 1
	done
	sleep 2
	framebuffer_on
fi
if [ -z "$DRAWN" ]; then
	echo "no Linux framebuffer: ES does not start"
	if [ -n "$ES_PID" ]; then
		kill "$ES_PID" 2>/dev/null
		wait "$ES_PID"
	fi
	rm -f /tmp/es-mister.pid "$SCREEN_READY" "$SYSTEMS_READY"
	exit 1
fi

# its size, to show it again at it (show, above)
echo "$DRAWN" >"$LOGS/es-mister.size"

# a console switch ES does not let go of within a second (Main_MiSTer running a script after F12 waited
# for it for ever, the menu frozen) is let go of by the guard (es-mister-helper guard)
"$ES/es-mister-helper" guard $$ &
GUARD=$!
if [ -n "$ES_PID" ]; then
	echo "$DRAWN" >"$SCREEN_READY"
	echo "the framebuffer is on: ES shown at $DRAWN (uptime $(uptime) s)"
else
	echo "starting ES: framebuffer $DRAWN (SIZE=$SIZE FILL=$FILL, uptime $(uptime) s)"
	./emulationstation &
	ES_PID=$!
fi
wait "$ES_PID"
echo "ES closed ($?)"
rm -f "$SCREEN_READY" "$SYSTEMS_READY"
kill $GUARD 2>/dev/null
# ES ended by a signal (killall, a crash, out of memory: 128 + the signal above) leaves the console in
# graphics mode, where the kernel ignores console switches: no F9, and no script shown from the MiSTer
# menu, until a reboot. Text mode again, whatever the way ES ended (after a normal end, nothing changes).
"$ES/es-mister-helper" text

rm -f /tmp/es-mister.pid
# RESTART SYSTEM: the MiSTer restarted cold, as its menu's Reboot held; SHUTDOWN OPERATING SYSTEM: shut
# down, the HDMI output off (es-mister-helper reboot / halt; ES has written its files and closed)
if [ -f /tmp/reboot.please ]; then
	rm -f /tmp/reboot.please
	echo "== $(date '+%F %T') RESTART SYSTEM: cold reboot"
	sync
	exec "$ES/es-mister-helper" reboot
fi
if [ -f /tmp/shutdown.please ]; then
	rm -f /tmp/shutdown.please
	echo "== $(date '+%F %T') SHUTDOWN OPERATING SYSTEM: halt"
	sync
	exec "$ES/es-mister-helper" halt
fi
# back to the MiSTer menu
"$ES/es-mister-helper" cmd "load_core /media/fat/menu.rbf"
