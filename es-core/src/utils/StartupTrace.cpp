#include "utils/StartupTrace.h"

#include <atomic>
#include <condition_variable>
#include <string>
#include <vector>
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#if defined(__linux__)
#include <dirent.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace StartupTrace
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Step
		{
			std::string name;
			double since;
		};

		struct State
		{
			FILE* file = nullptr;
			std::mutex lock;
			// the lines not yet written, and the thread that writes them: on the MiSTer's SD card (mounted
			// sync) a line written and flushed where it happens held its thread (and, under the lock, every
			// other thread marking a step) ~4 ms; ~250 lines during the start
			std::string pending;
			std::condition_variable written;
			bool exiting = false, allWritten = false;
			long mainThread = 0;
			std::map<long, Step> steps;	// the step each thread is in, by thread id
			std::atomic<double> lastFrame{ 0 };
			std::atomic<bool> finished{ false };
		};

		double now()
		{
			static const Clock::time_point start = Clock::now();
			return std::chrono::duration<double>(Clock::now() - start).count();
		}

		long threadId()
		{
#if defined(__linux__)
			return (long)syscall(SYS_gettid);
#else
			return 0;
#endif
		}

		void line(State& s, const char* format, ...)
#if defined(__GNUC__)
			__attribute__((format(printf, 2, 3)))
#endif
			;

		// a line for the writer thread (with s.lock held, or before any other thread runs)
		void line(State& s, const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			const int n = vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			if (n < 0)
				return;
			if ((size_t)n < sizeof(buffer))
				s.pending.append(buffer, (size_t)n);
			else
			{
				// longer than the buffer: formatted again at its length
				std::vector<char> big((size_t)n + 1);
				va_start(args, format);
				vsnprintf(big.data(), big.size(), format, args);
				va_end(args);
				s.pending.append(big.data(), (size_t)n);
			}
			s.pending += '\n';
			s.written.notify_one();
		}

		// the lines as they come, written and flushed by this thread, outside the lock (the only one that
		// writes the file, so the lines keep their order); at the process' end what is left, then done
		void writeLines(State* s)
		{
			std::string lines;
			while (true)
			{
				bool last;
				{
					std::unique_lock<std::mutex> guard(s->lock);
					s->written.wait(guard, [s] { return !s->pending.empty() || s->exiting; });
					lines.swap(s->pending);
					last = s->exiting;
				}
				if (!lines.empty())
				{
					fwrite(lines.data(), 1, lines.size(), s->file);
					fflush(s->file);
					lines.clear();
				}
				if (last)
				{
					std::lock_guard<std::mutex> guard(s->lock);
					if (s->pending.empty())
					{
						s->allWritten = true;
						s->written.notify_all();
						return;
					}
				}
			}
		}

		State* get();

		// the process ends: the writer thread writes what is left (a second at most)
		void writeLeft()
		{
			State* s = get();
			if (s == nullptr || s->file == nullptr)
				return;
			std::unique_lock<std::mutex> guard(s->lock);
			s->exiting = true;
			s->written.notify_all();
			s->written.wait_for(guard, std::chrono::seconds(1), [s] { return s->allWritten; });
		}

#if defined(__linux__)
		std::string readFirstLine(const std::string& path)
		{
			std::string text;
			FILE* f = fopen(path.c_str(), "r");
			if (f == nullptr)
				return text;
			char buffer[512];
			if (fgets(buffer, sizeof(buffer), f) != nullptr)
				text = buffer;
			fclose(f);
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			return text;
		}

		// what /proc says about the process: each thread's state, its time and the system call it is in,
		// the files open, and the memory
		void dumpProc(State& s)
		{
			if (DIR* tasks = opendir("/proc/self/task"))
			{
				while (dirent* entry = readdir(tasks))
				{
					if (entry->d_name[0] == '.')
						continue;
					const std::string task = std::string("/proc/self/task/") + entry->d_name;
					const std::string stat = readFirstLine(task + "/stat");
					// "tid (comm) state ... utime(14) stime(15)": after the ')' the fields are 3, 4, ...
					char state = '?';
					unsigned long utime = 0, stime = 0;
					const size_t close = stat.rfind(')');
					if (close != std::string::npos)
						sscanf(stat.c_str() + close + 1, " %c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &state, &utime, &stime);

					const long tid = atol(entry->d_name);
					auto step = s.steps.find(tid);
					line(s, "            thread %s%s %c user %lu sys %lu, syscall %s%s%s",
						entry->d_name, tid == s.mainThread ? " (main)" : "", state, utime, stime,
						readFirstLine(task + "/syscall").c_str(),
						step != s.steps.end() ? ", in " : "",
						step != s.steps.end() ? step->second.name.c_str() : "");
				}
				closedir(tasks);
			}

			if (DIR* fds = opendir("/proc/self/fd"))
			{
				std::string open;
				while (dirent* entry = readdir(fds))
				{
					if (entry->d_name[0] == '.')
						continue;
					char target[512];
					const ssize_t length = readlink((std::string("/proc/self/fd/") + entry->d_name).c_str(), target, sizeof(target) - 1);
					if (length <= 0)
						continue;
					target[length] = 0;
					if (strncmp(target, "/dev/", 5) == 0 || strncmp(target, "pipe:", 5) == 0 || strncmp(target, "socket:", 7) == 0 || strncmp(target, "anon_inode:", 11) == 0)
						continue;
					open += std::string(open.empty() ? "" : ", ") + target;
				}
				closedir(fds);
				line(s, "            open: %s", open.empty() ? "-" : open.c_str());
			}

			std::string memory;
			if (FILE* f = fopen("/proc/self/status", "r"))
			{
				char buffer[256];
				while (fgets(buffer, sizeof(buffer), f) != nullptr)
					if (strncmp(buffer, "VmRSS:", 6) == 0 || strncmp(buffer, "Cpus_allowed_list:", 18) == 0)
						memory += buffer;
				fclose(f);
			}
			if (FILE* f = fopen("/proc/meminfo", "r"))
			{
				char buffer[256];
				while (fgets(buffer, sizeof(buffer), f) != nullptr)
					if (strncmp(buffer, "MemAvailable:", 13) == 0)
						memory += buffer;
				fclose(f);
			}
			for (char& c : memory)
				if (c == '\n') c = ';';
			line(s, "            %s", memory.c_str());
		}
#endif

		void watch(State* s)
		{
			double reported = 0;
			while (!s->finished.load())
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
				const double since = now() - s->lastFrame.load();
				if (since < 5)
				{
					reported = 0;
					continue;
				}
				if (since - reported < 5 || s->finished.load())
					continue;

				std::lock_guard<std::mutex> guard(s->lock);
				line(*s, "%9.3f STALL no frame for %.0f s", now(), since);
				for (auto& step : s->steps)
					line(*s, "            [%ld]%s %s, for %.1f s", step.first, step.first == s->mainThread ? " (main)" : "", step.second.name.c_str(), now() - step.second.since);
#if defined(__linux__)
				dumpProc(*s);
#endif
				reported = since;
			}
		}

		State* get()
		{
			static State* state = []
			{
				State* s = new State();
				now();
				const char* path = getenv("ES_STARTUP_TRACE");
				if (path != nullptr && *path)
					s->file = fopen(path, "w");
				if (s->file != nullptr)
				{
					s->mainThread = threadId();
					char uptime[32] = "?";
					if (FILE* f = fopen("/proc/uptime", "r"))
					{
						if (fscanf(f, "%31s", uptime) != 1)
							strcpy(uptime, "?");
						fclose(f);
					}
					line(*s, "%9.3f [%ld] trace started (uptime %s s)", now(), s->mainThread, uptime);
					s->lastFrame = now();
					std::thread(writeLines, s).detach();
					std::thread(watch, s).detach();
					atexit(writeLeft);
				}
				return s;
			}();
			return state;
		}
	}

	bool enabled()
	{
		return get()->file != nullptr;
	}

	void mark(const std::string& step, const std::string& detail)
	{
		State* s = get();
		if (s->file == nullptr)
			return;
		std::lock_guard<std::mutex> guard(s->lock);
		line(*s, "%9.3f [%ld] %s%s%s", now(), threadId(), step.c_str(), detail.empty() ? "" : ": ", detail.c_str());
	}

	void frameShown()
	{
		State* s = get();
		if (s->file != nullptr)
			s->lastFrame = now();
	}

	namespace
	{
		std::atomic<int64_t> pressedAt{ 0 }; // steady clock, ns; 0: none waiting
		int64_t steadyNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	}

	void inputArrived()
	{
		int64_t none = 0;
		pressedAt.compare_exchange_strong(none, steadyNs()); // the first press not yet answered counts
	}

	bool inputAnswered(double& ms)
	{
		const int64_t at = pressedAt.exchange(0);
		if (at == 0)
			return false;
		ms = (steadyNs() - at) / 1e6;
		return true;
	}

	int64_t takeInputPress()
	{
		return pressedAt.exchange(0);
	}

	double msSincePress(int64_t pressedAtNs)
	{
		return (steadyNs() - pressedAtNs) / 1e6;
	}

	double inputWaitingMs()
	{
		const int64_t at = pressedAt.load();
		return at == 0 ? -1.0 : (steadyNs() - at) / 1e6;
	}

	bool onMainThread()
	{
		State* s = get();
		return s->file != nullptr && threadId() == s->mainThread;
	}

	void finish()
	{
		State* s = get();
		if (s->file == nullptr || s->finished.exchange(true))
			return;
		std::lock_guard<std::mutex> guard(s->lock);
		line(*s, "%9.3f [%ld] ready", now(), threadId());
	}

	Scope::Scope(const std::string& step, const std::string& detail) : mStart(0), mPreviousSince(0), mActive(false)
	{
		State* s = get();
		if (s->file == nullptr)
			return;
		mActive = true;
		mName = detail.empty() ? step : step + ": " + detail;
		mStart = now();

		std::lock_guard<std::mutex> guard(s->lock);
		const long tid = threadId();
		auto previous = s->steps.find(tid);
		if (previous != s->steps.end())
		{
			mPrevious = previous->second.name;
			mPreviousSince = previous->second.since;
		}
		s->steps[tid] = { mName, mStart };
		line(*s, "%9.3f [%ld] > %s", mStart, tid, mName.c_str());
	}

	Scope::~Scope()
	{
		if (!mActive)
			return;
		State* s = get();
		std::lock_guard<std::mutex> guard(s->lock);
		const long tid = threadId();
		const double end = now();
		if (mPrevious.empty())
			s->steps.erase(tid);
		else
			s->steps[tid] = { mPrevious, mPreviousSince };
		line(*s, "%9.3f [%ld] < %s (%.3f s)", end, tid, mName.c_str(), end - mStart);
	}
}
