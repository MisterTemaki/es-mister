# Stage 3 — decisions and validation history, 15/11/2026

**The consolidated state, the full tables and the pending items are in [CLOSING.md](CLOSING.md).**
This file keeps the sequence of decisions and the isolated trials.
The first test with covers ran into a loss of SSH and a frozen picture. Access
came back and dmesg confirmed an OOM with two instances of the frontend: the bench's
restore returned before the normal launcher created its process, allowing the
candidate to start at the same time. The synchronisation was fixed in the test
utilities; that session was invalidated. It is not evidence of a physical limit of the
hardware. The bench regression and acceptance still come before any deploy.
At the start, the device kept the validated binary `d53703bb…abb64`, Carbon at 1920×1080,
two rendering threads, STRIP=8, PLD=256 and FPS=60. There was no kernel
tuning. The completed Linux investigation is in [LINUX.md](LINUX.md).

## Incremental results already observed on the ARM

| Isolated change | Control | Candidate | Reach of the evidence |
|---|---:|---:|---|
| Catalogue: four → two threads; process → first input answered | 9.864 s | 9.225 s | Six warm openings per policy, alternating order; selection 12→13 checked in all |
| RGBA→ARGB conversion, opaque 1080p surface | 54.82–54.89 ms | 26.42–26.48 ms | ARM microbenchmark, eight pixels at a time; not the time of a frame |
| RGBA→ARGB conversion, alpha 127 | 43.28–43.32 ms | 26.47–26.59 ms | Same ARM microbenchmark |
| RGBA conversion in the application: process → first input answered | 11,289.523 ms | 10,508.790 ms | Six warm openings per option, ABBA repeated three times; selection 12→13 in all |
| RGBA conversion in the application: uinput → first redraw | 658.915 ms | 288.500 ms | First command brought forward; includes the queue before the first useful drawing |
| Known alpha of an opaque texture: full renderer | 90.8255 ms | 87.2005 ms | Median of the medians of two rounds of 1,000 commands per option |
| Same change: SDL queue → first redraw | 104.7305 ms | 102.0535 ms | Four ABBA rounds; 4,000/4,000 responses |

The two navigation rows use **the same executable**, with only
`ES_SOFT_OPAQUE_FADE=0/1` changed. That executable's SystemData and texture resources
were rebuilt from cba2b66, the already optimised version.
The historical gain against OPTIMIZATIONS=0 is not attributed to the candidate.

The four long rounds of the alpha change are complete. They are incremental
measurements of the same already optimised version, without using OPTIMIZATIONS=0 as the base.

| Round, ABBA order | SDL median (ms) | p95 (ms) | p99 (ms) | Maximum (ms) | Full renderer median (ms) |
|---|---:|---:|---:|---:|---:|
| A1, previous in the same executable | 104.045 | 149.007 | 181.988 | 212.040 | 90.387 |
| B1, opaque alpha on | 101.844 | 143.474 | 174.809 | 188.995 | 87.327 |
| B2, opaque alpha on | 102.263 | 146.481 | 181.170 | 236.987 | 87.074 |
| A2, previous in the same executable | 105.416 | 149.418 | 181.896 | 196.366 | 91.263 |

The gain between the rounds' medians was 2.56% in the first response and 3.99% in the
renderer. B2's worst occurrence raised the input maximum by 24.947 ms
over the worst control; the renderer's maximum went from 229.334 to 299.778 ms.
In that frame, the critical path had 289.983 ms of wall time and 166.362 ms of CPU.
There are about 124 ms without execution on that thread; the data do not identify
which scheduling, interrupt or service caused the pause. That maximum is not
attributed to the governor or to the alpha change without causal evidence.
The maximum RSS with tracing was between 163.1 and 163.5 MiB. The final decision also depends
on the combined comparison, without hiding this trade-off in the observed maximum.

An additional comparison of 64 commands per round against the SoftRaster actually
extracted from cba2b66 confirmed the reduction: SDL medians A1/B1/B2/A2 of
109.293/102.867/103.416/107.361 ms; renderer 92.076/87.118/88.126/90.124 ms.
All 256 commands were answered. The analyser was fixed to
associate frames by the exact instant of dispatch, keeping the SDL origin
only for the latency; this avoids selecting the previous idle frame by
rounding by one millisecond. In that sample, the observed end of the main
animation came at about 658–665 ms; intervals between frames came at
about 91–96 ms. The first-response event and the end of the transition are
distinct metrics. Interrupted transitions are not lost commands.

The short gain does not bring full redraws close to 16.7 ms. The configured ceiling
of 60 FPS remains a limiter, without showing 60 full frames/s.
The input times measure software up to the writing of the first redraw, not
physical controller–TV latency nor the end of the transition.

## Fidelity and fixes

- Rasteriser: 112 deterministic scenes identical to the independent implementation
  extracted from cba2b66, on the host with ASan/UBSan/LSan and on the ARM. Prefetch
  and eight-row bands were kept. The new path removes only the redundant
  calculation `div255(255 * alpha)`, keeping the other roundings.
- Application, opaque alpha off/on: PNGs of the carousel, list, selection and
  menu with identical SHA-256. Partial versus full redraw: 0/88 and 0/86
  divergences. In a separate diagnosis, the candidate processed 136,192,320
  pixels through the new path during transitions. These tests have detailed tracing
  or double checking; their times are not used as a speed test.
- RGBA conversion: 7,027 differential cases on the ARM and host, including tails,
  overlap, alignments, alpha and sentinels. Reference extracted from the previous
  code, without sharing the candidate function. The backend integration also
  passed in 14 independent ARM scenes. In the application, 18 captures of equivalent
  logical states had identical SHA-256, including eight transition states
  and eight selection states; 0/60 and 0/63 partial/full divergences.
  PNG and double checking were off in the speed measurements.
  The sources set up the path by default only in the candidate MiSTer build,
  respecting OPTIMIZATIONS=0; the previous production stays preserved while
  the joint acceptance is not complete. Other platforms keep the default.
- TextureLoader: exit with loading paused and shutdown notification
  fixed; 64 creation/shutdown cycles and queue completion passed on the
  host and ARM. After the A/B with covers, the MiSTer default became one decoding thread;
  the other platforms keep their defaults.
- Cleanup: the 250 MiB RAM limit on 32-bit platforms becomes
  independent of MaxVRAM's early return. An ARM test reproduced the previous
  failure with MaxVRAM=512 MiB and confirmed the fix. With the 128 MiB default,
  this case is not a demonstrated explanation of the normal latency.
- The accounting now reads sizes published in atomics, without waiting for a
  decoder's mutex. The ARM test confirmed reading the previous value during
  a blocked write and the new value after its publication. That is 16 bytes of
  metadata per texture on ARM32, with no other surface. The counters keep
  the semantics of committed buffers; they do not represent RSS or decoding
  temporaries. The GLES3 branch built, but was not run on another platform.

An intermediate prototype that blocked getMemoryUsage on the mutex was
discarded: it introduced waits of up to 153 ms. Removing that blocking is not
a gain over version cba2b66, which never contained it.

## Two final hypotheses and quality control

The additional prefetch keeps PLD=256, the stores and the presentation order.
In four short ABBA runs of 64 commands, the renderer's medians were
87.401/84.946/86.135/87.133 ms. The median of the medians went from 87.267 to
85.541 ms. The SDL medians were 102.604/101.747/103.199/105.169 ms, with
256/256 responses. The candidate's input p99 (176.178 and 171.955 ms)
exceeded the controls' (162.434 and 159.409 ms); that variation in a short sample
is not hidden. In the isolated component, the copy gained about 0.45 ms.
All 2,073,600 pixels of the framebuffer were checked after each trial.

The NEON search for alpha boundaries passed on 1,632 independent metadata and
155,376 boundary cases with guards on the host and ARM. A first prototype
made pictures with much alpha detail worse; the scalar path was kept
for those cases before the repetition. The component measurement improved all four
patterns. That still does not show a gain in the application or at opening.

The opening comparison was then completed: six runs per option, ABBA
repeated three times, same binary and NEON upload on. Process → first
input answered: 10,719.855 ms for the control and 10,793.828 ms for the candidate;
maxima 11,098.923 and 11,449.107 ms. SDL → first redraw: medians
190.922 and 223.540 ms; maxima 391.369 and 588.064 ms. Uinput → first
redraw: medians 475.850 and 459.466 ms; maxima 651.461 and 848.210 ms.
All 12 commands changed the selection 12→13. The variation between initial frames
prevents attributing each difference to the scan; the component gain did not show
a reproducible improvement in the opening goal. **Hypothesis rejected.** The active
function findClearRuns was restored from the verified reference. The prototype and its
tests are in `tests/audit/experiments/alpha-scan/`, outside the distributed build.

In the first visual comparison, 16/18 PNGs matched; two frames during the
entry into the list differed by up to two levels per channel. They were not accepted as
equivalent just because the difference was small. A **quality-only** build
then provided logical steps of 100 ms, regardless of the cost of PNG and
loading. That build also compared, in each real upload, the alpha metadata
with the independent previous algorithm, without copying the surface's pixels.
Result: **18/18 identical SHA-256**, 71 metadata checks in each
run and 0/61 and 0/60 partial/full divergences. The cause was the instant of the
animation captured. The fixed clock exists only in the quality build; it goes into neither the
active sources nor the speed measurements. The variable is also refused by the
speed analysers.

The commitment to explain the limit, without confusing it with the SoC's physical limit,
is detailed in [LIMITS.md](LIMITS.md). The matrix was closed on 15/11, and the candidate `872dd340…65b55` was installed
with a backup. The limits and the pending physical checks are in the closing report.

## Covers — comparison completed

The four valid rounds `covers-final-v2-*` completed 3,888/3,888 responses,
with a single frontend and no new OOM. One picture thread reduced the median
of the rounds from 359.211 to 318.099 ms and the p99 from 613.936 to 554.500 ms.
The RSS peak went from about 212–213 to 205–206 MiB with tracing.
The queue may be longer with one worker, but there were fewer loads and discards.

The fixture has 1,000 entries sharing 160 PNGs of 512×768. The real NES
catalogue has 2,465 files; the 45 entries in its gamelist have no covers.
The numbers of the synthetic grid are not compared directly with the historical carousel.

In the separate scenario of a visible selected cover, the request→upload median stayed
practically the same (551.805 → 554.459 ms), but p95 got worse by 58.734 ms. The trade-off
was kept explicit. The pass with minimal telemetry had an RSS peak of
192.184 MiB. The per-round table, the counters and the identification limits
are in [CLOSING.md](CLOSING.md), with the raw JSONs in the evidence delivery.

## Hypotheses discarded or still limited

Horizontal subdivision, quad merging, blending with packed channels and cold NEON
sampling were discarded after measurement. The last one improved a microbenchmark,
but increased the median of the first response by 177 ms and the opening's maximum by
509 ms. It was not installed. The patches remain in the bench's history.

A detailed profile of the first eight navigations found zero consecutive reusable
initial commands in each of the 41 full frames
observed. A simple cache of the layers' prefix would not help in that stretch.
That does not prove every cache is useless. A 32-bit 1080p surface
takes 8,294,400 bytes (7.91 MiB), plus the cost of building, copying and invalidating it.

The launcher's waits were instrumented optionally with
`ES_WRAPPER_TRACE=/path`. The two-second sleep and the recovery by
retries remain. Real measurements of the Scripts path, the
return from a core and opening after a reboot are still missing; the opening numbers above start
at the process, after the helper's preparation on the bench.

## Reproducing the current bench

Run from `es-mister-audit` in PowerShell, using the ARM cache already
prepared and SSH set up. These names need to be new on each run.

```powershell
wsl -d Ubuntu-26.04 -u root -- bash ./validation/cross_cache.sh bash ./validation/cross_build.sh opaque-only diagnostic-reference
wsl -d Ubuntu-26.04 -- arm-linux-gnueabihf-strip -o ./validation/arm/emulationstation-opaque-only-stripped ./validation/arm/emulationstation-opaque-only
wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py upload ./validation/arm/emulationstation-opaque-only-stripped stage3-opaque-only
pwsh -NoProfile -File ./validation/opaque_compare.ps1 -Prefix new-quality -Order 0,1 -Quality -Detail 16
pwsh -NoProfile -File ./validation/opaque_compare.ps1 -Prefix new-long -Order 0,1,1,0 -Steps 1000
```

`cross_build.sh` calls `mister/build.sh es` on the prepared sources, records
the hashes of the files used and waits for the build to finish before copying the
ELF. The diagnostic-reference variant keeps cba2b66's resources/SystemData;
`active` builds all the active sources. The ARM build is static for
arm-linux-musleabihf/Cortex-A9. Do not measure before build/upload have finished.

The fast runs use OPTIMIZATIONS=1, without PNG, double checking, dumps,
forced redraw or profiler. The frame trace was limited to redraws so that
the whole navigation fits in the buffer; its saturation is checked by the analyser.
The 1,000 inputs are still recorded in a separate buffer.

## Final matrix

The final installation, the normal run at 1080p and its hashes were checked.
This round's investigation is closed; the measurement limitations remain
explicit in CLOSING.md.

The exploration of the two final hypotheses has ended: the copy prefetch goes through
joint acceptance and the NEON alpha search was rejected. Do not open another hypothesis.
The decoder A/B, the minimal memory measurement and the short comparison of the sources
with covers were completed. The long navigation had to be repeated in the same
session after an important change in the control between days; the interrupted session
was kept, without attributing a gain to it.

The state of controls, rest, warm opening/after reboot, return from a core,
installation and physical inspection is in [CLOSING.md](CLOSING.md). The complete
commands are in [REPRODUCE.md](REPRODUCE.md).
