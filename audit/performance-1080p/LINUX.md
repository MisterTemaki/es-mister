# The MiSTer's Linux and latency — 14/11/2026

## Conclusion and scope

Investigation on the device 192.168.15.14, preserving the installed binary
`d53703bb6cd2dc16838d885645d797cac512a49c68d282962a03ae2bb94abb64`, catalogue,
Carbon, native 1920×1080 output, two drawing threads, STRIP=8, PLD=256,
ES_SOFT_FPS=60 and the optimisations already present. There was no overclock, module loading,
governor change, sysctl, swap change, drop_caches or permanent tuning.

**No CPUFreq policy is registered in this boot. The original/performance A/B
is unavailable; the gain of that switch is unmeasured.** The directory
`/sys/devices/system/cpu/cpufreq` exists and is empty; there are no `policy*` nor
`cpu[0-9]*/cpufreq`. This was also checked under load by the 1 Hz sampling.
No latency is attributed to a frequency reading at rest.

## Inventory observed

| Item | Device |
|---|---|
| Kernel | Linux **6.18.38-MiSTer**, `#2 SMP Sat Sep 12 20:19:51 CST 2026`, armv7l |
| CPUs online | 0–1; Cortex-A9, ARMv7, NEON |
| CPUFreq compiled in | `CONFIG_CPU_FREQ=y`; compiled-in default `performance` |
| Governors compiled in | performance, powersave, userspace, ondemand, conservative, schedutil |
| Policies exposed | None |
| Active CPUFreq driver | Not exposed/none registered through the interface queried |
| Active CPUFreq governor | Not exposed; the compiled-in default does not prove an active governor |
| Allowed, minimum/maximum and current frequencies | Not exposed; physical frequency not measured |
| Module available on disk | `socfpga-cpufreq.ko.xz`, name `socfpga_cpufreq`, not loaded |
| Module description | `MiSTer Cyclone V CPUFreq with explicit overclock opt-in` |
| CPUIdle | driver `none`, reported governor `ladder`, no `state*` exposed |
| Preemption/tick | `CONFIG_PREEMPT_NONE=y`, `CONFIG_HZ_1000=y`, periodic tick |
| Visible RAM | 502,864 KiB = 491.08 MiB |
| Swap | 0 KiB, `/proc/swaps` with no devices; pswpin=pswpout=0 |
| PSI | Not compiled in (`CONFIG_PSI` off); `/proc/pressure/*` unavailable |
| Memory available in the inventory during navigation | 231,688 KiB = 226.26 MiB; per-scenario ranges below |

`BogoMIPS=200` was not used as a measure of the clock. The clocks directory in
debugfs was not available; debugfs was not mounted nor PLL registers read
through physical access. No module was loaded to manufacture a policy
absent from the original configuration.

CPUFreq needs a registered driver to create the policies and their controls.
CPUIdle is another subsystem; the name `ladder` does not mean it is the frequency
governor. Primary references:
[CPU performance scaling](https://docs.kernel.org/admin-guide/pm/cpufreq.html) and
[CPU idle](https://docs.kernel.org/admin-guide/pm/cpuidle.html).

## Measurement method

- Navigation: the historical sequence of **1,000 commands**, four Right/four Left,
  key-down 60 ms, a 150 ms wait after key-up and the script's 450 ms wait.
  There are 14 artificial initial seconds; they are excluded from navigation and do not measure start-up.
  Inputs through SDL_PushEvent, as in the historical reference.
- Rest: a persistent uinput device created **before** SDL initialisation;
  each touch alternates Right/Left, lasts 60 ms and comes after **15 fixed s** of rest.
  There are 3 separate s to finish the previous transition. Besides the SDL metric,
  uinput submission → end of the first redraw's copy is recorded. This includes
  Linux/evdev/SDL input, but not the radio/USB of a physical controller nor scanout/TV.
- Isolated HOME/configuration; the device's same catalogue and themes. No existing
  game, save or metadata is changed. The normal launcher is restored at the end.
- `ES_SOFT_TRACE_DETAIL=0`; double-check, PNG, signal profiler, slow dumps and
  forced redraw off. Tracer in RAM, limited to 8,192 frames and 8,192
  responses, written on exit. Responses cover the 1,000 commands; per-stage
  times cover only the window up to the frame limit, identified in the JSON.
- Python monitor at 1 Hz: MemAvailable/RSS, the process's major faults,
  pswpin/out, direct/kswapd reclaim, allocstall, refaults, CPU and policies.
  Kept in RAM and written as JSON at the end. The 1 s resolution does not prove causality
  for a 100 ms pause; so the windows of the ten slowest inputs and the per-worker
  CPU time already available in the trace are also examined.
- Nearest-rank percentiles. Small rest samples are descriptive; p99
  coincides with the maximum if there are fewer than 100 responses. They are not guarantees.
- Order: original navigation 1 → uinput pilot → original rest → original
  navigation 2. It is a repetition of the original configuration, **not a governor A/B**.
  The first navigation included an inventory/hash collection during the run;
  repetition 2 is the confirmation without that extra read. Compressing/collecting the
  artefacts is done only with the application closed.
- There were failures in the host follow-up process's SSH status queries.
  The scripts and monitors carried on autonomously on the MiSTer; collection
  was resumed through new connections, checking 16/16 and 1,000/1,000 responses.
  The old host follow-up was ended before the final manual collection.
  The transport logs and those of the manual completion were kept.
- This report's date comes from the host. The device's civil clock was not
  synchronised and was not adjusted; all latency intervals use a monotonic
  clock, including the uinput/renderer pairing.

<!-- KERNEL RESULTS BEGIN -->
### Response to the first redraw (ms)

All the new rows use the **same original optimised binary**, with no kernel
tuning. The differences from the history are not a code or governor gain.

| Scenario | Responses | Median | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|---:|
| Optimised historical reference | 1,000 | 102.530 | 118.809 | 132.384 | 195.961 |
| Original navigation 1 | 1,000 | 97.563 | 112.076 | 151.822 | 176.928 |
| First command after 15 s of rest, SDL queue | 16 | 23.669 | 27.254 | 27.254 | 27.254 |
| Same rest, from uinput submission | 16 | 24.386 | 28.564 | 28.564 | 28.564 |
| Original navigation 2 | 1,000 | 97.815 | 120.360 | 146.695 | 185.965 |

The nominal 15 s rest measured 15.014–15.015 s. All 16 touches were answered.
In the part of the trace that allows relating input and frame, **10/10 first
responses after rest were partial**, with a median execution of 16.515 ms.
In navigation 1, **377/377 related responses were full frames**;
in repetition 2, **381/381**.
The other inputs are in the responses CSV, but outside the limited frame
window. The rest scenario alternates two adjacent systems; the historical
sequence visits four positions to each side. These numbers are not compared
as if they differed only by the rest or by a governor.

In repetition 2, the queue before dispatch had a median of 16 ms (p99 17 ms), and
dispatch → redraw had a median of 82.304 ms. The historical injection through
`Script::frame()` happens inside swapBuffers before the limiter's final
wait; the queue includes the time until the next iteration. That field is not
an isolated measurement of Linux scheduling. In the rest via uinput, the queue
measured a median of 0 ms and p99 1 ms; the external measurement also includes the evdev/SDL stage.

After the first partial response, the animation's full frames remain
expensive. The time to finish the transition was not measured in this kernel
investigation; the configured durations were not changed.

### Renderer in the recorded window

| Measure (ms) | Navigation 1, median / p95 / p99 | Navigation 2, median / p95 / p99 | Rest, median / p95 / p99 |
|---|---:|---:|---:|
| Full execution with copy | 81.575 / 97.453 / 116.876 | 81.843 / 100.565 / 137.552 | 83.355 / 103.288 / 164.040 |
| Slowest worker's drawing, wall | 73.400 / 90.351 / 109.213 | 73.906 / 93.153 / 126.744 | 75.417 / 96.716 / 156.575 |
| CPU of the same worker | 70.632 / 82.704 / 89.060 | 70.835 / 83.484 / 88.922 | 70.535 / 79.465 / 152.613 |
| Off the CPU, per frame/critical worker | 1.696 / 21.487 / 39.467 | 1.918 / 24.323 / 53.132 | 2.903 / 25.033 / 30.679 |
| Largest copy among workers | 9.285 / 12.450 / 16.515 | 9.163 / 12.464 / 16.626 | 9.895 / 12.203 / 12.693 |
| Publication → worker start, largest | 0.106 / 1.651 / 2.378 | 0.109 / 1.642 / 2.409 | 0.098 / 0.297 / 2.514 |

Navigation 1: 2,246 full frames, a trace window of 290.946 s between the first
and the last record; navigation 2: 2,260 full frames, a window of 293.591 s.
Rest: 59 full frames within the window; p99 with that sample is the maximum.
Wall/CPU times suggest relevant blocking or descheduling in part
of the tail, without identifying the cause. **The typical cost remains in the drawing
actually executed and in the copy**, not in waiting to wake the workers.

### Memory and paging

| During the inputs | Navigation 1 | Navigation 2 | 15 s rest |
|---|---:|---:|---:|
| MemAvailable minimum / median (MiB) | 218.930 / 223.969 | 217.207 / 222.941 | 247.109 / 250.562 |
| Maximum RSS (MiB), with tracing | 165.629 | 165.633 | 135.195 |
| Swap-in / swap-out (pages) | 0 / 0 | 0 / 0 | 0 / 0 |
| Frontend major faults | 0 | 0 | 0 |
| Bytes read by the frontend from storage | 0 | 0 | 0 |
| Global major faults | 15 | 1 | 0 |
| Global allocstall events | 6 | 0 | 0 |
| Pages reclaimed directly | 262 | 0 | 0 |
| Pages reclaimed by kswapd | 6,769 | 978 | 0 |

These are deltas over the inputs' window, excluding the process's initial load. At
rest, all recorded reclaim/refault/paging deltas were zero.
In test 1 there is limited global activity: 2 of the 10 slowest windows show
direct reclaim/allocstall within a margin of ±1 s; the other 8 show none of these
events, including the three largest delays. The margin may include activity
after the response and does not show causality. The extra inventory collection
is also an interfering factor in that first round. There is no basis to
attribute the typical latency to a lack of RAM or to justify a memory sysctl.

In repetition 2, none of the ten slowest windows had swap, major faults or
reclaim (direct or kswapd). The tail persisted with zero allocstall/direct reclaim
over the whole navigation. This weakens the paging hypothesis as the main
explanation; it does not exclude interrupts, scheduling or the application's locks.

A read-only inspection of zoneinfo during the rest found the Normal zone
with 10,917 free pages and a high watermark of 1,062; min_free_kbytes=2833,
watermark_scale_factor=10 and lowmem_reserve_ratio=32/0 stayed unchanged.
That snapshot does not replace the per-window deltas nor prove the state during
an earlier pause. The process's CPU in the rest interval had a median of
11.607% of one core; that is usage with tracing active and includes the whole 15 s interval.

**Governor A/B: unavailable. Gain when leaving rest or during navigation:
unmeasured.** No kernel tuning was accepted or incorporated into the configuration.

Final check on the device: one normal frontend ready, systems API HTTP
200, original HOME, same binary/helper, same settings/input/systems hashes,
framebuffer 1920×1080×32, FPS60/STRIP8/PLD256, heavy diagnostics off,
test keyboard removed, CPUFreq module still not loaded and same boot.
`swappiness=60` and `overcommit_memory=0` were only read and stayed the same.
<!-- KERNEL RESULTS END -->

## Interpreting the times

`execute_ns` includes rasterisation and copy. For each frame, the worker with the longest
drawing time is identified and **that same worker's wall time and CPU
time** are compared. The difference estimates time off the CPU, including blocking
and descheduling; it does not identify which task/IRQ/lock caused it. Waiting for DDR
during execution still counts as CPU time. Small negative clock/instrumentation
differences are clamped to zero in the summary, keeping the raw CSVs.

The medians of each stage are not added up as if they belonged to the same frame.
Time to first redraw is also not the end of a transition, 60 FPS reached or
physical controller–TV latency. PREEMPT_NONE, on its own, does not prove that the kernel
is responsible for the historical full redraws of about 90 ms.

Memory will not be tuned without pressure or paging associated with the pauses. Counters
accumulated before the test include reclaim and major faults from earlier loads; the
deltas during navigation, not those totals, are the relevant evidence.

## Reproducing

The three utilities are in `tests/audit/`: `kernel_probe.py`,
`idle_keyboard.py` and `kernel_analyze.py`. They use only the Python standard library.
None goes into the ARM binary distributed by `mister/build.sh es`.

Read-only inventory, on the MiSTer:

```sh
python3 kernel_probe.py inventory --output kernel-inventory.json \
  --binary /media/fat/emulationstation/emulationstation
python3 kernel_probe.py sample --pid ES_PID --interval 1 \
  --output kernel-samples.json
```

In this audit's workspace, PowerShell (SSH by key already set up):

```powershell
Set-Location -LiteralPath 'C:\work\es-mister-audit'
wsl -d Ubuntu-26.04 -- python3 ./validation/kernel_investigation.py
```

The script uses unique run names. To repeat after the artefacts already exist,
choose new labels in the script, keeping the previous ones. Do not run
two frontends/scripts at the same time. To summarise a directory already collected:

```powershell
wsl -d Ubuntu-26.04 -- python3 ./es-mister/tests/audit/kernel_analyze.py `
  ./validation/target-results/kernel-nav-original-1 `
  ./validation/target-results/kernel-idle-original-1 `
  ./validation/target-results/kernel-nav-original-2
```

If a future kernel exposes adjustable policies: first save the driver,
limits, frequencies and governor of **each** policy, confirm `performance`
in `scaling_available_governors`, and compare original/performance/performance/
original for each scenario, using the same executable. Only the governor changes;
limits, clocks, renderer and concurrency are not changed. Restore all the
original values in `finally`/trap, including after an error, and check by reading back.
This procedure was not run nor validated on this kernel without policies.
