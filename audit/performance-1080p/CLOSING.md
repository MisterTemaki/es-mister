# Closing the 1080p optimisation — 15/11/2026

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


| Round | Responses | SDL median ms | p95 | p99 | Maximum | Full renderer median ms | RSS peak MiB |
|---|---|---|---|---|---|---|---|
| final-navigation-v1-4-reference | 1000 | 98.623 | 138.930 | 167.918 | 209.086 | 81.858 | 163.539 |
| final-navigation-current-v1-2-candidate | 1000 | 95.306 | 129.970 | 151.030 | 230.190 | 77.903 | 163.672 |
| final-navigation-current-v1-3-candidate | 1000 | 95.695 | 130.329 | 153.039 | 167.948 | 78.214 | 163.680 |
| final-navigation-current-v1-4-reference | 1000 | 97.977 | 139.279 | 181.858 | 210.078 | 81.536 | 163.691 |


ABBA, 4,000/4,000 commands answered. Median of the SDL medians: **98.300 → 95.500 ms (2.848% reduction)**. Renderer: **81.697 → 78.059 ms (4.454%)**. These are this session's incremental gains; the result of an old 150–160 ms log is not subtracted.


| Metric; median of the two rounds | Reference | Candidate |
|---|---|---|
| p95 SDL ms | 139.105 | 130.149 |
| p99 SDL ms | 174.888 | 152.034 |
| SDL maximum per round ms | 209.582 | 199.069 |


**Worst command observed:** 210.078 → 230.190 ms; the candidate made that maximum worse by 20.112 ms, despite the consistent improvement in median/p95/p99 in both rounds.



Besides the responses, the 4,000 expected selection changes in the trace were checked. All versions started from cursor 4 and kept the same order/counts of systems (19,284 games) and the same script hash. This current starting point does not replace the old opening scenario 12→13.


| Worst event per round | Command/key | Cursor | UI ms | Renderer ms | Diagnostics ms | Critical worker: drawing wall/CPU/off CPU ms |
|---|---|---|---|---|---|---|
| final-navigation-v1-4-reference | 2/Right | 6 | 89.634 | 103.369 | 0.035 | 94.597/50.496/44.101 |
| final-navigation-current-v1-2-candidate | 440/Left | 4 | 6.627 | 207.325 | 7.479 | 184.757/62.492/122.266 |
| final-navigation-current-v1-3-candidate | 389/Left | 7 | 5.775 | 146.154 | 0.018 | 128.090/52.527/75.563 |
| final-navigation-current-v1-4-reference | 942/Left | 6 | 8.810 | 186.240 | 4.324 | 170.559/55.361/115.199 |


The maximum of 230.190 ms happened at the system change of command 440/Left. The critical worker spent 184.757 ms drawing, but accounted for 62.492 ms of CPU: about 122.266 ms were off the CPU. That difference may include blocking/preemption; the specific cause was not identified. Diagnostics took 7.479 ms in that event. The whole maximum is not attributed to the alpha calculation, prefetch or governor without causal evidence.



The individual maxima and the regressions are shown in the table. Percentiles of the rounds' medians are not percentiles of a pooled distribution. The first control keeps its original label final-navigation-v1-4-reference; it was not copied or renamed. The three following rounds complete ABBA in the same session.



Intervals between the last response of one round and the first injection of the next, in seconds: 232.953, 55.280, 61.232. These intervals are not counted as command latency.


## Where the time remains

| Stage: median over full frames | reference1 | candidate2 | candidate3 | reference4 |
|---|---|---|---|---|
| Preparation/update ms | 0.468 | 0.462 | 0.467 | 0.469 |
| UI recording ms | 1.780 | 2.220 | 2.235 | 1.761 |
| Changed regions ms | 0.104 | 0.104 | 0.104 | 0.104 |
| Largest worker drawing ms | 73.802 | 70.936 | 71.396 | 73.405 |
| Largest worker copy ms | 9.637 | 8.803 | 8.692 | 9.580 |
| Later maintenance ms | 0.010 | 0.009 | 0.009 | 0.010 |
| UI limiter ms | 0.003 | 0.003 | 0.003 | 0.003 |
| Renderer pacing ms | 0.003 | 0.003 | 0.003 | 0.003 |
| Largest share off the CPU during drawing ms | 2.407 | 2.387 | 2.379 | 2.390 |


The path measured is SDL queue → parseEvent → window.update → window.render
(recording the commands) → findParts → preparing/publishing the bands →
executePart/executeBand in the two workers → copy to the framebuffer → join and
maintenance. execute_ns measures the renderer's whole real interval with the copy.
One worker's drawing may overlap another's copy: independent maxima are not added
up to rebuild a frame. Stage medians also do not necessarily form
the same frame. The wait in the SDL queue includes the position of the historical
injection in the loop; it does not measure the Linux scheduler in isolation.


| Round | Frames drawn | Above 16.7 ms | Full |
|---|---|---|---|
| final-navigation-v1-4-reference | 5902 | 5828 | 5823 |
| final-navigation-current-v1-2-candidate | 5968 | 5914 | 5909 |
| final-navigation-current-v1-3-candidate | 5970 | 5914 | 5910 |
| final-navigation-current-v1-4-reference | 5914 | 5832 | 5829 |


With the full renderer at about 78.059 ms, reaching 16.7 ms would still require cutting about **61.392 ms (78.648% of the remainder)**. That explains the limit observed for this implementation; it does not prove an absolute physical limit of the hardware.


## End of the main transition

| Round | Commands | End observed | Median to finish ms | p95 | Median interval between drawings ms | Intervals >16.7 ms |
|---|---|---|---|---|---|---|
| final-navigation-v1-4-reference | 1000 | 68 | 769.443 | 835.429 | 85.495 | 4891 |
| final-navigation-current-v1-2-candidate | 1000 | 47 | 798.715 | 823.175 | 81.785 | 4956 |
| final-navigation-current-v1-3-candidate | 1000 | 48 | 800.000 | 817.583 | 82.065 | 4958 |
| final-navigation-current-v1-4-reference | 1000 | 67 | 792.550 | 818.839 | 85.121 | 4896 |


Measures the first frame written with the main SystemView/slot 0 animation inactive after an active sample, keeping the same cursor. It includes neither the informative fade of slot 2 nor the TV. A new key may interrupt the animation; a trace of redraws only may also omit a final logical state with no pixel change. No end observed does not mean a lost command. The durations above use a different subset in each version, favouring long intervals between commands; on their own they do not show a change in duration. The separate short scenario below avoids that censoring. Configured duration and effects were not changed.


### Transitions not interrupted by a new key

| Round | Commands/ends observed | End median ms | p95 | Maximum | Drawing interval median ms | Intervals >16.7 ms |
|---|---|---|---|---|---|---|
| final-animations-v1-1-reference | 16/16 | 701.904 | 801.834 | 801.834 | 84.334 | 74 |
| final-animations-v1-2-candidate | 16/16 | 700.959 | 831.988 | 831.988 | 83.229 | 77 |
| final-animations-v1-3-candidate | 16/16 | 686.631 | 797.632 | 797.632 | 82.351 | 77 |
| final-animations-v1-4-reference | 16/16 | 703.916 | 781.287 | 781.287 | 83.977 | 78 |


16 commands per round, ABBA, with 1,800 ms of additional wait and a trace of all the iterations. The final logical sample is available even if the pixels are already stable and need no new copy. The metric is the first inactive logical state after the active one; the TV was not measured. All the ends are required by the analyser. This scenario measures a complete transition and does not replace the 1,000-command percentiles of continuous navigation.


## Interrupted comparison: kept as evidence

| Round | SDL median ms | p95 | p99 | Maximum | Full renderer median ms |
|---|---|---|---|---|---|
| final-navigation-v1-1-reference | 107.523 | 154.538 | 196.461 | 264.981 | 91.468 |
| final-navigation-v1-2-candidate | 104.442 | 145.497 | 189.063 | 246.170 | 87.469 |
| final-navigation-v1-3-candidate | 104.709 | 142.932 | 174.437 | 235.800 | 87.537 |
| final-navigation-v1-4-reference | 98.623 | 138.930 | 167.918 | 209.086 | 81.858 |


The 4,000 responses are valid, but the last reference was run after a long follow-up pause. Intervals between rounds: 50.579, 51.540, 30748.684 s. The reference changed from 107.523 to 98.623 ms; pooling that interrupted session would give 103.073 against 104.576 ms and would show no gain. That is why a new continuous comparison was made, without hiding these numbers. The performance shift was not attributed to the governor or to an unmeasured cause.



## Cold covers, concurrency and memory

The same implementation during the isolated A/B of one/two picture threads,
1,000 entries sharing 160 original 512×768 PNGs, three cycles of
going in/out/carousel/list/menu and rest. All 3,888 inputs were answered,
with a single frontend. This grid has a different load from the historical carousel.


| Round | Decoder | SDL median ms | p95 | p99 | Maximum | RSS peak MiB |
|---|---|---|---|---|---|---|
| covers-final-v2-1-t2 | 2 | 347.612 | 498.639 | 596.107 | 1818.877 | 212.281 |
| covers-final-v2-2-t1 | 1 | 310.051 | 448.673 | 530.290 | 1433.579 | 206.332 |
| covers-final-v2-3-t1 | 1 | 326.147 | 464.910 | 578.710 | 1355.932 | 204.883 |
| covers-final-v2-4-t2 | 2 | 370.810 | 539.440 | 631.764 | 1701.042 | 212.684 |


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


## Rest and controls


| Round; 15 s of rest | Responses | SDL median ms | p95 | p99 | uinput median ms | Static CPU % of one core | Static RSS MiB |
|---|---|---|---|---|---|---|---|
| final-idle-v1-1-reference | 8 | 25.649 | 42.046 | 42.046 | 26.647 | 13.311 | 120.934 |
| final-idle-v1-2-candidate | 8 | 25.881 | 51.880 | 51.880 | 26.851 | 13.254 | 120.723 |
| final-idle-v1-3-candidate | 8 | 25.634 | 39.027 | 39.027 | 26.595 | 13.642 | 120.844 |
| final-idle-v1-4-reference | 8 | 28.053 | 44.682 | 44.682 | 29.341 | 13.307 | 120.961 |


| First response after rest | Renderer median ms | Partial/total responses |
|---|---|---|
| final-idle-v1-1-reference | 16.727 | 8/8 |
| final-idle-v1-2-candidate | 16.273 | 8/8 |
| final-idle-v1-3-candidate | 16.569 | 8/8 |
| final-idle-v1-4-reference | 17.620 | 8/8 |


Eight inputs per round: a small p99 coincides with the maximum. Rest alternates two adjacent systems and may answer with a partial drawing; it is not compared directly with the continuous path of four positions per side. uinput includes evdev/SDL, without physical USB/radio/TV.


| Round | Short touch | Advances holding Right/Left | Stopped on release |
|---|---|---|---|
| final-controls-v1-1-reference | 1 | 3/4 | True |
| final-controls-v1-2-candidate | 1 | 3/5 | True |

## Full opening

The wrapper is the active source, in an isolated package with the same assets/configuration; injection starts after the first interactive frame and checks for a selection change. Opening through an SSH call does not prove the closing time of the physical Scripts menu.


### warm: warm cache

| Round | Wrapper→input ms | Process→input ms | Helper ms | Fixed sleep ms | Load systems s | First interactive frame s | Selection |
|---|---|---|---|---|---|---|---|
| startup-full-warm-v1-1-reference-1-t4 | 15529.898 | 10972.659 | 1900.000 | 2050.000 | 6.825 | 10.034 | 4→5 |
| startup-full-warm-v1-2-candidate-1-t2 | 14511.376 | 9943.172 | 1910.000 | 2070.000 | 6.386 | 9.383 | 4→5 |
| startup-full-warm-v1-3-candidate-1-t2 | 14053.337 | 9530.310 | 1870.000 | 2050.000 | 6.482 | 9.347 | 4→5 |
| startup-full-warm-v1-4-reference-1-t4 | 15605.765 | 11047.205 | 1890.000 | 2060.000 | 6.957 | 10.196 | 4→5 |


| Median of the two rounds | Reference | Candidate |
|---|---|---|
| Wrapper→first input ms | 15567.832 | 14282.357 |
| Process→first input ms | 11009.932 | 9736.741 |


| First input after the first screen; median of the two rounds | Reference | Candidate |
|---|---|---|
| uinput injection→redraw ms | 814.682 | 299.747 |
| SDL queue→redraw ms | 264.348 | 199.437 |
| First interactive frame from the process s | 10.115 | 9.365 |
| LoadConfig s | 6.891 | 6.434 |


Bringing the first screen forward was checked together with the first input actually processed. The cost of that initial navigation is included in the wrapper→input time and also appears separately in the table; the gains are not added up. Two runs per version, with no guarantee of an opening p99.


### cold: first opening after a real reboot

| Round | Wrapper→input ms | Process→input ms | Helper ms | Fixed sleep ms | Load systems s | First interactive frame s | Selection |
|---|---|---|---|---|---|---|---|
| startup-full-cold-v1-1-reference-1-t4 | 20320.503 | 15127.513 | 2300.000 | 2090.000 | 10.270 | 13.930 | 4→5 |
| startup-full-cold-v1-2-candidate-1-t2 | 20104.453 | 15125.249 | 2200.000 | 2070.000 | 10.387 | 14.057 | 4→5 |
| startup-full-cold-v1-3-candidate-1-t2 | 20318.044 | 15306.867 | 2260.000 | 2060.000 | 10.888 | 14.299 | 4→5 |
| startup-full-cold-v1-4-reference-1-t4 | 20691.789 | 15776.372 | 2220.000 | 2070.000 | 10.422 | 14.550 | 4→5 |


| Median of the two rounds | Reference | Candidate |
|---|---|---|
| Wrapper→first input ms | 20506.146 | 20211.249 |
| Process→first input ms | 15451.943 | 15216.058 |


| First input after the first screen; median of the two rounds | Reference | Candidate |
|---|---|---|
| uinput injection→redraw ms | 632.217 | 342.598 |
| SDL queue→redraw ms | 269.010 | 226.492 |
| First interactive frame from the process s | 14.240 | 14.178 |
| LoadConfig s | 10.346 | 10.637 |


Bringing the first screen forward was checked together with the first input actually processed. The cost of that initial navigation is included in the wrapper→input time and also appears separately in the table; the gains are not added up. Two runs per version, with no guarantee of an opening p99.



A new boot_id was required per round; no hash/pre-read of the ELF before starting the process. The original services stayed active and were given the same stabilisation interval. The reboot is not drop_caches.



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



Return from a core, quality pass: 91 frames checked, 0 divergences. The connection failed before sending MENU; NES and PID were checked before recovering the return. The core stayed active for about 157 s, instead of the eight planned. That interval and the double checking are not treated as normal performance. The first input count did not show useful actions; this was fixed in the next pass by explicitly checking the state/selection.


### Normal return with the original test program

| Stage measured on the MiSTer | Time ms |
|---|---|
| Fixed wait after MENU observed by the launcher | 2050.000 |
| Framebuffer activation by the helper | 1890.000 |
| Helper finished→first drawing | 2730.101 |
| MENU observed by the launcher→first drawing | 6710.101 |


Double checking, PNG, slow dumps and profiler off; the same candidate binary at 1080p. This test's catalogue is minimal, with an original NES program without save RAM: it is not a before/after gain measurement on the real catalogue. There was a single core opening. After coming back, five probes confirmed the carousel active and the selection 0→1→0 with the arrows.

The script deliberately delayed those controls and waited for a rest condition that the animated grid did not reach. The follow-up timed out before the script, but the autonomous run finished and was collected with the expected states. The first drawing above was measured before that wait; the late timestamp of the controls does not estimate the shortest input readiness. The reproduction script was fixed to use the safety wait and selection probes, without requiring an animated grid to stop redrawing. The shortest input readiness after returning remains unmeasured.

The attempt final-core-speed-v1 was invalidated: a Return sent before confirming the screen reopened the test program itself. The return was recovered after checking NES/PID, without another frontend; its numbers are not part of the acceptance. The artefacts were kept.


## Layers: separate detailed profile

| Group by dimensions/vertices | Command/worker samples | Visits in bands | Sum of the workers' wall time ms | Share of that sum % |
|---|---|---|---|---|
| large picture, first alpha partial | 48 | 3260 | 797.338 | 36.257 |
| large picture, first alpha 255 | 54 | 3546 | 541.978 | 24.645 |
| smaller picture, first alpha partial | 250 | 1806 | 444.771 | 20.225 |
| atlas/text or geometry with more than four vertices | 173 | 1540 | 268.027 | 12.188 |
| untextured rectangle, first alpha partial | 38 | 627 | 81.437 | 3.703 |
| smaller picture, first alpha 255 | 147 | 761 | 53.027 | 2.411 |
| untextured rectangle, first alpha 255 | 6 | 288 | 12.578 | 0.572 |


This earlier profile samples selected frames. Large pictures add up to about 60.9%; the atlas/text/geometry group, 12.2%. The sum of the two workers is not the frame's real time, may contain pauses off the CPU and includes the cost of the detailed markers. First alpha does not prove the texture's opacity. These groups allow prioritising compositing and pictures, but not attributing an exact semantic cost to each background/cover/overlay without mapping the asset. They were not added to the times of the fast tests.



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



## Installation

Verified record: `deployment-final-20261115.json`.

```json
{
  "backup": "/media/fat/emulationstation/backups/codex-1080p-final-20261115-2556539896074",
  "original_sha256": {
    "LEIA-ME.txt": "aba3f84c1c916291750a97e893e80e90e581fb50362b22b304c40164750c72f2",
    "emulationstation": "d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64",
    "es-mister-helper": "c826dc9341aa47947e25230d8be51ca1b80882a4dc8e11296364d8bb785288b8",
    "es-mister.ini": "cdaa7045899ab22229bd698796c9dc122b907b40d5fb11118baf77bd129ce627",
    "es-mister.sh": "dbaecd4e2b5d9d357f89fe14672bbbb9f1eebcd55d4a57a1d930936a0bde4787",
    "mister-launch.sh": "a1c94434196806b2c76ac297285ae044e7ea1cf9e7c98eb448f675d2f1a2bf25"
  },
  "installed_sha256": {
    "emulationstation": "872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55",
    "es-mister-helper": "c826dc9341aa47947e25230d8be51ca1b80882a4dc8e11296364d8bb785288b8",
    "es-mister.sh": "73ee5b7ea6b26b18fddfaf64599cb09eabcda59cce5da0964e0dffa6d5d9da15",
    "mister-launch.sh": "6202964a729bf5d3da8e8d02e7450fb5138254d1a1214533a73f61953b7fa5f4",
    "LEIA-ME.txt": "aba3f84c1c916291750a97e893e80e90e581fb50362b22b304c40164750c72f2"
  },
  "settings": {
    "SIZE": "1920x1080",
    "FPS": 60,
    "STRIP": 8,
    "PLD": 256,
    "BENCH": 0,
    "PROFILE": 0,
    "SLOW_DUMPS": 0
  },
  "launcher_pid": 8011,
  "frontend_pid": 8420,
  "api_ready": true,
  "framebuffer": "1920,1080"
}
```

(`LEIA-ME.txt` is the name the notes file had in the installed package; the record is kept as it was taken.)



Check of the normal run: PID 8420, one frontend and one wrapper, first interactive frame confirmed, 1920×1080×32 bits, Carbon `es-theme-carbon`, STRIP=8/PLD=256/FPS=60. Frame/texture trace, double checking, PNG/script and profiler off. The installed SHA-256 matches the candidate measured. OOM in this boot: ['oom_kill 0']; CPUFreq policies: []. The previous copy and its hashes are in the backup above.



Observation of the installed run without script/trace, for 10.031 s: RSS 182.008–182.008 MiB, VmHWM 205.723 MiB and CPU 10.268% of one core. The user tried the controller during that period; it is not controlled rest, a before/after CPU comparison or proof of unlimited stability. The rest benchmark above used an active script and must not be confused with this normal observation.



Physical validation reported by the user on 15/11: **controller working normally on the TV**. Frontend audio: **no sound, as before**, no regression reported. This confirms observed operation; it does not measure controller→photons. Game audio was not validated by the silent video program.
