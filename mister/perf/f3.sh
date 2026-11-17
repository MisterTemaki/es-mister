#!/bin/bash
# Phase 3 batch: binaries compared in the same session, interleaved, each run from a clean state.
#   f3.sh PLAN_FILE
# Each PLAN_FILE line: KIND BIN COUNT [VAR=VALUE...]
#   KIND: grid | list (nav.script in that view), ggrid | glist (golden frames, fixed clock), boot (cold boot,
#         page cache emptied, abrir.script)
#   BIN:  a binary in $T/bins (es-NAME); its es-mister.sh from $T/bins/sh-NAME if there is one (scripts tested
#         the same way), else the test copy's own
# Lines are taken round-robin: one run of each line, then the next round, until each line has its COUNT.
# At the end the test copy as it was (es-p6 and its es-mister.sh) and the user's ES started and checked.
T=/media/fat/teste-claude/f1; B=/media/fat/teste-claude/f3; E=/media/fat/emulationstation-claude720p; H=$E/home/.emulationstation
RES=$B/res; mkdir -p $RES
log() { echo "$(date +%T) $*" >> $RES/batch.log; }
menu_wait() { for i in $(seq 60); do [ "$(cat /tmp/CORENAME 2>/dev/null)" = MENU ] && return 0; sleep 1; done; return 1; }

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

run_one() {
	local kind=$1 bin=$2 n=$3; shift 3
	local name=$kind-$bin-$n R=$RES/$kind-$bin-$n view=grid script=nav extra=()
	case $kind in
		list) view=detailed ;;
		ggrid) script=golden2; extra=(ES_PERF_FIXED_STEP=1 ES_SOFT_SHOTS=$R) ;;
		glist) view=detailed; script=golden2; extra=(ES_PERF_FIXED_STEP=1 ES_SOFT_SHOTS=$R) ;;
		boot) script=abrir ;;
	esac
	rm -rf $R; mkdir -p $R
	clean
	cp $B/bins/es-$bin $E/emulationstation
	if [ -f $B/bins/sh-$bin ]; then cp $B/bins/sh-$bin $E/es-mister.sh; else cp $T/es-mister.sh $E/es-mister.sh; fi
	cp $T/es_settings.cfg.orig $H/es_settings.cfg
	sed -i -e 's/name="LastSystem" value="[^"]*"/name="LastSystem" value="snes"/' $H/es_settings.cfg
	sed -i "s|</config>|	<string name=\"snes.defaultView\" value=\"$view\" />\n</config>|" $H/es_settings.cfg
	[ $kind = boot ] && { sync; echo 3 > /proc/sys/vm/drop_caches; }
	sleep 2
	local before=$(wc -l < $E/logs/es-mister.log)
	( while :; do p=$(pidof emulationstation); [ -n "$p" ] && awk -v t=$(cut -d' ' -f1 /proc/uptime) '/VmRSS|VmHWM/{printf "%s %s %s\n", t, $1, $2}' /proc/$p/status 2>/dev/null; sleep 1; done > $R/mem.txt ) &
	local sampler=$!
	env "${extra[@]}" "$@" ES_SOFT_STATS=0 ES_PERF_FRAMES=$R/perf.csv ES_STARTUP_TRACE=$R/trace.txt ES_SOFT_SCRIPT=$T/$script.script bash $E/es-mister.sh </dev/null >/dev/null 2>&1
	kill $sampler 2>/dev/null
	tail -n +$((before + 1)) $E/logs/es-mister.log | grep -v "MisterPads\|ERROR" > $R/script.txt
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
cp $T/es-mister.sh $E/es-mister.sh
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
