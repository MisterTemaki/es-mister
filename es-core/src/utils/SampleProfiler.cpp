#include "utils/SampleProfiler.h"

#if defined(__linux__)

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace SampleProfiler
{
	namespace
	{
		struct Sample
		{
			uintptr_t pc;
			uintptr_t caller;
			int32_t thread;
		};

		constexpr uint32_t kRing = 1 << 16;         // samples kept between two writes (4 ms each: over 4 minutes)
		constexpr int kIntervalUs = 4000;

		Sample gRing[kRing];
		std::atomic<uint32_t> gWritten{ 0 };
		int32_t gMainThread = 0;
		const char* gPath = nullptr;

		void onSample(int, siginfo_t*, void* context)
		{
			const int saved = errno;
			const ucontext_t* uc = (const ucontext_t*)context;
			Sample s;
#if defined(__arm__)
			s.pc = (uintptr_t)uc->uc_mcontext.arm_pc;
			s.caller = (uintptr_t)uc->uc_mcontext.arm_lr;
#elif defined(__aarch64__)
			s.pc = (uintptr_t)uc->uc_mcontext.pc;
			s.caller = (uintptr_t)uc->uc_mcontext.regs[30];
#elif defined(__x86_64__)
			s.pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
			s.caller = 0;
#else
			s.pc = 0;
			s.caller = 0;
#endif
			s.thread = (int32_t)syscall(SYS_gettid);
			const uint32_t index = gWritten.fetch_add(1, std::memory_order_relaxed);
			gRing[index & (kRing - 1)] = s;
			errno = saved;
		}

		struct Counts
		{
			uint64_t total = 0, main = 0;
			std::unordered_map<uintptr_t, uint32_t> pc, mainPc, caller;
			std::map<int32_t, uint64_t> threads;
		};

		void top(FILE* out, const char* title, const std::unordered_map<uintptr_t, uint32_t>& counts, uint64_t total, size_t limit)
		{
			std::vector<std::pair<uintptr_t, uint32_t>> sorted(counts.begin(), counts.end());
			std::sort(sorted.begin(), sorted.end(), [](const std::pair<uintptr_t, uint32_t>& a, const std::pair<uintptr_t, uint32_t>& b) { return a.second > b.second; });
			fprintf(out, "%s\n", title);
			for (size_t i = 0; i < sorted.size() && i < limit; i++)
				fprintf(out, "  0x%08lx %6u %5.1f%%\n", (unsigned long)sorted[i].first, sorted[i].second, total ? 100.0 * sorted[i].second / total : 0.0);
		}

		void writer()
		{
			Counts counts;
			uint32_t read = 0;
			const auto started = std::chrono::steady_clock::now();
			for (;;)
			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
				const uint32_t written = gWritten.load(std::memory_order_relaxed);
				if (written - read > kRing)
					read = written - kRing; // more than the ring holds: the oldest are gone
				for (; read != written; read++)
				{
					const Sample& s = gRing[read & (kRing - 1)];
					counts.total++;
					counts.pc[s.pc]++;
					if (s.caller != 0)
						counts.caller[s.caller]++;
					counts.threads[s.thread]++;
					if (s.thread == gMainThread)
					{
						counts.main++;
						counts.mainPc[s.pc]++;
					}
				}

				FILE* out = fopen(gPath, "w");
				if (out == nullptr)
					continue;
				const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
				fprintf(out, "ES profile: %llu samples of %d ms of processor time in %.0f s of running\n",
					(unsigned long long)counts.total, kIntervalUs / 1000, seconds);
				fprintf(out, "threads (main %d):", gMainThread);
				for (auto& thread : counts.threads)
					fprintf(out, " %d %.1f%%", thread.first, counts.total ? 100.0 * thread.second / counts.total : 0.0);
				fprintf(out, "\n");
				top(out, "all threads, where:", counts.pc, counts.total, 120);
				top(out, "main thread, where:", counts.mainPc, counts.main, 80);
				top(out, "all threads, called from:", counts.caller, counts.total, 80);
				fclose(out);
			}
		}
	}

	void start()
	{
		gPath = getenv("ES_PROFILE");
		if (gPath == nullptr || *gPath == 0)
			return;

		gMainThread = (int32_t)syscall(SYS_gettid);
		std::thread(writer).detach();

		struct sigaction action;
		memset(&action, 0, sizeof(action));
		action.sa_sigaction = onSample;
		action.sa_flags = SA_SIGINFO | SA_RESTART;
		sigemptyset(&action.sa_mask);
		sigaction(SIGPROF, &action, nullptr);

		struct itimerval timer;
		timer.it_interval.tv_sec = 0;
		timer.it_interval.tv_usec = kIntervalUs;
		timer.it_value = timer.it_interval;
		setitimer(ITIMER_PROF, &timer, nullptr);
	}
}

#else

namespace SampleProfiler
{
	void start() {}
}

#endif
