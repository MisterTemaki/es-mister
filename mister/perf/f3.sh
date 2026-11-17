#!/bin/bash
# Phase 3 batch: binaries and scripts compared in the same session, interleaved, each run from a clean state.
#   f3.sh PLAN_FILE
# Each PLAN_FILE line: KIND BIN COUNT [sh=NAME] [helper=NAME] [launch=NAME] [cache=cold|warm] [profile=SECONDS]
#   [VAR=VALUE...]
#   KIND: grid | list (nav.script in that view), ggrid | glist (golden frames, fixed clock), boot (cold boot,
#         page cache emptied, abrir.script), launch (a SNES game started as ES starts it, the menu core loaded
#         again 5 s later; ES not running)
#   BIN:  a binary in $B/bins (es-NAME)
#   sh=, helper=, launch=: es-mister.sh, es-mister-helper, mister-launch.sh from $B/bins (sh-NAME, ...);
#         without them the test copy's own (as saved in $B/bins/*-orig at the first batch)
#   cache=cold: an empty picture cache for this run; cache=warm: the batch's shared one ($B/pc/warm)
# Lines are taken round-robin: one run of each line, then the next round, until each line has its COUNT.
# At the end the test copy as it was and the user's ES started and checked.
T=/media/fat/teste-claude/f1; B=/media/fat/teste-claude/f3; E=/media/fat/emulationstation-claude720p; H=$E/home/.emulationstation
RES=$B/res; mkdir -p $RES $B/pc
log() { echo "$(date +%T) $*" >> $RES/batch.log; }
menu_wait() { for i in $(seq 60); do [ "$(cat /tmp/CORENAME 2>/dev/null)" = MENU ] && return 0; sleep 1; done; return 1; }
up() { cut -d' ' -f1 /proc/uptime; }

# the test copy's own files, once
[ -f $B/bins/sh-orig ] || cp $T/es-mister.sh $B/bins/sh-orig
[ -f $B/bins/helper-orig ] || cp $E/es-mister-helper $B/bins/helper-orig
[ -f $B/bins/launch-orig ] || cp $E/mister-launch.sh $B/bins/launch-orig

clean() {
	killall emulationstation 2>/dev/null
	for i in $(seq 30); do pidof emulationstation >/dev/null || break; sleep 1; done
	pidof emulationstation >/dev/null && { killall -9 emulationstation; sleep 1; }
	for i in $(seq 15); do [ -f /tmp/es-mister.pid ] || break; sleep 1; done
	if [ -f /tmp/es-mister.pid ]; then
		p=$(cat /tmp/es-mister.pid); kill "$p" 2>/dev/null; sleep 1; rm -f /tmp/es-mister.pid
		log "an es-mister.sh still starting ($p) stopped"
		echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd; sleep 3
	fi
	menu_wait
}

# the files a run uses: es-NAME and the scripts asked for (or the test copy's own)
setup() {
	local bin=$1 sh=$2 helper=$3 launch=$4
	cp $B/bins/es-$bin $E/emulationstation
	cp $B/bins/sh-${sh:-orig} $E/es-mister.sh
	cp $B/bins/helper-${helper:-orig} $E/es-mister-helper
	cp $B/bins/launch-${launch:-orig} $E/mister-launch.sh
	chmod +x $E/emulationstation $E/es-mister-helper
}

run_one() {
	local kind=$1 bin=$2 n=$3; shift 3
	local sh="" helper="" launch="" cache="" profile="" env=()
	for a in "$@"; do
		case $a in
			sh=*) sh=${a#sh=} ;;
			helper=*) helper=${a#helper=} ;;
			launch=*) launch=${a#launch=} ;;
			cache=*) cache=${a#cache=} ;;
			profile=*) profile=${a#profile=} ;;
			*) env+=("$a") ;;
		esac
	done
	local tag=$bin; [ -n "$sh" ] && tag=$tag-s$sh; [ -n "$helper" ] && tag=$tag-h$helper; [ -n "$launch" ] && tag=$tag-l$launch; [ -n "$cache" ] && tag=$tag-$cache; [ -n "$profile" ] && tag=$tag-prof
	local name=$kind-$tag-$n R=$RES/$kind-$tag-$n view=grid script=nav
	case $kind in
		list) view=detailed ;;
		ggrid) script=golden2; env+=(ES_PERF_FIXED_STEP=1 ES_SOFT_SHOTS=$R) ;;
		glist) view=detailed; script=golden2; env+=(ES_PERF_FIXED_STEP=1 ES_SOFT_SHOTS=$R) ;;
		boot) script=abrir ;;
	esac
	# sampled from PROFILE seconds after ES starts (ES_PROFILE_FROM), into the run's profile.txt
	[ -n "$profile" ] && env+=(ES_PROFILE=$R/profile.txt ES_PROFILE_FROM=$profile)
	case $cache in
		cold) env+=(ES_PICTURE_CACHE=$B/pc/cold-$kind-$n) ;; # a new, empty one (nothing deleted)
		warm) env+=(ES_PICTURE_CACHE=$B/pc/warm) ;;
	esac
	rm -rf $R; mkdir -p $R
	clean
	setup $bin "$sh" "$helper" "$launch"

	if [ $kind = launch ]; then
		local game="/media/fat/games/SNES/Aero the Acro-Bat 2 (EU).sfc"
		sleep 2
		local s=$(up)
		bash $E/mister-launch.sh snes "$game" </dev/null >/dev/null 2>&1 &
		local lp=$! c="" b=""
		for i in $(seq 1500); do x=$(cat /tmp/CORENAME 2>/dev/null); [ -n "$x" ] && [ "$x" != MENU ] && { c=$(up); break; }; sleep 0.02; done
		sleep 5
		local m=$(up); echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd
		for i in $(seq 1500); do [ "$(cat /tmp/CORENAME 2>/dev/null)" = MENU ] && { b=$(up); break; }; sleep 0.02; done
		wait $lp; local d=$(up)
		echo "launch $n start $s core $c menu-asked $m menu $b script-done $d" > $R/launch.txt
		tail -12 $E/logs/es-mister.log | grep -v "MisterPads\|ERROR" > $R/script.txt
		# the Linux framebuffer was left on without ES (no ES ran the game): there Main_MiSTer takes no F9,
		# so the menu core again, as after any game with ES
		echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd; sleep 3; menu_wait
		log "$name done"
		return
	fi

	cp $T/es_settings.cfg.orig $H/es_settings.cfg
	sed -i -e 's/name="LastSystem" value="[^"]*"/name="LastSystem" value="snes"/' $H/es_settings.cfg
	sed -i "s|</config>|	<string name=\"snes.defaultView\" value=\"$view\" />\n</config>|" $H/es_settings.cfg
	[ $kind = boot ] && { sync; echo 3 > /proc/sys/vm/drop_caches; }
	sleep 2
	local before=$(wc -l < $E/logs/es-mister.log)
	( while :; do p=$(pidof emulationstation); [ -n "$p" ] && awk -v t=$(up) '/VmRSS|VmHWM/{printf "%s %s %s\n", t, $1, $2}' /proc/$p/status 2>/dev/null; sleep 1; done > $R/mem.txt ) &
	local sampler=$!
	env "${env[@]}" ES_SOFT_STATS=0 ES_PERF_FRAMES=$R/perf.csv ES_STARTUP_TRACE=$R/trace.txt ES_SOFT_SCRIPT=$T/$script.script bash $E/es-mister.sh </dev/null >/dev/null 2>&1
	kill $sampler 2>/dev/null
	tail -n +$((before + 1)) $E/logs/es-mister.log | grep -v "MisterPads\|ERROR" > $R/script.txt
	[ $kind = boot ] && cp $H/es_systems.cfg $R/es_systems.cfg
	log "$name done ($(grep -c '^M' $R/perf.csv 2>/dev/null) marks)"
}

log "f3 start: $1"
mapfile -t PLAN < <(grep -v '^#' "$1" | grep .)
round=1
while :; do
	any=0
	for line in "${PLAN[@]}"; do
		set -- $line
		kind=$1 bin=$2 count=$3; shift 3
		[ $round -le $count ] || continue
		any=1
		run_one $kind $bin $round "$@"
	done
	[ $any = 1 ] || break
	round=$((round + 1))
done

cp $T/es-p6 $E/emulationstation
cp $B/bins/sh-orig $E/es-mister.sh
cp $B/bins/helper-orig $E/es-mister-helper
cp $B/bins/launch-orig $E/mister-launch.sh
cp $T/es_settings.cfg.orig $H/es_settings.cfg
clean; sleep 2
for try in 1 2; do
	setsid bash /media/fat/es-mister-720p/es-mister.sh </dev/null >/dev/null 2>&1 &
	for i in $(seq 40); do pidof emulationstation >/dev/null && break; sleep 1; done
	if pidof emulationstation >/dev/null; then log "user's ES up (try $try)"; break; fi
	log "user's ES not up (try $try): the menu core loaded again"
	clean; sleep 2
done
log "f3 end"
date +%T > $RES/fim
