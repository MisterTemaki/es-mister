#include "ThreadPool.h"
#include "utils/Platform.h"
#include "Log.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#if WIN32
#include <Windows.h>
#include "StringUtil.h"
#elif defined(__linux__)
#include <pthread.h>
#endif

namespace Utils
{
    void WorkItem::complete()
    {
        { std::lock_guard<std::mutex> lock(mReadyMutex); mDone = true; }
        mReady.notify_all();
    }
    void WorkItem::wait() const
    {
        std::unique_lock<std::mutex> lock(mReadyMutex);
        mReady.wait(lock, [&] { return mDone.load(); });
    }
    ThreadPool::ThreadPool(const std::string& name, int byCore)
        : mRunning(false), mNumWork(0), mPoolName(name), mThreadByCore(byCore) {}
    ThreadPool::~ThreadPool() { stop(); }

    void ThreadPool::start()
    {
        std::lock_guard<std::mutex> lifecycle(mLifecycleMutex);
        if (mRunning) return;
        for (auto& thread : mThreads) if (thread.joinable()) thread.join();
        mThreads.clear();
        size_t count;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            const uint64_t requested = mThreadByCore < 0 ? (uint64_t)-(int64_t)mThreadByCore :
                (uint64_t)std::max(1u, std::thread::hardware_concurrency()) * (uint64_t)mThreadByCore;
            count = (size_t)std::max<uint64_t>(1, requested);
            mAccepting = true;
            mRunning = true;
        }
        auto worker = [this](size_t id) {
#if WIN32
            if (Utils::Platform::isWindows10())
            {
                std::wstring name = Utils::String::convertToWideString("ThreadPool::thread(" + mPoolName + ")");
                SetThreadDescription(GetCurrentThread(), name.c_str());
            }
            if (id < sizeof(DWORD_PTR) * 8)
                SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << id);
#elif defined(__linux__)
            pthread_setname_np(pthread_self(), ("pool " + mPoolName).substr(0, 15).c_str());
#endif
            for (;;)
            {
                WorkEntry entry;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    mAvailable.wait(lock, [&] { return !mRunning || !mWorkQueue.empty(); });
                    if (!mRunning) return;
                    entry = std::move(mWorkQueue.front());
                    mWorkQueue.pop();
                }
                try { entry.fn(); }
                catch (const std::exception& e) { LOG(LogError) << "ThreadPool::" << mPoolName << " : " << e.what(); }
                catch (...) { LOG(LogError) << "ThreadPool::" << mPoolName << " : unknown exception"; }
                entry.item->complete();
                { std::lock_guard<std::mutex> lock(_mutex); --mNumWork; }
                mFinished.notify_all();
            }
        };
        try
        {
            mThreads.reserve(count);
            for (size_t i = 0; i < count; ++i) mThreads.emplace_back(worker, i);
        }
        catch (...)
        {
            cancel();
            for (auto& thread : mThreads) if (thread.joinable()) thread.join();
            mThreads.clear();
            throw;
        }
    }
    WorkItemPtr ThreadPool::queueWorkItem(work_function work)
    {
        auto item = std::make_shared<WorkItem>();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!mAccepting) { item->complete(); return item; }
            mWorkQueue.push({std::move(work), item});
            mAllItems.push_back(item);
            ++mNumWork;
        }
        mAvailable.notify_one();
        return item;
    }
    void ThreadPool::wait()
    {
        start();
        std::unique_lock<std::mutex> lock(_mutex);
        mFinished.wait(lock, [&] { return mNumWork.load() == 0; });
        mAllItems.clear();
    }
    void ThreadPool::wait(work_function update, int delay)
    {
        start();
        std::unique_lock<std::mutex> lock(_mutex);
        while (mNumWork.load())
        {
            lock.unlock(); update(); lock.lock();
            mFinished.wait_for(lock, std::chrono::milliseconds(std::max(1, delay)), [&] { return mNumWork.load() == 0; });
        }
        mAllItems.clear();
    }
    void ThreadPool::cleanupDoneItems()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        mAllItems.erase(std::remove_if(mAllItems.begin(), mAllItems.end(),
            [](const WorkItemPtr& item) { return item->isDone(); }), mAllItems.end());
    }
    void ThreadPool::waitAll(std::initializer_list<WorkItemPtr> items)
    {
        start();
        for (const auto& item : items) if (item) item->wait();
        cleanupDoneItems();
    }
    void ThreadPool::waitAllExcept(WorkItemPtr item) { waitAllExcept({item}); }
    void ThreadPool::waitAllExcept(std::initializer_list<WorkItemPtr> excluded)
    {
        start();
        std::vector<WorkItemPtr> waiting;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (const auto& item : mAllItems)
                if (std::find(excluded.begin(), excluded.end(), item) == excluded.end()) waiting.push_back(item);
        }
        for (const auto& item : waiting) item->wait();
        cleanupDoneItems();
    }
    void ThreadPool::cancel()
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            mRunning = false;
            mAccepting = false;
            while (!mWorkQueue.empty())
            {
                mWorkQueue.front().item->complete();
                mWorkQueue.pop();
                --mNumWork;
            }
        }
        mAvailable.notify_all();
        mFinished.notify_all();
    }
    void ThreadPool::stop()
    {
        std::lock_guard<std::mutex> lifecycle(mLifecycleMutex);
        cancel();
        for (auto& thread : mThreads) if (thread.joinable()) thread.join();
        mThreads.clear();
        std::lock_guard<std::mutex> lock(_mutex);
        mAllItems.clear();
        mAccepting = true;
    }
}
