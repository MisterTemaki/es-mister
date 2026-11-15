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
//   StartupTrace::onMainThread()                      the caller is ES's main thread (the one that draws)
//   StartupTrace::inputArrived()                      a press of the user's (main.cpp), and
//   StartupTrace::inputAnswered(ms)                   after a frame is shown: true, with the time since the
//                                                     press, for the first frame shown after one (the renderer)
//   StartupTrace::inputWaitingMs()                    how long a press has waited (-1: none)
//
// The trace's first line has the time since the machine started (/proc/uptime), as the MiSTer scripts'
// lines do, to put ES's steps on the same clock as theirs.

#include <string>
#include <cstdint>

namespace StartupTrace
{
	struct FrameTimes { int64_t wait=0, input=0, update=0, record=0, limit=0, systemCursor=-1, systemAnimation=-1; };
	int64_t frameClock();
	void frameTimes(const FrameTimes& times);
	FrameTimes frameTimes();
	bool enabled();
	void mark(const std::string& step, const std::string& detail = "");
	void frameShown();
	void mainLoopEntered();
	void interactiveFrameShown();
	void finish();
	bool onMainThread();
	void inputArrived(uint32_t queuedMs = 0);
	bool inputAnswered(double& ms, double* eventMs = nullptr);
	double inputWaitingMs(); // how long the press not yet answered has waited; -1: none

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
