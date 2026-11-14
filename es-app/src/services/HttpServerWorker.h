#pragma once
#include "httplib.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

// Keep the server (including active request handlers) alive until its worker has joined.
// run must check stopRequested immediately before calling Server::listen.
class HttpServerWorker
{
public:
    using Run = std::function<void(httplib::Server&, const std::atomic<bool>&)>;
    explicit HttpServerWorker(Run run)
    {
        mThread = std::thread([this, run] {
            try { run(mServer, mStopRequested); }
            catch (...) { mFailed = true; }
            mFinished = true;
        });
    }
    ~HttpServerWorker() { stop(); }
    void stop()
    {
        if (!mThread.joinable()) return;
        mStopRequested = true;
        // stop() in the bundled httplib does nothing before listen starts. Wait for either
        // cancellation of startup, bind failure, or the actual listener, then stop it once.
        while (!mFinished && !mServer.is_running())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!mFinished) mServer.stop();
        mThread.join();
    }
    bool failed() const { return mFailed; }
    HttpServerWorker(const HttpServerWorker&) = delete;
    HttpServerWorker& operator=(const HttpServerWorker&) = delete;
private:
    httplib::Server mServer;
    std::atomic<bool> mStopRequested{false}, mFinished{false}, mFailed{false};
    std::thread mThread;
};
