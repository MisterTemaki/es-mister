# Validation on the MiSTer at 1080p — 13–14/11/2026

Final update of 15/11: incremental decisions, the current comparison and pending items
in [CLOSING.md](CLOSING.md). This file keeps the historical reference.

Update of 14/11: the Linux-specific investigation is in [LINUX.md](LINUX.md).
The same optimised binary answered two new sequences of 1,000 commands
(medians 97.563 and 97.815 ms), with no kernel change. No CPUFreq policies are
exposed in this boot; the governor A/B was unavailable. These numbers do not
replace the historical comparison below nor represent a new code gain.

This stage complements the historical stage 1 report and the analysis `ANALYSIS.md`.
The resolution was kept at **1920×1080**, with the Carbon theme, two threads,
**STRIP=8 and PLD=256**. Resolution, filtering, effects and transition durations
were not reduced to obtain the gains.

## Method and origin

The executable originally installed had SHA-256
`253a15744237071c9bc136d94666957cc4567fbbe8ebf349ce1a10cd86daf404`, identical to package 18.
The ZIP received was preserved. The comparison with the local code and the reconstruction
of patch 29 are in `comparison.json` and `zip-vs-installed.diff`.
No claim is made to have rebuilt the original executable bit for bit.

The final build is Release/O3, static, ARMv7 hard-float, Cortex-A9/NEON,
with Zig 0.15.2. SHA-256 of the distributed executable:
`d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64`.
Each run used isolated settings and HOME, keeping the device's resources and
catalogue. No existing game was launched, removed or changed by the benchmarks.

The rebuilt control keeps the renderer's previous paths with
`ES_SOFT_OPTIMIZATIONS=0`. An extra control of the executable actually installed
had a median internal response of 112 ms over 32 commands. The rebuild with
optimisations off came to 109 ms in the 48-command tests; the short optimised
version came to about 92 ms. These short tests are not enough to establish p99.

The sustained comparison uses **1,000 commands per run**, alternating
four moves to the right and four to the left, spaced out, with no forced-redraw
benchmark, replay of slow dumps or signal profiler. The time counted
runs from the timestamp of entry into the SDL queue to the end of the first redraw
that answers it. The old metric, from dispatch in the loop, was also kept.
This measures software; **it does not measure physical controller–TV latency** nor the end of the animation.
The keys come from SDL_PushEvent, not from a physical controller.

The tracer records in RAM, has limits of 8,192 frames, 65,536 command samples
and 8,192 input responses, and only writes the CSVs at shutdown. The 1,000 inputs
are recorded regardless of the frame limit. Memory is sampled at 1 Hz.
The per-stage profile covers the start of the test up to the buffer's limit, not all
the frames of the twelve minutes. p99 uses nearest rank, without interpolation; it describes
one sequence per version, not a confidence interval or a worst-case guarantee.

<!-- MEASUREMENTS BEGIN -->
## Sustained comparison

| Measure (ms) | Control | Optimised | Reduction |
|---|---:|---:|---:|
| SDL queue → redraw, median | 121.644 | 102.530 | 15.7% |
| SDL queue → redraw, p95 | 135.504 | 118.809 | 12.3% |
| SDL queue → redraw, p99 | 144.798 | 132.384 | 8.6% |
| SDL queue → redraw, maximum observed | 240.934 | 195.961 | 18.7% |
| Dispatch → redraw, median | 107.052 | 90.695 | 15.3% |
| Dispatch → redraw, p99 | 130.138 | 119.625 | 8.1% |

Each version answered exactly 1,000 commands. The control lasted 728.8 s and the optimised one 767.0 s, including start-up and shutdown. The total duration also includes the script's artificial waits; it is not a throughput measure.

| Stage of the full redraw, median (ms) | Control | Optimised |
|---|---:|---:|
| UI preparation | 3.532 | 2.678 |
| Dirty regions | 0.104 | 0.104 |
| Execution including copy | 98.969 | 90.280 |
| Slowest worker's drawing | 89.172 | 81.909 |
| Slowest worker's copy | 11.426 | 9.695 |
| Limiter wait | 16.752 | 0.003 |

The frame distributions differ because the corrected limiter allows more animation steps and culling allows partial updates. The main comparison is the response to the same inputs. **60 FPS was not reached in Carbon's full redraws at 1080p.**

Maximum resident memory during the instrumented test: 165.23 MiB for the control and 165.32 MiB for the optimised one. After the tracing buffer filled and before shutdown, the medians of 100 s windows were 165.309 and 165.320 MiB for the optimised one. There was no relevant growth in that window; this does not prove there are no leaks on every screen. Tracing reserves and occupies memory progressively; the numbers are not the minimum usage of a normal run.

In the separate quality validation, **0 of 91 frames checked diverged** between partial rendering and the full reference. Carousel, NES list, selection change and menu were captured at 1920×1080 and inspected visually. That mode redraws the reference and saves PNGs; its times are not used as a benchmark.

One thread had an internal median of 134.652 ms over 24 commands, against 90.695 ms with two threads in the long test. The samples have different sizes, but no reason appeared to change the two-thread default.

Start-up of a new process with the system cache already warm, one run of each:

| Version | Ready mark (s) | First command answered (s) |
|---|---:|---:|
| startup-control-final | 8.165 | 9.733 |
| startup-fast-final | 8.258 | 9.664 |

The script waits 9 s before the key; the first command confirms it works at that moment and is an upper bound, not a search for the first interactive millisecond. The values exclude the wrapper's initial wait. There was no reboot/global cache clearing to measure a cold boot, and no statistical start-up gain is concluded from one sample.

<!-- MEASUREMENTS END -->

## What explained the cost

In the control with detailed sampling, the navigation's full redraws had
approximate medians of 3.51 ms preparing the UI, 0.10 ms computing dirty regions,
100.08 ms running the renderer (copy included), 90.15 ms of drawing in the slowest
worker and 11.14 ms of copying. These medians are independent distributions and
must not be added up as if they came from one frame.

The limiter also added about **16.75 ms of waiting after frames that were already
late**. Waking the workers cost about 0.11 ms. In the detailed navigation samples
there were 3,679 cache lookups, all hits, with no row generation or cache
allocations; no evidence appeared that the cache mutex was the dominant bottleneck
under this warm load.

The main cost is in rasterisation/compositing and moving pixels.
There were text commands with hundreds of vertices whose whole opacity was
zero. Even invisible, they went through bands, vertices and glyph preparation.
Full-screen backgrounds and overlays remain costly at 1080p.

The 150–160 ms in the old logs are observations of specific frames, including
transitions and warm-up; they are not a universal median. The old slow dumps
redraw commands serially for diagnosis and are not equivalent to the original
parallel time. The gain comparison uses controlled runs; it does not subtract
92 ms directly from 160 ms to announce an artificial improvement.

## Fixes applied

- Discard a command only if all its vertices have zero alpha, normal blending
  SRC_ALPHA/ONE_MINUS_SRC_ALPHA and there is no stencil write. Frames containing
  only discarded commands still clear the previous frame's pixels.
- Reject quads outside the band before preparing the glyphs' colours and interpolation.
- Keep the FPS limit without imposing an extra frame of waiting when drawing
  has already used up the budget. Transitions remain driven by real time.
- Specialise NEON for alpha modulation with white RGB, keeping the same
  alpha and blend arithmetic and the prefetch. The pixel comparison validates this
  specialisation; the variable microbenchmarks do not support a precise isolated gain.
- Turn off BENCH, PROFILE and SLOW_DUMPS in the normal-use configuration. The dumps
  remain available for explicit diagnosis.
- Fix the helper to recognise the framebuffer already active when reopened over
  SSH. The photograph showed the Linux login and the frontend was not started by
  the previous helper. The new helper confirmed opening at 1080p in 82.7 ms; auto/2
  followed by auto confirmed the return to 1920×1080. These numbers measure the helper,
  not the catalogue load or the whole start-up.

The console diagnosis combines the photograph, the confirmation of fb_cmd on the
device and the inspection of the [official Main_MiSTer](https://github.com/MiSTer-devel/Main_MiSTer/blob/master/menu.cpp).
No claim is made that this file matches the installed Main bit for bit.

## Integrity and pending items from the previous report

| Item | Result at this stage |
|---|---|
| P01 HTTP | The server stays alive until the join. 64 immediate start/stop cycles passed, a handler active beyond the internal stop timeout and an exception at start, under ASan/UBSan/LSan. |
| P02 ThreadPool | Synchronised queues/flags, waiting on a condition_variable, reuse, cancellation, fallback for zero cores and exceptions. 32 reuses, 4 producers/4,000 jobs, nested jobs and exceptions passed. ThreadSanitizer was not run. |
| P03 copyFile | 64 KiB buffer, exclusive temporary file in the destination's directory, read/write/flush/close validated and publication only on completion. Missing source, same file/hard link, symlink, permissions and a real write failure through RLIMIT_FSIZE passed, preserving the previous destination. Windows was not run. |
| P04 installers | The Git theme is now extracted/validated in staging, keeping the old one and rolling back on a publication failure. Invalid CRC, traversal and an EXDEV injected into publication passed. The Win32-specific installers remain outside the MiSTer validation and are not declared fully fixed. |
| P05 search | Jaro-Winkler no longer zeroes two fixed arrays of 65,000 integers on the stack per pair; it uses sized storage and explicit limits. 1,240 results identical to the old formula passed on valid inputs, and cases of up to 100,000 bytes. The original integer division of the transpositions was kept to preserve the ordering. |
| P06 build | pugixml recovered at revision 61c944895e071872370219cadade8423dc6e04dc; RapidJSON and controllers pinned to commits; ALSA now installs into DESTDIR so as not to write settings on the host; full ARM build run. Pinning revisions does not mean a bit-for-bit reproducible build nor a dependency/CVE audit. |
| P07 coverage | The full review of the other areas and platforms remains open. The original stage 1 inventory is historical; it does not represent a new manual review of all 1,008 files. |

The eight Linux suites pass with ASan/UBSan/LSan: HTTP/ZIP/files (17 scenarios),
Settings, backend, server, pool, copy, similarity and themes. The rasteriser's 16
differential scenes produce the same pixel hashes on ARM/NEON and in the native
test. This is not equivalent to testing every theme/controller combination.
There was no physical test of audio, hotplug, MGL launch or return from a core.

On ARM, a synthetic search microbenchmark with 2,000 pairs went from 717.845 ms
to 17.762 ms. It is a measure of that component and those strings; it does not mean
the whole interface became forty times faster.

The ARM build still has no HTTPS, preview videos or shaders, as per the port's
earlier limitations. No additional features were disabled to obtain performance.
Saving by rename protects against the operation failures tested, but
gives no transactional guarantee through power loss. The extraction code does not
offer isolation from another privileged process changing links during the operation.

## Reproducing the measurements

`ANALYSIS.md` describes the instrumentation points. In a controlled launch,
use `ES_SOFT_TRACE=/path/run.csv` and optionally `ES_SOFT_TRACE_DETAIL=16`.
Keep ES_SOFT_FPS=60, ES_SOFT_STRIP=8, ES_SOFT_PLD=256 and the same resources.
Use ES_SOFT_OPTIMIZATIONS=0/1 to compare the paths. Do not run collection,
log compression or another benchmark on the ARM while measuring navigation.

This stage's bench scripts and logs are in `evidence-stage2`; the SSH keys
are not part of the artefacts. The update contains a SHA-256 manifest and an
installer with backup, and preserves installed games, metadata, controllers and themes.
To go back to the previous version, close the interface through the local API, restore the
five files from the backup folder recorded at deployment and restart the launcher.

## Deployment checked on 14/11/2026

The update was installed and restarted by the normal `es-mister.sh`. Checked:
a single active frontend, executable and helper with the manifest's hashes, framebuffer
1920×1080, STRIP=8, PLD=256, FPS=60 and the local systems API answering. Tracing,
BENCH, PROFILE, SLOW_DUMPS, test scripts and double pixel checking were
off in the normal-use process. The log marked ready at 9.537 s from the
start of the process in that start-up after a reboot; that number excludes the wrapper.

Backup of the five previous files:
`/media/fat/emulationstation/backups/codex-1080p-19691231-210331-561759`.
The name reflects the device's unsynchronised clock after the reboot; the deployment
was observed on 14/11/2026 on the computer. The original helper from before the fix
is also preserved in `codex-performance-20261113/backup/es-mister-helper`.
The complete record is `evidence-stage2/deployment-verification.json`.
