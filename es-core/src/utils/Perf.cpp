#include "utils/Perf.h"

#ifdef MISTER_PERF

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

#include <fcntl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

namespace
{
	std::atomic<uint64_t> sNews{ 0 };

	// The lines, kept in memory until ES ends: no writes to the SD card while measuring
	const size_t kBufferBytes = 16u << 20;
	char* sBuffer = nullptr;
	size_t sUsed = 0;
	bool sOverflow = false;
	bool sClosed = false; // written once (the main loop ended): what comes after (pictures still loading) left out
	std::mutex sLock;

	int64_t sStartNs = 0;
	int sIoFd = -1;
	uint64_t sLastNews = 0, sLastReads = 0, sLastWrites = 0;
	struct rusage sLastUsage = {};

	int64_t monotonicNs()
	{
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
	}

	const char* framesPath()
	{
		static const char* path = getenv("ES_PERF_FRAMES");
		return (path != nullptr && *path != 0) ? path : nullptr;
	}

	void start()
	{
		if (sBuffer != nullptr)
			return;
		sStartNs = monotonicNs();
		sBuffer = (char*)malloc(kBufferBytes);
		sIoFd = open("/proc/self/io", O_RDONLY | O_CLOEXEC);
		getrusage(RUSAGE_SELF, &sLastUsage);
		char uptime[64] = "";
		FILE* f = fopen("/proc/uptime", "r");
		if (f != nullptr)
		{
			if (fgets(uptime, sizeof(uptime), f) == nullptr)
				uptime[0] = 0;
			fclose(f);
			char* space = strchr(uptime, ' ');
			if (space != nullptr)
				*space = 0;
		}
		if (sBuffer != nullptr)
			sUsed = (size_t)snprintf(sBuffer, kBufferBytes, "# ES perf lines, t = ms since ES started (uptime %s s at t = 0)\n", uptime);
	}

	// appends a line (the lock held)
	void add(const char* format, ...) __attribute__((format(printf, 1, 2)));
	void add(const char* format, ...)
	{
		if (sBuffer == nullptr || sOverflow || sClosed)
			return;
		va_list args;
		va_start(args, format);
		const int written = vsnprintf(sBuffer + sUsed, kBufferBytes - sUsed, format, args);
		va_end(args);
		if (written < 0 || sUsed + (size_t)written >= kBufferBytes - 1)
		{
			sOverflow = true;
			return;
		}
		sUsed += (size_t)written;
	}

	double sinceStartMs(int64_t ns) { return (ns - sStartNs) / 1e6; }

	// read and write system calls so far (/proc/self/io), each reading counting itself
	void readWriteCalls(uint64_t& reads, uint64_t& writes)
	{
		reads = writes = 0;
		if (sIoFd < 0)
			return;
		char text[512];
		const ssize_t got = pread(sIoFd, text, sizeof(text) - 1, 0);
		if (got <= 0)
			return;
		text[got] = 0;
		const char* r = strstr(text, "syscr:");
		const char* w = strstr(text, "syscw:");
		if (r != nullptr)
			reads = strtoull(r + 6, nullptr, 10);
		if (w != nullptr)
			writes = strtoull(w + 6, nullptr, 10);
	}

	struct Flusher { ~Flusher() { Perf::flush(); } } sFlusher;
}

// operator new counted (malloc underneath, as the default one: the default delete frees it)
void* operator new(size_t size)
{
	sNews.fetch_add(1, std::memory_order_relaxed);
	void* p = malloc(size == 0 ? 1 : size);
	if (p == nullptr)
		throw std::bad_alloc();
	return p;
}

void* operator new[](size_t size)
{
	sNews.fetch_add(1, std::memory_order_relaxed);
	void* p = malloc(size == 0 ? 1 : size);
	if (p == nullptr)
		throw std::bad_alloc();
	return p;
}

namespace Perf
{
	bool fixedStep()
	{
		static const bool fixed = getenv("ES_PERF_FIXED_STEP") != nullptr && strcmp(getenv("ES_PERF_FIXED_STEP"), "0") != 0;
		return fixed;
	}

	static uint64_t sTurns = 0;
	static uint32_t sVirtualMs = 0;

	int nextStep()
	{
		// 1000 ms every 60 turns: 16 or 17 each, always in the same order
		const int step = (int)(((sTurns + 1) * 1000) / 60 - (sTurns * 1000) / 60);
		sTurns++;
		sVirtualMs += (uint32_t)step;
		return step;
	}

	uint32_t virtualMs() { return sVirtualMs; }

	bool enabled() { return framesPath() != nullptr; }

	int64_t nowNs() { return monotonicNs(); }

	void loopTurn(int64_t startNs, int64_t waitedNs, int64_t eventsNs, int64_t updateNs, int64_t renderNs, int64_t pacedNs, int64_t swapNs)
	{
		if (!enabled())
			return;
		std::lock_guard<std::mutex> lock(sLock);
		start();
		const uint64_t news = sNews.load(std::memory_order_relaxed);
		uint64_t reads, writes;
		readWriteCalls(reads, writes);
		// the reading of /proc/self/io is one read of its own
		const uint64_t ownReads = sLastReads == 0 ? 0 : 1;
		add("L,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu\n", sinceStartMs(startNs), (waitedNs - startNs) / 1e6,
			(eventsNs - waitedNs) / 1e6, (updateNs - eventsNs) / 1e6, (renderNs - updateNs) / 1e6, (pacedNs - renderNs) / 1e6,
			(swapNs - pacedNs) / 1e6,
			(unsigned long long)(news - sLastNews), (unsigned long long)(reads - sLastReads - std::min(ownReads, reads - sLastReads)),
			(unsigned long long)(writes - sLastWrites));
		sLastNews = news;
		sLastReads = reads;
		sLastWrites = writes;
	}

	void frameDrawn(double esMs, double rasterMs, double copyMs, uint64_t copied, uint64_t redrawn, bool partial, double answerMs)
	{
		if (!enabled())
			return;
		std::lock_guard<std::mutex> lock(sLock);
		start();
		add("F,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%d,%.3f\n", sinceStartMs(monotonicNs()), esMs, rasterMs, copyMs,
			(unsigned long long)copied, (unsigned long long)redrawn, partial ? 1 : 0, answerMs);
	}

	void pictureLoaded(double ms, int width, int height, bool mainThread)
	{
		if (!enabled())
			return;
		std::lock_guard<std::mutex> lock(sLock);
		start();
		add("T,%.3f,%.3f,%d,%d,%d\n", sinceStartMs(monotonicNs()), ms, width, height, mainThread ? 1 : 0);
	}

	void mark(const std::string& name)
	{
		if (!enabled())
			return;
		std::lock_guard<std::mutex> lock(sLock);
		start();
		struct rusage usage;
		getrusage(RUSAGE_SELF, &usage);
		auto ms = [](const struct timeval& now, const struct timeval& before) {
			return (now.tv_sec - before.tv_sec) * 1000.0 + (now.tv_usec - before.tv_usec) / 1000.0; };
		long rssKb = 0;
		FILE* f = fopen("/proc/self/statm", "r");
		if (f != nullptr)
		{
			long size = 0, resident = 0;
			if (fscanf(f, "%ld %ld", &size, &resident) == 2)
				rssKb = resident * (sysconf(_SC_PAGESIZE) / 1024);
			fclose(f);
		}
		std::string clean = name;
		for (char& c : clean)
			if (c == ',' || c == '\n')
				c = ' ';
		add("M,%.3f,%s,%.1f,%.1f,%ld\n", sinceStartMs(monotonicNs()), clean.c_str(), ms(usage.ru_utime, sLastUsage.ru_utime),
			ms(usage.ru_stime, sLastUsage.ru_stime), rssKb);
		sLastUsage = usage;
	}

	void flush()
	{
		std::lock_guard<std::mutex> lock(sLock);
		const char* path = framesPath();
		if (path == nullptr || sBuffer == nullptr || sUsed == 0 || sClosed)
			return;
		if (sOverflow)
			add("# lines left out: the buffer was full\n");
		FILE* f = fopen(path, "w");
		if (f != nullptr)
		{
			fwrite(sBuffer, 1, sUsed, f);
			if (sOverflow)
				fputs("# lines left out: the buffer was full\n", f);
			fclose(f);
		}
		sClosed = true;
	}
}

#endif
