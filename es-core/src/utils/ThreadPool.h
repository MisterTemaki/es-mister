#ifndef __THREADPOOL
#define __THREADPOOL

#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <initializer_list>
#include <string>
#include <condition_variable>

namespace Utils
{
	class WorkItem
	{
	public:
		WorkItem() : mDone(false) {}

		bool isDone() const { return mDone.load(); }
		void wait() const;

	private:
		friend class ThreadPool;
		std::atomic<bool> mDone;
		mutable std::mutex mReadyMutex;
		mutable std::condition_variable mReady;
		void complete();
	};

	using WorkItemPtr = std::shared_ptr<WorkItem>;

	class ThreadPool
	{
	public:
		typedef std::function<void(void)> work_function;

		ThreadPool(const std::string& poolName = "", int threadByCore = 2);
		~ThreadPool();

		void start();
		WorkItemPtr queueWorkItem(work_function work);

		void wait();
		void wait(work_function work, int delay = 50);

		void waitAll(std::initializer_list<WorkItemPtr> items);

		void waitAllExcept(WorkItemPtr excluded);
		void waitAllExcept(std::initializer_list<WorkItemPtr> excluded);

		// Cancel queued work (its items become done); running functions finish normally.
		void cancel();
		void stop();

		bool isRunning() const { return mRunning.load(); }

	private:
		std::atomic<bool> mRunning;
		bool mAccepting = true; // protected by _mutex; start() reopens a cancelled pool

		struct WorkEntry
		{
			work_function   fn;
			WorkItemPtr     item;
		};

		std::queue<WorkEntry> mWorkQueue;
		std::atomic<size_t> mNumWork;
		std::mutex _mutex;
		std::mutex mLifecycleMutex;
		std::condition_variable mAvailable, mFinished;
		std::vector<std::thread> mThreads;
		std::string mPoolName;
		int mThreadByCore;

		std::vector<std::shared_ptr<WorkItem>> mAllItems;
		void cleanupDoneItems();
	};
}

#endif
