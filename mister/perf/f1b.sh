#!/bin/bash
# Phase 1, second batch (after f1.sh): golden frames with the menu, options and search (2 runs), the memory
# bench again (memcpy kept), profiles (grid scrolling; idle systems; idle grid). The measurement build es-perf2
# (its lines no longer overwritten at exit). The test copy and the user's ES as they were at the end.
T=/media/fat/teste-claude/f1; E=/media/fat/emulationstation-claude720p; H=$E/home/.emulationstation
log() { echo "$(date +%T) $*" >> $T/res/batch.log; }
menu_wait() { for i in $(seq 60); do [ "$(cat /tmp/CORENAME 2>/dev/null)" = MENU ] && return 0; sleep 1; done; return 1; }
es_gone() { killall emulationstation 2>/dev/null; for i in $(seq 30); do pidof emulationstation >/dev/null || return 0; sleep 1; done; killall -9 emulationstation; sleep 1; }
run_es() {
	local name=$1 script=$2 view=$3; shift 3
	local R=$T/res/$name; mkdir -p $R
	es_gone; menu_wait
	cp $T/es_settings.cfg.orig $H/es_settings.cfg
	sed -i -e 's/name="LastSystem" value="[^"]*"/name="LastSystem" value="snes"/' $H/es_settings.cfg
	sed -i "s|</config>|	<string name=\"snes.defaultView\" value=\"$view\" />\n</config>|" $H/es_settings.cfg
	sleep 2
	local before=$(wc -l < $E/logs/es-mister.log)
	env "$@" ES_SOFT_STATS=0 ES_PERF_FRAMES=$R/perf.csv ES_STARTUP_TRACE=$R/trace.txt ES_SOFT_SCRIPT=$T/$script.script bash $E/es-mister.sh </dev/null >/dev/null 2>&1
	tail -n +$((before + 1)) $E/logs/es-mister.log | grep -v "MisterPads\|ERROR" > $R/script.txt
	log "$name done"
}
while [ ! -f $T/res/fim ]; do sleep 10; done
sleep 20
log "f1b start"
es_gone; menu_wait
cp $T/es-perf2 $E/emulationstation
for n in 1 2; do run_es gold2-grid-$n golden2 grid ES_PERF_FIXED_STEP=1 ES_SOFT_SHOTS=$T/res/gold2-grid-$n; done
es_gone
big=$(find /media/fat/games/PSX -type f -size +300M 2>/dev/null | head -1)
$T/f1bench2 "$big" /media/fat/games/SNES > $T/res/bench2.txt 2>&1
run_es prof-rolagem nav grid ES_PROFILE=$T/res/prof-rolagem/profile.txt ES_PROFILE_FROM=60
run_es prof-ocioso-sis ocioso-sis grid ES_PROFILE=$T/res/prof-ocioso-sis/profile.txt ES_PROFILE_FROM=9
run_es prof-ocioso-lista ocioso-lista grid ES_PROFILE=$T/res/prof-ocioso-lista/profile.txt ES_PROFILE_FROM=16
cp $T/es-p6 $E/emulationstation
cp $T/es_settings.cfg.orig $H/es_settings.cfg
log "f1b end"
date +%T > $T/res/fim2
es_gone; menu_wait; sleep 2
setsid bash /media/fat/es-mister-720p/es-mister.sh </dev/null >/dev/null 2>&1 &
