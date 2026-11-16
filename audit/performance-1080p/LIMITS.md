# The limit observed in this round

The measurements and the state of acceptance are in [CLOSING.md](CLOSING.md).
This text explains the criterion for closing, without claiming a physical ceiling of the SoC.
The exploration of the two final hypotheses has ended; the remaining matrix validates the
whole, without opening a third renderer hypothesis.

## What was demonstrated

The largest share is in rasterisation/compositing and copying, not in computing
changed regions or in a 16.7 ms wait added to each late frame.
The optimised historical reference already had two threads, NEON, PLD=256, bands of
eight rows, conditional discarding of transparent commands, a scale cache,
partial redraw and the pacing fix. That gain is not attributed again to the
final stage.

The isolated opaque-alpha change took the full renderer's median from
90.8255 to 87.2005 ms in the medians of two rounds per option. The isolated copy
with two workers came to about 12 ms; the second prefetch saved about
0.45 ms in that component. The combined results must be read in the current
table: isolated gains in catalogue, upload, fade and copy cannot be added up.

There was an important change in the control after the interruption between days: the
first session went from 107.523 to 98.623 ms for the reference. That session was
kept, and a continuous comparison was made so as not to announce an improvement
smaller than the control's variation. This is a limit of experimental attribution,
not a fault that justifies hiding the numbers.

## The 16.7 ms budget

A 1920×1080×4 framebuffer takes 8,294,400 bytes, or 7.91 MiB. The bytes
written for one full presentation at 60 Hz alone come to about 498 MB/s
(decimal), before reading the source, sampling pictures or compositing layers. It is logical
volume, not measured physical DDR traffic. Cache and row reuse change the
effective memory accesses.

A typical blend needs to read source and destination and write the destination. Bilinear
may look up four texels per pixel, with significant reuse. That volume was not
multiplied by all the pixels to claim DDR saturation.
In the carousel script, the statistics recorded about 5.2–5.4
million pixels processed per frame drawn, plus almost two million
copied. This shows repeated work, but does not measure in isolation the physical
limit of reading, writing or the processor's instructions.

When the full renderer costs about 87 ms, the 16.7 ms target
requires removing more than 80% of the remaining time. Gaining 0.45 ms of copying or about
2 ms in preparation does not close that gap. A first partial response
may come close to one frame's budget even when the rest of the transition
needs much more expensive full redraws. Configured FPS, FPS of
iterations without drawing, first response, end of the animation and controller→photons
are distinct measures.

## Why enlarging the cache or swapping the kernel was not enough

The separate detailed profile concentrated about 60.9% of the sum of the workers'
times in groups of large pictures; atlas/text/geometry with many
vertices came to about 12.2%. The sum over workers includes overlap and
may include pauses off the CPU; it is neither the frame's wall time nor an exact identification
of each asset's name. In the 41 full frames sampled, the initial prefix
of stable reusable commands was zero. The scaled cache from 40 to
48 MiB kept the same six misses in the stretch observed. Those concrete
proposals were rejected; this does not prove that any future cache is useless.

An additional full-screen surface costs another 7.91 MiB, besides the texture,
metadata, invalidation, rebuilding and copying. Without measured stability and hits,
creating that cache would only shift work and memory pressure.

Linux 6.18.38-MiSTer, two Cortex-A9/NEON, 491.08 MiB of RAM and no swap. The
kernel exposed no CPUFreq policies, driver, active governor or allowed
frequencies through the interface queried, also under load. There was no governor
A/B: the gain is unmeasured for lack of that control. The compiled-in
performance default does not prove an active governor. The opt-in overclock module was not
loaded and no permanent kernel/VM tuning was done.

The armv7_cortex_a9 PMU appeared in sysfs, with perf_event_paranoid=2. No
cycles/instructions/misses were collected in this matrix. CPU time includes memory
stalls; time off the CPU does not by itself reveal which interrupt, service or
scheduling caused a pause. There is no proof of 100% use of all
resources, nor of DDR saturation. The OOM of the incident with two frontends was
caused by the bench and fixed; it does not explain the navigation of one instance.

## Where the round stops

NEON alpha search, horizontal subdivision, quad merging, packed blending and
cold NEON sampling did not show enough gain in the application and were
rejected. RGBA conversion, opaque alpha and prefetch have independent pixel
differentials; the joint acceptance checks behaviour and the latency's
tails. The one-decoder policy improves the grid with covers, but made the p95 of the
selected cover's availability proxy 58.734 ms worse in a separate
test. That trade-off is stated explicitly, without claiming a universal improvement.

The limit of this round is the best result demonstrated while keeping the appearance,
controls and current architecture. It is neither a perfect optimisation nor a proof of
the physical impossibility of 16.7 ms. An accelerated compositor, deeper reuse
with correct invalidation, or another presentation might require a new
design. The current renderer rasterises the UI on the ARM; it does not use the FPGA as the
interface's compositor. None of these alternatives was implemented or measured here.

The final table records median, p95, p99, maxima, responses, frames above
16.7 ms, RSS and CPU; the host/ARM limitations and the physical checks not yet
observed remain identified. The project will not be described as 60 FPS
in full redraws unless the measurement supports that claim.

## Closing the matrix

The candidate was installed and verified on 15/11/2026. In the continuous comparison,
full renderer: 81.697 → 78.059 ms; SDL queue → first response: 98.300 →
95.500 ms; p99 of the rounds: 174.888 → 152.034 ms. The worst command observed
rose from 210.078 to 230.190 ms and remains shown in the closing report.
The user confirmed normal control on the TV and the frontend without sound, as before.
Controller→photons, the real start from the Scripts menu and the shortest
input readiness after returning from a core were not measured. The limit is this round's, not a
proven physical ceiling nor a promise of 16.7 ms.
