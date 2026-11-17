#!/bin/bash
# Phase 1 baseline batch, on the test copy (emulationstation-claude720p) with the measurement build (es-perf):
# golden frames (fixed step, 2 runs a view), microbenchmarks, 10 cold boots, 30 navigation runs in the SNES grid,
# 10 in the detailed list, 10 core launches. Results in /media/fat/teste-claude/f1/<run>. At the end the test copy
# gets its normal binary back and the user's ES (es-mister-720p) starts again.
T=/media/fat/teste-claude/f1; E=/media/fat/emulationstation-claude720p; H=$E/home/.emulationstation
mkdir -p $T/res
cp -n $H/es_settings.cfg $T/es_settings.cfg.orig
log() { echo "$(date +%T) $*" >> $T/res/batch.log; }

menu_wait() { for i in $(seq 60); do [ "$(cat /tmp/CORENAME 2>/dev/null)" = MENU ] && return 0; sleep 1; done; return 1; }
es_gone() { killall emulationstation 2>/dev/null; for i in $(seq 30); do pidof emulationstation >/dev/null || return 0; sleep 1; done; killall -9 emulationstation; sleep 1; }

# run_es NAME SCRIPT VIEW [VAR=VALUE...]: ES started as the Scripts menu starts it (es-mister.sh), the script run
run_es() {
	local name=$1 script=$2 view=$3; shift 3
	local R=$T/res/$name; mkdir -p $R
	es_gone; menu_wait
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
	cp $E/logs/es-mister-stats.txt $R/stats.txt 2>/dev/null
	log "$name done"
}

log "start; uinput: $(ls /dev/uinput 2>&1)"
es_gone; menu_wait
cp $T/es-perf $E/emulationstation

# golden frames: twice a view, the same frames expected
for v in grid detailed; do for n in 1 2; do run_es gold-$v-$n golden $v ES_PERF_FIXED_STEP=1 ES_SOFT_SHOTS=$T/res/gold-$v-$n; done; done

# microbenchmarks (ES closed)
es_gone
big=$(find /media/fat/games/PSX -type f -size +300M 2>/dev/null | head -1)
sync; echo 3 > /proc/sys/vm/drop_caches; sleep 1
$T/f1bench "$big" /media/fat/games/SNES > $T/res/bench-cold.txt 2>&1
$T/f1bench "$big" /media/fat/games/SNES > $T/res/bench-warm.txt 2>&1
log "bench done"

# cold boots: the page cache emptied first, as after the MiSTer starts
for n in $(seq 1 10); do es_gone; menu_wait; sync; echo 3 > /proc/sys/vm/drop_caches; sleep 1; run_es cold-$n start grid; done

# navigation: 30 in the grid, 10 in the detailed list (each a warm boot too)
for n in $(seq 1 30); do run_es nav-grid-$n nav grid; done
for n in $(seq 1 10); do run_es nav-list-$n nav detailed; done

# core launches (as ES launches a game: mister-launch.sh), and back to the menu
es_gone; menu_wait
game="/media/fat/games/SNES/Aero the Acro-Bat 2 (EU).sfc"
for n in $(seq 1 10); do
	menu_wait; sleep 2
	s=$(cut -d' ' -f1 /proc/uptime)
	bash $E/mister-launch.sh snes "$game" </dev/null >/dev/null 2>&1 &
	lp=$!
	c=""; for i in $(seq 1500); do x=$(cat /tmp/CORENAME 2>/dev/null); [ -n "$x" ] && [ "$x" != MENU ] && { c=$(cut -d' ' -f1 /proc/uptime); break; }; sleep 0.02; done
	sleep 5
	m=$(cut -d' ' -f1 /proc/uptime); echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd
	b=""; for i in $(seq 1500); do [ "$(cat /tmp/CORENAME 2>/dev/null)" = MENU ] && { b=$(cut -d' ' -f1 /proc/uptime); break; }; sleep 0.02; done
	wait $lp; d=$(cut -d' ' -f1 /proc/uptime)
	echo "launch $n start $s core $c menu-asked $m menu $b script-done $d" >> $T/res/launch.txt
done
log "launches done"

# the test copy as it was, and the user's ES again
cp $T/es-p6 $E/emulationstation
cp $T/es_settings.cfg.orig $H/es_settings.cfg
log "end"
date +%T > $T/res/end
menu_wait; sleep 2
setsid bash /media/fat/es-mister-720p/es-mister.sh </dev/null >/dev/null 2>&1 &
