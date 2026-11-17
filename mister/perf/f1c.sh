#!/bin/bash
# Phase 1, third batch: what f1.sh/f1b.sh lost. The 7 detailed-list runs whose lines es-perf overwrote at exit
# (es-perf2 now), the second batch's golden frames and profiles (they found the user's ES still starting:
# "already running"), and one cover's cost with ES closed (f1img). Each run from a clean state: no ES, no
# es-mister.sh, the menu core loaded again. At the end the test copy as it was and the user's ES started and
# checked (the menu core loaded again and a second try if it is not up).
T=/media/fat/teste-claude/f1; E=/media/fat/emulationstation-claude720p; H=$E/home/.emulationstation
log() { echo "$(date +%T) $*" >> $T/res/batch.log; }
menu_wait() { for i in $(seq 60); do [ "$(cat /tmp/CORENAME 2>/dev/null)" = MENU ] && return 0; sleep 1; done; return 1; }

# no ES and no es-mister.sh: ES closed (its script loads the menu core again), or a script still starting
# stopped and the menu core loaded again (a fresh Main_MiSTer, F9 as after the Scripts menu)
clean() {
	killall emulationstation 2>/dev/null
	for i in $(seq 30); do
		pidof emulationstation >/dev/null || break
		sleep 1
	done
	pidof emulationstation >/dev/null && { killall -9 emulationstation; sleep 1; }
	for i in $(seq 15); do
		[ -f /tmp/es-mister.pid ] || break
		sleep 1
	done
	if [ -f /tmp/es-mister.pid ]; then
		p=$(cat /tmp/es-mister.pid)
		kill "$p" 2>/dev/null
		sleep 1
		rm -f /tmp/es-mister.pid
		log "an es-mister.sh still starting ($p) stopped"
		echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd
		sleep 3
	fi
	menu_wait
}

run_es() {
	local name=$1 script=$2 view=$3; shift 3
	local R=$T/res/$name; rm -rf $R; mkdir -p $R
	clean
	cp $T/es_settings.cfg.orig $H/es_settings.cfg
	sed -i -e 's/name="LastSystem" value="[^"]*"/name="LastSystem" value="snes"/' $H/es_settings.cfg
	sed -i "s|</config>|	<string name=\"snes.defaultView\" value=\"$view\" />\n</config>|" $H/es_settings.cfg
	sleep 2
	local before=$(wc -l < $E/logs/es-mister.log)
	( while :; do p=$(pidof emulationstation); [ -n "$p" ] && awk -v t=$(cut -d' ' -f1 /proc/uptime) '/VmRSS|VmHWM/{printf "%s %s %s\n", t, $1, $2}' /proc/$p/status 2>/dev/null; sleep 1; done > $R/mem.txt ) &
	local sampler=$!
	env "$@" ES_SOFT_STATS=0 ES_PERF_FRAMES=$R/perf.csv ES_STARTUP_TRACE=$R/trace.txt ES_SOFT_SCRIPT=$T/$script.script bash $E/es-mister.sh </dev/null >/dev/null 2>&1
	kill $sampler 2>/dev/null
	tail -n +$((before + 1)) $E/logs/es-mister.log | grep -v "MisterPads\|ERROR" > $R/script.txt
	log "$name done ($(grep -c '^M' $R/perf.csv 2>/dev/null) marks)"
}

log "f1c start"
clean
cp $T/es-perf2 $E/emulationstation

for n in 1 2 3 4 5 6 9; do run_es nav-list-$n nav detailed; done
for n in 1 2; do run_es gold2-grid-$n golden grid ES_PERF_FIXED_STEP=1 ES_SOFT_SHOTS=$T/res/gold2-grid-$n; done
run_es prof-scroll nav grid ES_PROFILE=$T/res/prof-scroll/profile.txt ES_PROFILE_FROM=60
run_es prof-idle-systems idle-systems grid ES_PROFILE=$T/res/prof-idle-systems/profile.txt ES_PROFILE_FROM=9
run_es prof-idle-list idle-list grid ES_PROFILE=$T/res/prof-idle-list/profile.txt ES_PROFILE_FROM=16

# one cover's cost, ES closed: the first 40 SNES covers at the grid's tile size (219x156), page cache emptied
clean
ls /media/fat/games/SNES/images/*.png | head -40 > $T/res/f1img-files.txt
sync; echo 3 > /proc/sys/vm/drop_caches; sleep 1
tr '\n' '\0' < $T/res/f1img-files.txt | xargs -0 $T/f1img 219 156 > $T/res/f1img-cold.txt 2>&1
tr '\n' '\0' < $T/res/f1img-files.txt | xargs -0 $T/f1img 219 156 > $T/res/f1img-warm.txt 2>&1
log "f1img done"

# the test copy as it was, and the user's ES: started, then checked
cp $T/es-p6 $E/emulationstation
cp $T/es_settings.cfg.orig $H/es_settings.cfg
clean; sleep 2
for try in 1 2; do
	setsid bash /media/fat/es-mister-720p/es-mister.sh </dev/null >/dev/null 2>&1 &
	for i in $(seq 40); do pidof emulationstation >/dev/null && break; sleep 1; done
	if pidof emulationstation >/dev/null; then log "user's ES up (try $try)"; break; fi
	log "user's ES not up (try $try): the menu core loaded again"
	clean; sleep 2
done
log "f1c end"
date +%T > $T/res/end3
