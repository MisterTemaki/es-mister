#pragma once
#ifndef ES_CORE_UTILS_STARTUP_TRACE_H
#define ES_CORE_UTILS_STARTUP_TRACE_H

// Where ES's time goes while it starts, for machines where that is slow (the MiSTer's ARM). Each step
// is written, with the seconds since ES started and the thread, to the file ES_STARTUP_TRACE names;
// nothing is done without it. Until finish(), a watchdog thread notes when the main thread has not
// shown a frame for five seconds: the step every thread is in, and what /proc says about them (the
// system call each one is in, the files open, the memory left), so a long wait names its cause.
//
//   StartupTrace::Scope scope("load system", name);   a step's start, and its end with its duration
//   StartupTrace::mark("ready");                       a moment
//   StartupTrace::frameShown();                       the main thread showed a frame (the renderer)
//   StartupTrace::finish();                           ES is up: the watchdog stops

#include <string>

namespace StartupTrace
{
	bool enabled();
	void mark(const std::string& step, const std::string& detail = "");
	void frameShown();
	void finish();

	class Scope
	{
	public:
		Scope(const std::string& step, const std::string& detail = "");
		~Scope();
	private:
		std::string mName;
		std::string mPrevious;
		double mStart;
		double mPreviousSince;
		bool mActive;
	};
}

#endif
