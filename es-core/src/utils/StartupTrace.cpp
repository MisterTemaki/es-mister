#include "utils/StartupTrace.h"

#include <atomic>
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

		void line(State& s, const char* format, ...)
		{
			va_list args;
			va_start(args, format);
			vfprintf(s.file, format, args);
			va_end(args);
			fputc('\n', s.file);
			fflush(s.file);
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
					line(*s, "%9.3f [%ld] trace started", now(), s->mainThread);
					s->lastFrame = now();
					std::thread(watch, s).detach();
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
