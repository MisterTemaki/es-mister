#!/bin/bash
# EmulationStation on the MiSTer FPGA: started by Scripts/emulationstation.sh (in the background), it
# makes ES's configuration, asks Main_MiSTer for the Linux framebuffer, runs ES on it and, when ES
# closes, loads the MiSTer menu again. A game chosen in ES is started by mister-launch.sh.

ES=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
HOME_DIR=$ES/home
CONFIG=$HOME_DIR/.emulationstation
LOGS=$ES/logs
mkdir -p "$CONFIG" "$LOGS" "$ES/cache"
LOG=$LOGS/es-mister.log
exec >>"$LOG" 2>&1
# --show: ES running hidden (after F12), on the screen again; --scripts PID: started from the Scripts menu by
# emulationstation.sh (PID), which stays while ES runs (below: ES shown on the script's console at once)
SHOW=""
FROM_SCRIPTS=""
SCRIPTS_PID=""
while [ $# -gt 0 ]; do
	case "$1" in
	--show) SHOW=1 ;;
	--scripts)
		FROM_SCRIPTS=1
		case "${2:-}" in '' | *[!0-9]*) ;; *) SCRIPTS_PID=$2; shift ;; esac
		;;
	esac
	shift
done
# emulationstation.sh let end (Main_MiSTer then closes the script's screen, and takes F9 afterwards)
scripts_release() {
	[ -n "$SCRIPTS_PID" ] && kill "$SCRIPTS_PID" 2>/dev/null
	SCRIPTS_PID=""
}

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
THEME_CACHE=1
PREFETCH=1
SPLASH_TIME=2.2
STOP_SERVICES="bgm zaparoo arcsnd"
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

# Started from the Scripts menu and not shown on the script's console (below): Main_MiSTer takes no F9 until
# the script has ended (emulationstation.sh, let end: scripts_release) and it has seen it end; its screen then
# waits for a key (the first F9 closes it, the next shows ES's console). Waited for here, up to 3 s.
scripts_screen_wait() {
	[ -n "$FROM_SCRIPTS" ] || return 0
	"$ES/es-mister-helper" scripts-wait
}

# ES on the screen again (the Scripts menu's emulationstation.sh, with ES running hidden: after F12 the
# MiSTer menu covers it). The script's screen closes first, then F9 until Main_MiSTer shows ES's console,
# at the size ES draws at (as after a game).
show() {
	local drawn
	drawn=$(cat "$LOGS/es-mister.size" 2>/dev/null)
	[ -n "$drawn" ] || drawn=$SCREEN
	echo "== $(date '+%F %T') ES running: on the screen again at $drawn"
	if [ -n "$FROM_SCRIPTS" ] && "$ES/es-mister-helper" scripts "fill:$drawn" >/dev/null; then
		return 0
	fi
	scripts_release
	scripts_screen_wait
	for attempt in 1 2 3 4 5 6 7; do
		"$ES/es-mister-helper" console "$drawn" 8 >/dev/null && return 0
		echo "the framebuffer is not on yet (try $attempt)"
		sleep 1
	done
	echo "ES could not be shown again"
	return 1
}

if [ -f /tmp/es-mister.pid ] && kill -0 "$(cat /tmp/es-mister.pid)" 2>/dev/null; then
	if [ -n "$SHOW" ]; then
		show
		exit $?
	fi
	echo "already running"
	scripts_release
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

# Started from the Scripts menu: while emulationstation.sh runs, Main_MiSTer shows the Linux framebuffer with its
# console, so ES is shown there at once (black until it draws, ES started below), with no F9 (Main_MiSTer takes
# it only once the script has ended and its screen has closed, its menu's background then drawn again: 3-4 s
# with a wallpaper). emulationstation.sh stays until ES closes; meanwhile F12 does not show the MiSTer menu over
# ES (Main_MiSTer takes no keys while a script runs on the framebuffer) until a game has been played. When that
# cannot be (a SIZE without FILL), emulationstation.sh is let end and F9 is used, as before.
# es-mister-helper switches to that console and paints ES's splash there while ES starts (below); its answer
# (the size the framebuffer is drawn at, or nothing: then F9) is read once ES has started (scripts_drawn), and
# only then may ES open the screen (SCREEN_READY), so that its frames come after the helper's.
SCRIPTS_DRAWN=""
SCRIPTS_FD=""
if [ -n "$FROM_SCRIPTS" ]; then
	case "$SCREEN" in
	fill:*x*) exec {SCRIPTS_FD}< <("$ES/es-mister-helper" scripts "$SCREEN" "$ES/cache/splash.snapshot") ;;
	*) scripts_release ;;
	esac
fi
scripts_drawn() {
	[ -n "$SCRIPTS_FD" ] || return 0
	read -r -u "$SCRIPTS_FD" SCRIPTS_DRAWN || true
	exec {SCRIPTS_FD}<&-
	SCRIPTS_FD=""
	case "$SCRIPTS_DRAWN" in
	*x*) ;;
	*)
		SCRIPTS_DRAWN=""
		scripts_release
		;;
	esac
}

# What else runs on the MiSTer closed while ES runs, so that ES has the processor and the memory to itself, and
# started again when ES closes (also on any other way out of this script), each as the MiSTer starts it at boot:
# by its own init script or command, with the boot's environment. STOP_SERVICES names them; by default:
#   bgm      the MiSTer BGM (mrext's bgm.sh or the older Python one; under ES its music skipped)
#   zaparoo  Zaparoo (NFC cards; ES's "write an NFC tag" is not offered meanwhile)
#   arcsnd   the arcade sound server (Konami Hornet, Hyper NeoGeo 64): paused while ES is on screen, and running
#            during the games ES starts, whose sound it plays (mister-launch.sh)
# and, if added (left running by default):
#   samba    the SD card shared on the network (smbd, nmbd)
#   ftp      the FTP server (proftpd)
#   ntp      the clock's network time (ntpd; the clock runs on meanwhile)
#   syslog   the system log (syslogd, klogd; the kernel keeps its messages meanwhile)
#   gpm      the Linux console's mouse
# Always left running: the MiSTer's main program, the network (Wi-Fi, dhcpcd), SSH, Bluetooth with D-Bus (which
# it needs), udev (controllers and USB devices plugged in), the consoles' logins (init starts them again at
# once) and the MiSTer's sync of the SD card every 5 s (/etc/resync: what is written reaches the card even if
# the power is cut). All are sent here, before ES starts, the signal their init script sends; their end is waited
# for in the background once ES has started.
BOOT_ENV=(HOME=/ TERM=vt102 USER=root SHELL=/bin/sh PATH=/sbin:/usr/sbin:/bin:/usr/bin)
BGM_SCRIPT=/media/fat/Scripts/bgm.sh
BGM_SOCKET=/tmp/bgm.sock
ZAPAROO_SCRIPT=/media/fat/Scripts/zaparoo.sh
ZAPAROO_PID=/tmp/zaparoo/core.pid
GPM_COMMAND=(/sbin/gpm -m /dev/input/mice -t imps2)
# their pid files, which the init scripts remove (or a process killed leaves): a start finds none, as at boot
SERVICE_PID_FILES=(/var/run/samba/smbd.pid /var/run/samba/nmbd.pid /var/run/proftpd.pid /var/run/ntpd.pid
	/var/run/syslogd.pid /var/run/klogd.pid /var/run/gpm.pid "$ZAPAROO_PID")
SERVICES_STOPPED=""
SERVICES_JOB=""
CLOSING_PIDS=""
CLOSE_PENDING=""
PAUSED=""
ZAPAROO=""
BGM_PIDS=""

boot_env() { env -i "${BOOT_ENV[@]}" "$@"; }

# the pids given gone (looked at every 10 ms, up to about 2 s), else killed
pids_gone() {
	local i pid left
	for ((i = 0; i < 150; i++)); do
		left=""
		for pid; do kill -0 "$pid" 2>/dev/null && left+=" $pid"; done
		[ -z "$left" ] && return 0
		sleep 0.01
	done
	kill -KILL $left 2>/dev/null
	return 1
}
# Zaparoo's service: its pid file's pid, if that is it (ZAPAROO)
zaparoo_pid() {
	local pid="" args
	ZAPAROO=""
	read -r pid 2>/dev/null <"$ZAPAROO_PID"
	case "$pid" in "" | *[!0-9]*) return 1 ;; esac
	mapfile -d '' -t args 2>/dev/null <"/proc/$pid/cmdline" || return 1
	case "${args[*]}" in *zaparoo*-service*) ZAPAROO=$pid ;; *) return 1 ;; esac
}

# The BGM closed as its own "bgm.sh stop" closes it (SIGTERM to its service, the process its socket names: mrext's
# runs as "bgm.sh exec", found by services_stop's pidof in BGM_PIDS; the Python one's, named python, asked of its
# socket) without starting bgm.sh for it (the Python one takes about a second to start on the MiSTer); both end
# their player as they close (in ~30 ms: ES opens the sound ~4 s after its start), and mrext's removes its copy in
# /tmp.
bgm_stop() {
	[ -S "$BGM_SOCKET" ] && [ -f "$BGM_SCRIPT" ] || return 1
	local pid args service=""
	for pid in $BGM_PIDS; do
		mapfile -d '' -t args 2>/dev/null <"/proc/$pid/cmdline" || continue
		[ "${args[-1]}" = exec ] && service+=" $pid"
	done
	if [ -z "$service" ]; then
		service=$("$ES/es-mister-helper" bgm pid) || return 1
		case "$service" in "" | *[!0-9]*) return 1 ;; esac
	fi
	kill $service 2>/dev/null || return 1
	CLOSING_PIDS+=" $service"
}
# a service closed as its init script closes it (samba: SIGKILL, as S91smb; the others SIGTERM), the signal sent
# from here at once, without running the script (forks are slow, and ES is about to start); its pids kept for
# the wait. 1: not running.
service_close() {
	local pids
	case "$1" in
	zaparoo) [ -f "$ZAPAROO_SCRIPT" ] && zaparoo_pid && pids=$ZAPAROO && kill -TERM $pids ;;
	samba) [ -x /etc/init.d/S91smb ] && pids=$(pidof smbd nmbd) && kill -KILL $pids ;;
	ftp) [ -x /etc/init.d/S50proftpd ] && pids=$(pidof proftpd) && kill -TERM $pids ;;
	ntp) [ -x /etc/init.d/S49ntp ] && pids=$(pidof ntpd) && kill -TERM $pids ;;
	syslog) [ -x /etc/init.d/S01syslogd ] && pids=$(pidof klogd syslogd) && kill -TERM $pids ;;
	gpm)
		pids=$(pidof gpm) || return 1
		local cmdline
		mapfile -d '' -t cmdline 2>/dev/null <"/proc/${pids%% *}/cmdline" && [ ${#cmdline[@]} -gt 0 ] && GPM_COMMAND=("${cmdline[@]}")
		kill -TERM $pids ;;
	*) return 1 ;;
	esac 2>/dev/null || return 1
	CLOSING_PIDS+=" $pids"
}
service_open() {
	case "$1" in
	syslog) boot_env /etc/init.d/S01syslogd start; boot_env /etc/init.d/S02klogd start ;;
	ntp) boot_env /etc/init.d/S49ntp start ;;
	samba) boot_env /etc/init.d/S91smb start ;;
	ftp) boot_env /etc/init.d/S50proftpd start ;;
	zaparoo) boot_env "$ZAPAROO_SCRIPT" -service start ;;
	gpm) boot_env "${GPM_COMMAND[@]}" ;;
	bgm)
		# as at the MiSTer's start ("start"), or "exec" if its bgm.ini has startup = no (it had been started from
		# the Scripts menu)
		if grep -qiE '^[[:space:]]*startup[[:space:]]*=[[:space:]]*(no|false|off|0)[[:space:]]*$' /media/fat/music/bgm.ini; then
			boot_env "$BGM_SCRIPT" exec &
		else
			boot_env "$BGM_SCRIPT" start
		fi ;;
	esac </dev/null >/dev/null 2>&1
}
# the processes paused (pid:name), signalled if still that program
paused_signal() {
	local entry comm
	for entry in $PAUSED; do
		read -r comm 2>/dev/null <"/proc/${entry%%:*}/comm" && [ "$comm" = "${entry#*:}" ] && kill -"$1" "${entry%%:*}"
	done
	return 0
}
# after the closing, in the background: the ends waited for (one that did not end in 2 s killed), pid files left
# removed, and what the BGM may leave (its player if it had to be killed, the Python one's inotifywait: left to
# init; a socket nobody answers on, which its next start would take for a running service)
services_closed() {
	local name pid stat
	[ -n "${CLOSING_PIDS// /}" ] && { pids_gone $CLOSING_PIDS || echo "some did not end in 2 s: killed"; }
	for name in "${SERVICE_PID_FILES[@]}"; do
		pid=""
		read -r pid 2>/dev/null <"$name"
		[ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null && rm -f "$name"
	done
	case " $SERVICES_STOPPED " in
	*" bgm "*)
		for pid in $(pidof mpg123 ogg123 aplay aplaymidi inotifywait); do
			read -r stat 2>/dev/null <"/proc/$pid/stat" || continue
			stat=${stat##*) }
			stat=${stat#* }
			[ "${stat%% *}" = 1 ] && kill "$pid" 2>/dev/null && echo "left by the BGM, ended: pid $pid"
		done
		[ -S "$BGM_SOCKET" ] && ! "$ES/es-mister-helper" bgm status >/dev/null 2>&1 && rm -f "$BGM_SOCKET" ;;
	esac
	[ -n "$SERVICES_STOPPED$PAUSED" ] && echo "[$(cut -d' ' -f1 /proc/uptime)] closed while ES runs:${SERVICES_STOPPED:- none}${PAUSED:+; paused:$PAUSED}"
	return 0
}

services_stop() {
	local name pid comm arcsnd=""
	BGM_PIDS=""
	# the BGM's and arcsnd's pids with one pidof (each fork costs ~15 ms on the MiSTer)
	for pid in $(pidof bgm.sh arcsnd); do
		read -r comm 2>/dev/null <"/proc/$pid/comm" || continue
		case "$comm" in bgm.sh) BGM_PIDS+=" $pid" ;; arcsnd) arcsnd+=" $pid" ;; esac
	done
	for name in $STOP_SERVICES; do
		case "$name" in
		bgm) bgm_stop && SERVICES_STOPPED+=" bgm" ;;
		arcsnd)
			for pid in $arcsnd; do
				# one left paused by an ES that did not end well (its script killed) taken as running
				kill -STOP "$pid" && PAUSED+=" $pid:arcsnd"
			done ;;
		*) service_close "$name" && SERVICES_STOPPED+=" $name" ;;
		esac
	done
	# for mister-launch.sh: running during a game
	export ES_MISTER_PAUSED="$PAUSED"
	CLOSE_PENDING=1
}
# the background part, once ES has started (its fork, ~20 ms, after ES's)
services_closing() {
	[ -n "$CLOSE_PENDING" ] || return 0
	CLOSE_PENDING=""
	services_closed &
	SERVICES_JOB=$!
}
services_start() {
	[ -n "$SERVICES_STOPPED$PAUSED" ] || return 0
	local stopped=$SERVICES_STOPPED name
	if [ -n "$CLOSE_PENDING" ]; then
		CLOSE_PENDING=""
		services_closed
	elif [ -n "$SERVICES_JOB" ]; then
		wait "$SERVICES_JOB" 2>/dev/null
	fi
	paused_signal CONT
	SERVICES_STOPPED=""
	PAUSED=""
	SERVICES_JOB=""
	# in the background (the BGM's "start" waits for nothing; Zaparoo's takes ~0.3 s), in the boot's order
	(
		cd / || exit
		for name in syslog ntp samba ftp zaparoo gpm bgm; do
			case " $stopped " in *" $name "*) service_open "$name" ;; esac
		done
		echo "[$(cut -d' ' -f1 /proc/uptime)] started again:$stopped"
	) &
}
# with ES started early (FILL, below) right after it, so that it starts sooner; otherwise now
case "$SCREEN" in fill:*x*) ;; *) services_stop ;; esac
trap services_start EXIT

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
	# ES's first frame (the splash) kept there, painted by es-mister-helper at the next start from the Scripts
	# menu before ES draws
	export ES_SPLASH_SNAPSHOT=$ES/cache/splash.snapshot
	# the logo (ES's splash) seen at least SPLASH_TIME seconds once ES's screen is shown, when ES is ready
	# sooner; in ms for ES (2.2: 2200), 0 or not a number: not held
	local whole frac
	case "$SPLASH_TIME" in
	'' | *[!0-9.]* | *.*.* | .) export ES_SPLASH_MIN_MS=0 ;;
	*)
		whole=${SPLASH_TIME%%.*}
		frac=${SPLASH_TIME#"$whole"}
		frac=${frac#.}000
		export ES_SPLASH_MIN_MS=$((10#${whole:-0} * 1000 + 10#${frac:0:3}))
		;;
	esac
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
	# each system's theme as the theme built it (cache/themes; ~130 KB a system), loaded again while nothing it
	# depends on changed, instead of parsing the theme for each system at every start; THEME_CACHE=0: none
	[ "$THEME_CACHE" = 1 ] && export ES_THEME_CACHE=${ES_THEME_CACHE:-$ES/cache/themes}
	# keyboards ES leaves out: Main_MiSTer's and Zaparoo's virtual ones repeat a controller's presses as keys.
	# And always the controllers' touchpads (DualSense, DualShock 4: "... Controller Touchpad"), which SDL took
	# for a mouse: pressing the pad (easily done reaching for Create or Options, on either side of it) was a
	# click, which closed the menu just opened or cancelled the press
	export SDL_EVDEV_IGNORE="${INPUT_IGNORE:+$INPUT_IGNORE,}Controller Touchpad"
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
	if [ -n "$SCRIPTS_FD" ]; then
		./emulationstation {SCRIPTS_FD}<&- &
	else
		./emulationstation &
	fi
	ES_PID=$!
	services_stop
	# on the script's console already (--scripts), its splash painted: ES draws there from its first frame
	scripts_drawn
	[ -n "$SCRIPTS_DRAWN" ] && echo "$SCRIPTS_DRAWN" >"$SCREEN_READY"
	services_closing
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
# Started from the Scripts menu: once Main_MiSTer has seen the script end (scripts_screen_wait, above), short
# tries (8 presses, about 4 s each) until it takes F9, for up to ~30 s.
echo "the menu core is loaded (uptime $(uptime) s)"
scripts_drawn
if [ -z "$SCRIPTS_DRAWN" ]; then
	scripts_screen_wait || echo "the Scripts menu's screen did not end in 3 s"
	echo "asking for the framebuffer (uptime $(uptime) s)"
fi
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
if [ -n "$SCRIPTS_DRAWN" ]; then
	DRAWN=$SCRIPTS_DRAWN
elif ! framebuffer_on; then
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
	services_closing
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
# back to the MiSTer menu, and what was closed while ES ran started again
"$ES/es-mister-helper" cmd "load_core /media/fat/menu.rbf"
services_start
