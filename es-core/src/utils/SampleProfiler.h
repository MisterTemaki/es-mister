#pragma once
#ifndef ES_CORE_UTILS_SAMPLE_PROFILER_H
#define ES_CORE_UTILS_SAMPLE_PROFILER_H

// Where ES spends the processor, on a machine with no profiler at hand (the MiSTer). With the file
// ES_PROFILE names, SIGPROF stops ES every few milliseconds of processor time, and the address it was
// at (and its caller's, the link register on ARM) is counted, with the thread. Every 10 seconds the
// counts so far are written to the file, most frequent first; the addresses are put back to function
// names with the binary's symbols (llvm-symbolizer). Linux only; nothing is done without ES_PROFILE.

namespace SampleProfiler
{
	void start();
}

#endif
