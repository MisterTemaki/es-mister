# MiSTer at 1080p: diagnosis before optimising

13/11/2026. Analysis of the local code and the existing logs. No renderer, video parameter, prefetch or band size was changed. The installed application kept running. Only analysis references, diffs and this document were created.

**Main conclusion**

The 150–160 ms are the slowest worker's drawing time, not the full time between pressing a button and seeing the response. Each worker draws its region of the screen in several bands of 8 rows; that number is not the time of a single 8-row band. There is strong evidence of cost in the large layers with transparency, pixel reads/writes and the scaled-picture paths. However, the logs do not yet separate the kernels' actual work, repeated per-band preparation, mutex waits, scheduling delay and synchronisation. The next change must first measure these components on the real path of **8-row bands, with PLD=256**, keeping the same pixels and the same theme.

**Versions compared and preservation of the existing work**

- The installed executable has SHA-256 `253a15744237071c9bc136d94666957cc4567fbbe8ebf349ce1a10cd86daf404`, identical to the binary in `es-mister-7ad64ca-18/emulationstation/emulationstation`.
- The local repository in `D:/FPGA/Frontend/Mistercera/es-mister` is clean at commit `fcd0450c1c5cef720ce08690c217f2aab40da620`.
- All the rendering files present in the ZIP `es-mister-src-5b5c7bc.zip` match that repository. Among the ZIP's files, only `mister/LEIA-ME.txt` and `mister/package.sh` differ from the local repository; the repository also has the benchmark added later.
- The relevant difference from the installed package is described by patch 29, reported commit `3e0121f0eea22fcd8650d70e6b295e0a04a58bde`. It adds prefetch and NEON copies with prefetch and changes the defaults from 16 to 8 rows. The patch was applied successfully **only to a temporary analysis reference**. The resulting reference is in `installed-reference/`; the diff against the ZIP is in `zip-vs-installed.diff`.
- The binary/package identity is confirmed. The source reference was rebuilt from the repository and the corresponding patch; no bit-for-bit rebuild was done to prove the origin of every byte of the executable.

| Item | ZIP / local sources before patch 29 | Package 18 / configuration observed |
|---|---|---|
| Output resolution | Port allows the native framebuffer | 1920×1080, 32 bpp |
| Bands (`STRIP`, `ES_SOFT_STRIP`) | Default 16 rows | **8 rows** |
| Span prefetch | Absent | **256 bytes** ahead; destination and direct source |
| Copies of opaque/scaled pixels | `memcpy` on the changed paths | `copyPixels`, NEON with PLD |
| Copy to the framebuffer | NEON without asking for the source ahead | NEON with source prefetch |
| Drawing threads | Up to two by default | Two in the logs |
| Partial redraw and scaled cache | Already exist | Preserved; not new in patch 29 |

It would make no sense to use the ZIP without these adjustments as a supposed performance update. They must make up the reference for any later measurement.

**What the logs really show**

The hardware observed has two ARMv7 processors with NEON, about 491 MiB of RAM visible to Linux and a 1920×1080×32 framebuffer. The SSH query found ES with nine threads and about 207 MiB of RSS at that moment. These figures do not, by themselves, show memory pressure or excessive contention between threads.

The file `es-mister-stats.txt` aggregates several versions and sessions over the day. I did not use the old averages as the reference for package 18. After the installation at 19:30, the local logs available contain six windows of 600 iterations: three with some drawing and three without a redraw.

| Record | Measure observed | Interpretation |
|---|---|---|
| `es-startup.log`, 45.858 s | ES 6 ms; drawing **157 ms**, 100% of the screen | Example of navigation where ES's preparation is small and drawing dominates. |
| `es-startup.log`, 46.282 s | ES 6 ms; drawing **156 ms**, 100% of the screen | Repetition of the full-redraw cost. |
| `es-startup.log`, 61.926 s | ES 5 ms; drawing **298 ms**, 100% of the screen | There are peaks beyond 150–160 ms. The kernel/cache/scheduling breakdown is missing. |
| Statistics 19:35:07 | 295 redraws; average drawing 77.3 ms, maximum 283.0 ms; average copy 5.47 ms | Mixes full frames, partial frames and the start of the session. It does not mean the cost of a typical full frame. |
| Same window, inputs | 59 responses recorded; median **215 ms**, largest value and sample p99 **864 ms** | An indicator of slow interaction; small sample and first phase subject to dumps. It is not a sustained p99 nor the screen's physical latency. |
| Statistics 19:35:25 | 382 redraws, 380 partial; average drawing 15.6 ms, maximum 297.9 ms | A different regime, with 10% of the screen redrawn on average. One recorded response of 216 ms does not establish a percentile. |
| Statistics 19:36:05 | One partial redraw: drawing **2.0 ms**, copy **0.03 ms**, 5 thousand pixels copied | The small-change path can already be fast. |

`frames=600` includes iterations in which the picture did not change. `frame 4.8 ms` with `0 drawn` is not 208 FPS of new pictures. The recorded frame time excludes the limiter's final wait, and the monitor keeps showing the last picture.

Start-up is also a separate measurement: first picture noted at 1.651 s after the process started; `load systems` takes 6.402 s; the logical `ready` mark comes at 8.983 s; there are still heavy frames at 9.800 and 10.323 s. The wrapper had already used about 8.95 s between its start and the call to ES, according to the uptimes. The first response noted was 420 ms, without saying when the user decided to press the button. That is not enough to measure opening up to the first useful action completed.

**Stages that may explain the cost**

| Stage / location | Evidence or hypothesis | How to separate the measurement |
|---|---|---|
| Interface preparation: `main`, input, `Window`, views and `record` | Confirmed separately: ES usually 5–43 ms in the examples; entering the NES list took **765 ms**. There are initialisations and picture loads on the main thread. | Mark the event's dequeue, end of input, `update`, preparation of each view, `render/record`, synchronous upload/decoding and entry into `swapBuffers`. Record the ID of the action and of the state change. |
| Region detection: `findParts` | The LCS can visit up to about a million pairs. When more than 70% of the tiles are marked, the code picks the full frame. Its time is part of neither `esMs` nor the per-band timers. | Time the whole function and count previous/current commands, LCS pairs, tiles, rectangles and the fallback reason. Identify which changed command covers the screen: a legitimate background/fade or excessive invalidation. |
| Per-band preparation: `executePart`, `Raster::draw/drawQuad` | A 1080-row frame goes through about **135–136 strips of 8 rows**, spread over the two workers. Geometry, clipping, path selection and cache lookup are repeated in the strips each command crosses. The number of calls does not prove they dominate the time. | Count visits, commands discarded by bounding box, actual draw calls, vertices revisited, clears and stencil resets; measure preparation separately from the spans in a few selected samples. |
| Clears and large layers | `executePart` clears the strip before drawing all the commands. Dump 1 also contains a full-screen black fill and a full-screen opaque copy. | Measure clear, fills and opaque copies as separate categories. Count bytes touched and overlap; only then evaluate redundant writes with proof of coverage/opacity. |
| Backgrounds, alpha and scaled pictures | In dump 4, two 1280×720 backgrounds stretched to 1080p cost **26.152 and 32.496 ms** in the serial replay; another 1:1 layer cost **16.958 ms**. Together they are 57% of that dump's 132.438 serial ms. | Group the real path's times by command ID and path chosen: sampling, copy, modulation, alpha and direct bilinear. Record pixels and bytes, without lowering quality or removing effects. |
| Scale cache: `scaledFor`, `scaledRow`, `scaledFrame` | There is a global mutex, a default budget of 40 MiB, grid creation and rows filled on demand. Each strip may look up the cache; when there is no room, a structure without persistent pixels is returned. Thrashing or contention are hypotheses, not facts proven by the logs. | Count hit/miss/eviction, reason for not keeping, bytes, rows ready/generated/repeated, allocations and mutex wait time. Separate a cold and a warm cache. The `bilinear` category also counts the path with a scaled picture in the cache; it does not mean all those pixels were interpolated again. |
| Text and shadows | Dump 1 repeats the same text at many offsets. In dump 4, commands 39–43 show zero alpha in the first vertex's colour and still add up to **8.198 ms** in the serial replay. They are candidates for work with no visual effect; the dump does not print the other vertices' colour. | Count and time text and shadows separately on the real path; check the alpha of all vertices. Any later discarding or caching requires checking blend/stencil and pixel equality. |
| Workers, locks and scheduling | Both drawing timers include the time their thread was descheduled. Waking the worker and preparing bands are outside them. Two CPUs do not guarantee that shared memory and preparation scale. | Record job publication, each worker's real start, end of drawing, end of copying and return of the join; use per-thread CPU clocks and context switches to separate scheduled work from descheduling. Memory waits inside the CPU also count as CPU time: PMU/perf would be needed to tell them apart from useful execution. |
| Output: `presentBand` → `presentRect` → `copyBytes` | Dumps: copy between **10.5 and 21.7 ms**. The isolated benchmark gives about **16.3–16.8 ms with PLD**. The code already uses the native format when compatible. | Measure per worker the time, bytes, pitch, width, native/conversion mode and the conversion path's time when used. Keep the prefetch. Do not confuse writing to the mmap with confirmation of physical presentation on the TV. |
| Diagnosis and pacing | Dumps redo the frame after drawing it; input and `frameMs` are measured after that replay. The limiter may reserve another interval of about 16.67 ms after an overrun, through the path that resets `nextFrame=now`. | Measure dumps, log writing and the actual duration of `sleep_until` separately, plus the delay relative to the deadline. Use a control run without dumps and without profiling; do not change the limiter before this confirmation. |

**Memory evidence: relevant, without premature attribution**

A 1920×1080 ARGB frame takes 8,294,400 bytes, about 7.91 MiB. Delivering a full frame at 60 Hz already requires about 498 MB/s of pixels copied to the framebuffer, before counting reads of the source, layers and other accesses. This is a volume calculation, not a measurement of the DDR's physical traffic.

The existing benchmark, nine repetitions and median, shows:

| Synthetic operation | Configuration A | Configuration B | Note |
|---|---:|---:|---|
| One layer with fade, one thread | 48.7 ms, no PLD | 28.1 ms, PLD 256 | Shows the benefit of prefetch. Two threads with PLD gave 28.9 ms: no gain in this kernel. |
| NEON copy to the screen, one thread | 20.3 ms, no PLD | 16.3 ms, with PLD | One copy is already close to the full 16.67 ms budget of 60 Hz. It does not prove an absolute physical limit. |
| Synthetic frame, 16-row strips without PLD → 8-row strips with PLD, one thread | 166.9 ms | 96.9 ms | Changes two factors; it does not allow attributing the gain only to switching from 16 to 8. |
| Same frame, PLD fixed, 16-row → 8-row strips, one thread | 101.4 ms | 96.9 ms | Isolates the difference in strip size in this test. |
| Synthetic frame, 8-row strips drawn separately and copied, PLD 256 | 87.9 ms, one thread | 88.4 ms, two threads | Suggests a shared limitation in this load. The implementation with a separate buffer is not the same as the app's. |

The benchmark has few kernels, and its own data and sequence; it contains neither the whole UI, text, cache, list creation nor the frontend's workers. Its times cannot be subtracted from the 150–160 ms to find an exact "remainder". I did not run a new benchmark in this analysis.

**Why the dumps do not add up**

`dumpCommands` creates another raster and replays the whole list on one thread. It does not call `executeBand` nor reproduce the original 8-row strips and concurrency. In addition, it can take advantage of the scale cache filled in the real frame and changes that cache's state; it writes the results to a file.

| Dump | Real drawing reported in the header | Sum of the commands replayed on one thread | Real copy reported |
|---|---:|---:|---:|
| 1 | 126.2 ms | 84.155 ms | 12.6 ms |
| 2 | 127.5 ms | 62.607 ms, printed total | 21.7 ms |
| 3 | 206.7 ms | 77.981 ms, printed total | 10.5 ms |
| 4 | 125.2 ms | 132.438 ms, printed total | 11.1 ms |

Adding up the dump's rounded lines may differ by a few microseconds from the printed total. The fact that the serial replay is sometimes faster **does not show** that removing a thread would fix the app. Nor does it allow attributing the difference to waiting: order, cache, clipping, number of calls and execution conditions all changed.

In the current implementation, `drawing = max(drawing_worker_i)` and `copy = max(copy_worker_i)`. One worker's drawing and the other's copy overlap. Therefore `max(drawing)+max(copy)` is not the exact duration of `execute()` and may combine the maxima of different workers. Waiting to wake, preparation and part of the wrap-up also need markers of their own.

**Measurement plan before the functional change**

1. Fix the reference: package 18's binary identified by hash, 1920×1080, PLD 256, STRIP 8, two threads, same theme, library, initial cursor and effects. Keep the effective environment, not only the `.ini`.
2. Make a normal pass without dumps and without a profiler. Through the wrapper, use `SLOW_DUMPS=0`, `BENCH=0`, `PROFILE=0`, and make sure no inherited diagnostic variables remain. Do not use `ES_SOFT_BENCH=0` as a substitute: the code turns that mode on by the variable's existence. Compare a pass with profiling separately. No quality change.
3. Use separate scripts: idle; system navigation with fade; first entry into a list; repeating that list and systems already visited; prolonged scrolling. Separate the first run, the session's first opening and a warm cache. Do not clear global caches or real data.
4. At the first level of instrumentation, capture only per-frame/worker timestamps: event dequeue, end of input, end of update, end of record, start/end of `findParts`, band preparation, job publication, start/end of each drawing/copy, join, end of `execute`, maintenance of the cache (`scaledFrame`) and of the previous list, diagnosis, start/end of pacing. Use the same monotonic clock (`steady_clock`, already used by the renderer) and relate them all to the same `frame_id` and to the action that changed the cursor or the screen. For per-thread CPU, use `CLOCK_THREAD_CPUTIME_ID`; record a lack of support instead of substituting process time.
5. The per-worker timeline accounts for the real time: `execute_wall = execute_exit - execute_entry`. Compare that wall time with each worker's complete intervals from job publication; do not add up overlapping stages. Separate the thread's actual time from time off the CPU.
6. Only then detail the dominant commands: timers aggregated by command/category, visits per strip and cache counters. No `printf`, allocation or clock per pixel/NEON group. Use pre-allocated per-thread buffers and write after the measurement; sample the detail and measure the instrumentation's own cost against the control.
7. For input response, start from the event's timestamp, also measure the time in the queue and record the first screen **corresponding to the action**. The current code marks arrival when taking the event out and closes the first pending input at any redraw; this may group commands or attribute an unrelated change to the input. The final write to the framebuffer is a useful software boundary, but latency to photons depends on external observation or confirmation of scanout.
8. Repeat the sequences at least three times; keep the individual times. Report median, p95/p99 when there are enough samples, maximum, full/partial frames separately, deadline delay, memory, cache misses and queues. To evaluate p99, aim for at least 1,000 useful responses, without mixing all the scenarios into a single distribution.
9. First evaluate reducing accesses and work proven redundant: opaque layers that make clears useless, commands with no effect, repeated preparation and cache. Any later optimisation must pass a pixel comparison (`ES_SOFT_DIRTY_CHECK=1`) in a run separate from the performance one, because that mode draws the full reference and increases the load.

It is not possible to break down all the components retroactively with the current binary and logs. The points above define the minimum instrumentation needed before modifying the algorithm. The data already collected supports the priorities, but not a promise of 1080p at 60 FPS nor an exact attribution of the 150–160 ms to each sub-stage.

**Evidence files**

`comparison.json`: hashes and differences; `zip-vs-installed.diff`: the delta from the ZIP to the reference with patch 29; `log-analysis.json`: a verifiable extraction of the records; `logs/`: copies of the files already collected; `installed-reference/`: reference sources used in the reading. The audit's pending functional fixes remain recorded in the previous report and were not marked as resolved by this analysis.
