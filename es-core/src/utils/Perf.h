#pragma once
#ifndef ES_CORE_UTILS_PERF_H
#define ES_CORE_UTILS_PERF_H

// The measurement build's instruments (cmake -DMISTER_PERF=ON): none of this is in the normal binary, where
// the calls below are not compiled at all (each call site is inside #ifdef MISTER_PERF).
//
//   ES_PERF_FRAMES=FILE      a line a main loop turn (L), a drawn frame (F), a picture loaded (T) and a script
//                            mark (M), as CSV, written when ES ends (kept in memory until then: no writes to
//                            the SD card while measuring)
//   ES_PERF_FIXED_STEP=1     every main loop turn is 1/60 s of ES's time (16 or 17 ms, 1000 ms every 60 turns),
//                            and the test script (ES_SOFT_SCRIPT) counts its times in those turns: the same
//                            frames in the same order on every run, whatever the machine's speed (golden frames)
//
// The lines (times in ms since ES started, on CLOCK_MONOTONIC):
//   L,t,wait,events,update,render,pace,swap,allocs,reads,writes a main loop turn: its stages (wait: for events,
//                                                               the power saver's; pace: the FPS limit's delay;
//                                                               swap: the frame handed over, the bands awaited,
//                                                               the renderer's own pacing), operator new calls
//                                                               and read/write system calls during it
//   F,t,es,raster,copy,copied,redrawn,partial,answer            a frame drawn: ES's time before it, the bands'
//                                                               drawing and copy (the slowest band), pixels
//                                                               copied to the screen (at the drawing's size) and
//                                                               drawn again, drawn in part, the press it answers
//                                                               (ms, -1: none)
//   T,t,ms,width,height,thread                                  a picture loaded (read, decoded, resized): 1 on
//                                                               the main thread, 0 on a loader
//   M,t,name,user,system,rss                                    a script's "log NAME": the processor time ES used
//                                                               since the last mark (ms) and its memory (kB)

#ifdef MISTER_PERF

#include <cstdint>
#include <string>

namespace Perf
{
	bool fixedStep();
	// the next main loop turn's time (fixed step), the clock moved on by it
	int nextStep();
	// ES's time in fixed step, ms
	uint32_t virtualMs();

	bool enabled();
	int64_t nowNs();

	void loopTurn(int64_t startNs, int64_t waitedNs, int64_t eventsNs, int64_t updateNs, int64_t renderNs, int64_t pacedNs, int64_t swapNs);
	void frameDrawn(double esMs, double rasterMs, double copyMs, uint64_t copied, uint64_t redrawn, bool partial, double answerMs);
	void pictureLoaded(double ms, int width, int height, bool mainThread);
	void mark(const std::string& name);
	// the lines to ES_PERF_FRAMES (ES ending)
	void flush();
}

#endif

#endif // ES_CORE_UTILS_PERF_H
