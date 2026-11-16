"""Build the final report from accepted raw result files; no target I/O."""
import csv,json,pathlib,statistics
V=pathlib.Path(__file__).resolve().parent
D=V.parent/'es-mister/audit/performance-1080p'
def read(name):return json.loads((V/name).read_text())
def f(value):return f'{value:.3f}'
def table(head,rows):return '\n'.join(['| '+' | '.join(head)+' |','|'+'|'.join('---' for _ in head)+'|']+['| '+' | '.join(map(str,row))+' |' for row in rows])
def median(values):return statistics.median(values)
parts=['''# Closing the 1080p optimisation — 15/11/2026

This report is generated from the results of the final matrix. Fields without a completed
run appear as pending; they are not estimates of gain. Carbon's appearance,
filtering, transparencies, layer order, transition durations,
1920×1080 output, two drawing threads, STRIP=8 and PLD=256 were preserved.
FPS=60 is the configured ceiling, not the performance reached in full frames.

Rebuilt reference of the already optimised version: SHA-256
`1c29852e682db65a01590674986e00c937ee14a63dc44d053700ff4a325393a6`.
Combined candidate: SHA-256
`872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55`.
The binary installed at the start of the round was `d53703bb…abb64`.
The measurement markers are common to both builds. The historical improvement
against ES_SOFT_OPTIMIZATIONS=0 was not attributed to the candidate.

## Changes in the active sources

- RGBA→ARGB conversion with NEON, eight pixels at a time; keeps alpha, tails,
  overlap and channel order. Only MiSTer turns the new default on.
- A texture proven opaque during a fade: replaces only the redundant
  calculation div255(255×alpha) with the same alpha. Blending and the remaining roundings
  stay the same. Stencil, clipping and clearing of the previous selection preserved.
- A second prefetch for the second cache line of the 64-byte copy blocks,
  keeping PLD=256 and the order of the stores. It does not change the framebuffer's format.
- Catalogue limited to two threads on the MiSTer, instead of four; progress uses
  an atomic. TextureLoader uses one thread on the MiSTer; other platforms keep
  their defaults. ES_SYSTEM_THREADS and ES_TEXTURE_THREADS allow reproducing the A/B.
- Buffer counters published in atomics avoid waiting for the decoder's
  mutex. They cost 16 bytes per texture on ARM32, with no new surface. The 250 MiB
  RAM cleanup is not skipped just because Allocated is below MaxVRAM.
  They are counters of committed buffers; they are neither RSS nor a global budget.
- Shutdown of the paused loader and synchronisation of mExit fixed.
- Optional markers separate wrapper/helper/catalogue/first frame/input;
  the two-second sleep and the recovery attempts were kept.

## Historical script in the current configuration: combined comparison on the device
''']
navfile=V/'final-navigation-current-v1-summary.json'
if navfile.exists():
    nav=read(navfile.name);runs=nav['runs']
    parts.append(table(['Round','Responses','SDL median ms','p95','p99','Maximum','Full renderer median ms','RSS peak MiB'],[
        [r['run'],r['responded'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99','max')],f(r['full_renderer_ms']['median']),f(r['rss_peak_mib'])] for r in runs]))
    grouped={kind:[r for r in runs if r['implementation']==kind] for kind in ('reference','candidate')}
    a,b=[median(r['sdl_first_redraw_ms']['median'] for r in grouped[k]) for k in grouped]
    ar,br=[median(r['full_renderer_ms']['median'] for r in grouped[k]) for k in grouped]
    parts.append(f'\nABBA, 4,000/4,000 commands answered. Median of the SDL medians: **{f(a)} → {f(b)} ms ({f(100*(a-b)/a)}% reduction)**. Renderer: **{f(ar)} → {f(br)} ms ({f(100*(ar-br)/ar)}%)**. These are this session\'s incremental gains; the result of an old 150–160 ms log is not subtracted.\n')
    parts.append(table(['Metric; median of the two rounds','Reference','Candidate'],[[label,*[f(median(r['sdl_first_redraw_ms'][key] for r in grouped[k])) for k in grouped]] for label,key in [('p95 SDL ms','p95'),('p99 SDL ms','p99'),('SDL maximum per round ms','max')]]))
    worst=[max(r['sdl_first_redraw_ms']['max'] for r in grouped[k]) for k in grouped]
    parts.append(f'\n**Worst command observed:** {f(worst[0])} → {f(worst[1])} ms; the candidate made that maximum worse by {f(worst[1]-worst[0])} ms, despite the consistent improvement in median/p95/p99 in both rounds.\n')
    if runs[0].get('verified_selection_changes'):
        parts.append('\nBesides the responses, the 4,000 expected selection changes in the trace were checked. All versions started from cursor '+str(runs[0]['initial_system_cursor'])+' and kept the same order/counts of systems (19,284 games) and the same script hash. This current starting point does not replace the old opening scenario 12→13.\n')
    worst_file=V/'final-navigation-worst-events.json'
    if worst_file.exists():
        events=read(worst_file.name)
        parts.append(table(['Worst event per round','Command/key','Cursor','UI ms','Renderer ms','Diagnostics ms','Critical worker: drawing wall/CPU/off CPU ms'],[[r['run'],str(r['command'])+'/'+r['key'],r['system_cursor'],f(r['ui_ms']),f(r['renderer_ms']),f(r['diagnostics_ms']),'/'.join(f(r[k]) for k in ('critical_draw_wall_ms','critical_draw_cpu_ms','critical_draw_off_cpu_ms'))] for r in events]))
        parts.append('\nThe maximum of 230.190 ms happened at the system change of command 440/Left. The critical worker spent 184.757 ms drawing, but accounted for 62.492 ms of CPU: about 122.266 ms were off the CPU. That difference may include blocking/preemption; the specific cause was not identified. Diagnostics took 7.479 ms in that event. The whole maximum is not attributed to the alpha calculation, prefetch or governor without causal evidence.\n')
    parts.append('\nThe individual maxima and the regressions are shown in the table. Percentiles of the rounds\' medians are not percentiles of a pooled distribution. The first control keeps its original label final-navigation-v1-4-reference; it was not copied or renamed. The three following rounds complete ABBA in the same session.\n')
    if nav.get('inter_run_gaps_s'):
        parts.append('\nIntervals between the last response of one round and the first injection of the next, in seconds: '+', '.join(f(x) for x in nav['inter_run_gaps_s'])+'. These intervals are not counted as command latency.\n')
    stages=[('update_ns','Preparation/update ms'),('record_ns','UI recording ms'),('dirty_ns','Changed regions ms'),('draw_max_ms','Largest worker drawing ms'),('copy_max_ms','Largest worker copy ms'),('post_ns','Later maintenance ms'),('fps_limit_ns','UI limiter ms'),('pacing_ns','Renderer pacing ms'),('off_cpu_draw_max_ms','Largest share off the CPU during drawing ms')]
    parts.append('## Where the time remains\n\n'+table(['Stage: median over full frames',*[r['implementation']+str(i+1) for i,r in enumerate(runs)]],[[label,*[f(r['full_stages'][key]['median']) for r in runs]] for key,label in stages]))
    parts.append('''
The path measured is SDL queue → parseEvent → window.update → window.render
(recording the commands) → findParts → preparing/publishing the bands →
executePart/executeBand in the two workers → copy to the framebuffer → join and
maintenance. execute_ns measures the renderer's whole real interval with the copy.
One worker's drawing may overlap another's copy: independent maxima are not added
up to rebuild a frame. Stage medians also do not necessarily form
the same frame. The wait in the SDL queue includes the position of the historical
injection in the loop; it does not measure the Linux scheduler in isolation.
''')
    parts.append(table(['Round','Frames drawn','Above 16.7 ms','Full'],[[r['run'],r['drawn_frames'],r['drawn_above_16_7_ms'],r['full_frames']] for r in runs]))
    parts.append(f'\nWith the full renderer at about {f(br)} ms, reaching 16.7 ms would still require cutting about **{f(br-1000/60)} ms ({f(100*(1-(1000/60)/br))}% of the remainder)**. That explains the limit observed for this implementation; it does not prove an absolute physical limit of the hardware.\n')
else:parts.append('**Pending: final four-round comparison.**\n')
transitions=V/'final-transitions-summary.json'
if transitions.exists():
    data=read(transitions.name)['runs']
    parts.append('## End of the main transition\n\n'+table(['Round','Commands','End observed','Median to finish ms','p95','Median interval between drawings ms','Intervals >16.7 ms'],[[r['run'],r['commands'],r['completed'],f(r['main_transition_end_ms']['median']),f(r['main_transition_end_ms']['p95']),f(r['redraw_intervals_in_transition_ms']['median']),r['intervals_above_16_7_ms']] for r in data]))
    parts.append('\nMeasures the first frame written with the main SystemView/slot 0 animation inactive after an active sample, keeping the same cursor. It includes neither the informative fade of slot 2 nor the TV. A new key may interrupt the animation; a trace of redraws only may also omit a final logical state with no pixel change. No end observed does not mean a lost command. The durations above use a different subset in each version, favouring long intervals between commands; on their own they do not show a change in duration. The separate short scenario below avoids that censoring. Configured duration and effects were not changed.\n')
quiet=V/'final-animations-v1-transitions.json'
if quiet.exists():
    data=read(quiet.name)['runs']
    parts.append('### Transitions not interrupted by a new key\n\n'+table(['Round','Commands/ends observed','End median ms','p95','Maximum','Drawing interval median ms','Intervals >16.7 ms'],[[r['run'],str(r['commands'])+'/'+str(r['completed']),*[f(r['main_transition_end_ms'][k]) for k in ('median','p95','max')],f(r['redraw_intervals_in_transition_ms']['median']),r['intervals_above_16_7_ms']] for r in data]))
    parts.append('\n16 commands per round, ABBA, with 1,800 ms of additional wait and a trace of all the iterations. The final logical sample is available even if the pixels are already stable and need no new copy. The metric is the first inactive logical state after the active one; the TV was not measured. All the ends are required by the analyser. This scenario measures a complete transition and does not replace the 1,000-command percentiles of continuous navigation.\n')
oldfile=V/'final-navigation-v1-summary.json'
if oldfile.exists():
    old=read(oldfile.name)
    parts.append('## Interrupted comparison: kept as evidence\n\n'+table(['Round','SDL median ms','p95','p99','Maximum','Full renderer median ms'],[[r['run'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99','max')],f(r['full_renderer_ms']['median'])] for r in old['runs']]))
    parts.append('\nThe 4,000 responses are valid, but the last reference was run after a long follow-up pause. Intervals between rounds: '+', '.join(f(x) for x in old.get('inter_run_gaps_s',[]))+' s. The reference changed from 107.523 to 98.623 ms; pooling that interrupted session would give 103.073 against 104.576 ms and would show no gain. That is why a new continuous comparison was made, without hiding these numbers. The performance shift was not attributed to the governor or to an unmeasured cause.\n')
parts.append('''
## Cold covers, concurrency and memory

The same implementation during the isolated A/B of one/two picture threads,
1,000 entries sharing 160 original 512×768 PNGs, three cycles of
going in/out/carousel/list/menu and rest. All 3,888 inputs were answered,
with a single frontend. This grid has a different load from the historical carousel.
''')
covers=read('covers-final-v2-summary.json')['runs']
parts.append(table(['Round','Decoder','SDL median ms','p95','p99','Maximum','RSS peak MiB'],[[r['run'],r['texture_threads'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99','max')],f(r['rss_peak_mib'])] for r in covers]))
parts.append('''
Median of the rounds: 359.211 → 318.099 ms (11.4%); p99 of the rounds:
613.936 → 554.500 ms (9.7%). One thread produced fewer loads and discards,
although the queue could be longer: a shorter queue is not interpreted as an
automatically faster response. active_loads includes synchronous loads on the UI.

In a separate selected-cover test, detailed view and 24 visible steps
per round, the request→upload median was 551.805 → 554.459 ms;
p95 730.598 → 789.332 ms (**58.734 ms worse**). All the uploads happened before
the next step of about 910 ms. It is a software readiness proxy,
including UI/fade/decoding/upload; it is not latency to photons. The earlier quick
pilot mixed in the hidden list and is not valid for this conclusion.

The short comparison of the previous code with the final set, 52 keys per
round, measured 291.296 → 244.345 ms in the median of the rounds. p95 went
434.758 → 443.778 ms and p99 507.915 → 511.201 ms. They are small but
explicit regressions; with 52 samples p99 coincides with the maximum. The long test
above isolates concurrency; the short numbers do not replace its reach.

Mapping by key located the short candidate's tail in the
list→carousel exit (Escape, command 49): 537.349 and 485.053 ms, against
458.753 and 430.216 ms for the controls. In that action, the median of the two rounds
went 444.485 → 511.201 ms (**66.716 ms worse**); UI preparation in the
candidate took 294.998/308.006 ms and the renderer 241.950/176.767 ms.
That whole cost is not attributed to one routine without additional markers.
The results are in covers-tail-locations.json, without a new pass on the device.

In the long list, with 160 steps per direction and three exits per round, the median
of the medians of list→carousel was 1,169.500 → 1,303.411 ms (133.911 ms worse),
while the worst value observed for that action fell from 1,818.877 to 1,433.579 ms.
Scrolling improved, but the exit still contains a noticeable pause and is not described
as instant. There are six exits per policy, not enough to estimate a p99
specific to that action. The texture event buffer filled up before the first
exit in all four rounds; it does not identify which decode/upload caused that pause.
The per-action times are in covers-long-phases.json, and the censoring was checked
in covers-exit-observation.json. The one-worker policy favours scrolling and
reduces the worst stall observed, with this trade-off in the typical exit cost.

With minimal telemetry, without a frame/texture trace, the candidate had an RSS peak
sampled at 1 Hz of 192.184 MiB, VmHWM of 193.621 MiB and a late median RSS of
82.668 MiB in the fixture. The script ran 328
keys; the inputs CSV was off, so its zero counter does not represent
zero responses. Detailed tracing raised the peak to about 205–213 MiB.
The 4,096-position texture events filled up; aggregate counters and
memory samples continued. Repeating the same path may mean another
size, not necessarily a reload through eviction. Three cycles do not prove
the absence of leaks in a session of arbitrary length.

The animated grid at rest used about 147% of one logical CPU
(two CPUs available), both before and after. Keeping the animations has a cost;
that number does not represent a static screen nor prove 100% use of the hardware.
''')
parts.append('## Rest and controls\n')
for mode in ('idle','controls'):
    path=V/f'final-{mode}-v1-summary.json'
    if not path.exists():parts.append(f'**Pending: {mode}.**\n');continue
    data=read(path.name)['runs']
    if mode=='idle':
        parts.append(table(['Round; 15 s of rest','Responses','SDL median ms','p95','p99','uinput median ms','Static CPU % of one core','Static RSS MiB'],[[r['run'],r['responses'],*[f(r['sdl_first_redraw_ms'][k]) for k in ('median','p95','p99')],f(r['uinput_first_redraw_ms']['median']),f(r['static_idle_cpu_percent_one_core_units']['median']),f(r['static_idle_rss_mib']['median'])] for r in data]))
        if all('first_response_renderer_ms' in r for r in data):parts.append('\n'+table(['First response after rest','Renderer median ms','Partial/total responses'],[[r['run'],f(r['first_response_renderer_ms']['median']),str(r['first_response_partial_frames'])+'/'+str(r['responses'])] for r in data]))
        parts.append('\nEight inputs per round: a small p99 coincides with the maximum. Rest alternates two adjacent systems and may answer with a partial drawing; it is not compared directly with the continuous path of four positions per side. uinput includes evdev/SDL, without physical USB/radio/TV.\n')
    else:parts.append(table(['Round','Short touch','Advances holding Right/Left','Stopped on release'],[[r['run'],r['short_advance'],str(r['held_right_advance'])+'/'+str(r['held_left_advance']),r['release_stable']] for r in data]))
parts.append('## Full opening\n\nThe wrapper is the active source, in an isolated package with the same assets/configuration; injection starts after the first interactive frame and checks for a selection change. Opening through an SSH call does not prove the closing time of the physical Scripts menu.\n')
for mode in ('warm','cold'):
    paths=sorted((V/'target-results').glob(f'startup-full-{mode}-v1-*/startup-summary.json'))
    if len(paths)!=4:parts.append(f'**Pending: full {mode} opening, {len(paths)}/4 rounds analysed.**\n');continue
    data=[json.loads(p.read_text()) for p in paths]
    parts.append(f'### {mode}: '+('warm cache' if mode=='warm' else 'first opening after a real reboot')+'\n\n'+table(['Round','Wrapper→input ms','Process→input ms','Helper ms','Fixed sleep ms','Load systems s','First interactive frame s','Selection'],[[r['run'],f(r['wrapper_to_first_input_ms']),f(r['spawn_to_first_input_ms']),f(r['helper_ms']),f(r['wrapper_fixed_wait_ms']),f(r['load_systems_s']),f(r['first_interactive_s']),str(r['selection_before'])+'→'+str(r['selection_after'])] for r in data]))
    parts.append('\n'+table(['Median of the two rounds','Reference','Candidate'],[[label,*[f(median(r[key] for r in data if ('candidate' in r['run'])==candidate)) for candidate in (False,True)]] for label,key in [('Wrapper→first input ms','wrapper_to_first_input_ms'),('Process→first input ms','spawn_to_first_input_ms')]]))
    parts.append('\n'+table(['First input after the first screen; median of the two rounds','Reference','Candidate'],[[label,*[f(median(r[key] for r in data if ('candidate' in r['run'])==candidate)) for candidate in (False,True)]] for label,key in [('uinput injection→redraw ms','uinput_ms'),('SDL queue→redraw ms','sdl_input_ms'),('First interactive frame from the process s','first_interactive_s'),('LoadConfig s','load_systems_s')]]))
    parts.append('\nBringing the first screen forward was checked together with the first input actually processed. The cost of that initial navigation is included in the wrapper→input time and also appears separately in the table; the gains are not added up. Two runs per version, with no guarantee of an opening p99.\n')
    if mode=='cold':parts.append('\nA new boot_id was required per round; no hash/pre-read of the ELF before starting the process. The original services stayed active and were given the same stabilisation interval. The reboot is not drop_caches.\n')
parts.append('''
## Quality, stability and reach of the tests

Host with ASan/UBSan/LSan and ARM: 112 rasterisation scenes against independent
previous code, 7,027 RGBA conversion cases and 14 backend integration
scenes; 64 loader life cycles including pause/shutdown and the queue.
ARM32 reproduced the previous RAM cleanup failure and confirmed the fix,
also when Allocated exceeded 250 MiB but RAM stayed below the threshold.
The GLES3 accounting branch built; it was not validated by running on another
platform. The coverage is not equivalent to testing every existing theme.

In the ARM application, captures of carousel/list/selection/menu/transition in equal
logical states had pixel equality, including 18/18 identical SHA-256 in the
animation pass and zero partial/full divergences. The quality
pass uses a fixed logical clock only in its own build; it is not the distributed
executable nor part of the performance measurements. The additional copy had all
2,073,600 pixels of the framebuffer checked after each micro-trial.

The freeze reported by the user was investigated: the OSD was still alive,
SSH came back, and dmesg confirmed an OOM after the bench overlapped two frontends.
It was a synchronisation error in the bench's restore, fixed and validated in
two regressions before resuming the tests. The invalid round is in
no table. It was not hidden as a governor or hardware problem.
''')
core=V/'target-results/final-core-v1/lifecycle-summary.json'
if core.exists():
    result=json.loads(core.read_text())
    parts.append('\nReturn from a core, quality pass: '+str(result['quality_checked_frames'])+' frames checked, '+str(result['quality_wrong_frames'])+' divergences. The connection failed before sending MENU; NES and PID were checked before recovering the return. The core stayed active for about 157 s, instead of the eight planned. That interval and the double checking are not treated as normal performance. The first input count did not show useful actions; this was fixed in the next pass by explicitly checking the state/selection.\n')
speed=V/'target-results/final-core-speed-v2/lifecycle-summary.json'
if speed.exists():
    r=json.loads(speed.read_text())
    parts.append('### Normal return with the original test program\n\n'+table(['Stage measured on the MiSTer','Time ms'],[['Fixed wait after MENU observed by the launcher',f(r['return_fixed_wait_ms'])],['Framebuffer activation by the helper',f(r['return_helper_ms'])],['Helper finished→first drawing',f(r['helper_complete_to_first_draw_ms'])],['MENU observed by the launcher→first drawing',f(r['menu_observed_to_first_draw_ms'])]]))
    parts.append('\nDouble checking, PNG, slow dumps and profiler off; the same candidate binary at 1080p. This test\'s catalogue is minimal, with an original NES program without save RAM: it is not a before/after gain measurement on the real catalogue. There was a single core opening. After coming back, five probes confirmed the carousel active and the selection 0→1→0 with the arrows.\n\nThe script deliberately delayed those controls and waited for a rest condition that the animated grid did not reach. The follow-up timed out before the script, but the autonomous run finished and was collected with the expected states. The first drawing above was measured before that wait; the late timestamp of the controls does not estimate the shortest input readiness. The reproduction script was fixed to use the safety wait and selection probes, without requiring an animated grid to stop redrawing. The shortest input readiness after returning remains unmeasured.\n\nThe attempt final-core-speed-v1 was invalidated: a Return sent before confirming the screen reopened the test program itself. The return was recovered after checking NES/PID, without another frontend; its numbers are not part of the acceptance. The artefacts were kept.\n')
else:parts.append('\n**Pending: normal return from a core with confirmed controls.**\n')
layerfile=V/'profile-layer-costs.json'
if layerfile.exists():
    layers=read(layerfile.name)
    parts.append('## Layers: separate detailed profile\n\n'+table(['Group by dimensions/vertices','Command/worker samples','Visits in bands','Sum of the workers\' wall time ms','Share of that sum %'],[[r['label'],r['command_worker_samples'],r['band_visits'],f(r['sum_worker_wall_ms']),f(r['share_sum_worker_wall_percent'])] for r in layers['groups']]))
    parts.append('\nThis earlier profile samples selected frames. Large pictures add up to about 60.9%; the atlas/text/geometry group, 12.2%. The sum of the two workers is not the frame\'s real time, may contain pauses off the CPU and includes the cost of the detailed markers. First alpha does not prove the texture\'s opacity. These groups allow prioritising compositing and pictures, but not attributing an exact semantic cost to each background/cover/overlay without mapping the asset. They were not added to the times of the fast tests.\n')
parts.append('''
## Why this round ends here

The last two renderer hypotheses were closed: the copy's second prefetch
had a small reproduced gain; the NEON search for alpha boundaries was
rejected because the component gain did not turn into a reproducible improvement in
opening. Horizontal subdivision, quad merging, packed blending and cold NEON
sampling were also rejected. Enlarging the cache from 40 to 48 MiB did not remove
additional misses; the reusable layer prefix was zero in the 41 full
frames investigated. A 1080p framebuffer costs 7.91 MiB, besides the cost of
building, invalidating and copying it. No full-screen cache was created without
demonstrated stable content.

The separate detailed profile concentrates cost in large pictures and compositing
with transparency; text still visits many bands. Dimensions/first alpha
identify groups, not the semantic name of each asset. The sum
of the two workers' times is not confused with the frame's wall time, nor is serial dump
time used as if it were the parallel renderer. The reference and the candidate still
need to read/sample/composite part of the same pixels several times.

Linux 6.18.38-MiSTer exposes two Cortex-A9/NEON, 491.08 MiB of RAM and no
swap. It exposes no CPUFreq policies/driver/governor/adjustable frequencies;
the performance A/B is **unmeasured for lack of control**. The opt-in overclock
module was not loaded, nor were the kernel or VM changed permanently.
CPU/PMU/DDR were not measured to the point of proving physical saturation; the PMU exists
in sysfs, but no cycles/instructions/misses were collected in this matrix.
Some maxima include time off the CPU whose exact cause remains uncertain.
See LINUX.md and LIMITS.md.

Another architecture could study an accelerated compositor, reuse with
more sophisticated invalidation or a presentation that avoids copies, but that requires
its own design and validation. It is neither a gain already shown nor a promise
of 16.7 ms while keeping exactly the same appearance. The FPGA currently does not run a
UI compositor for this frontend.

## Reproduction and explicit pending items

The exact commands and conditions are in REPRODUCE.md and in the
controllers in validation/. The per-round JSONs keep hashes, effective parameters and
samples; host measurements are identified separately from those on the MiSTer.
Physical controller→TV latency was not measured, nor the real path through the
Scripts menu. No measurement of the wrapper over SSH replaces those checks.
The shortest input time after returning from a core was also not estimated by the
functional script with a deliberate wait. The first inputs of the initial
warm opening/after reboot were measured with early injection and a real selection.
''')
deploy=V/'deployment-final-20261115.json'
parts.append('\n## Installation\n\n'+('Verified record: `'+deploy.name+'`.\n\n```json\n'+deploy.read_text()+'\n```\n\n(`LEIA-ME.txt` is the name the notes file had in the installed package; the record is kept as it was taken.)\n' if deploy.exists() else '**Not installed yet: waiting for the matrix to be completed and reviewed.**\n'))
verification=V/'installed-final-verification.json'
if verification.exists():
    live=read(verification.name)
    parts.append('\nCheck of the normal run: PID '+str(live['pid'])+', one frontend and one wrapper, first interactive frame confirmed, 1920×1080×32 bits, Carbon `'+live['theme_settings'].get('ThemeSet','')+'`, STRIP=8/PLD=256/FPS=60. Frame/texture trace, double checking, PNG/script and profiler off. The installed SHA-256 matches the candidate measured. OOM in this boot: '+str(live['oom_kill'])+'; CPUFreq policies: '+str(live['cpufreq_policies'])+'. The previous copy and its hashes are in the backup above.\n')
normal=V/'normal-runtime-sample.json'
if normal.exists():
    r=read(normal.name)
    parts.append('\nObservation of the installed run without script/trace, for '+f(r['duration_s'])+' s: RSS '+f(r['rss_min_mib'])+'–'+f(r['rss_max_mib'])+' MiB, VmHWM '+f(r['hwm_mib'])+' MiB and CPU '+f(r['cpu_percent_one_core_units'])+'% of one core. The user tried the controller during that period; it is not controlled rest, a before/after CPU comparison or proof of unlimited stability. The rest benchmark above used an active script and must not be confused with this normal observation.\n')
physical=V/'physical-validation.json'
if physical.exists():
    r=read(physical.name)
    parts.append('\nPhysical validation reported by the user on 15/11: **controller working normally on the TV**. Frontend audio: **no sound, as before**, no regression reported. This confirms observed operation; it does not measure controller→photons. Game audio was not validated by the silent video program.\n')
D.mkdir(parents=True,exist_ok=True);(D/'CLOSING.md').write_text('\n\n'.join(parts),encoding='utf-8')
print(D/'CLOSING.md')
