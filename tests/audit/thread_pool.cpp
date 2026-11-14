#include "utils/ThreadPool.h"
#include <cassert>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace Utils;
int main()
{
    std::atomic<int> count{0};ThreadPool pool("audit",0);
    for(int round=0;round<32;++round)
    {
        auto item=pool.queueWorkItem([&]{++count;});pool.wait();assert(item->isDone());
    }
    assert(count==32);pool.start();pool.start();
    auto ex=pool.queueWorkItem([]{throw std::runtime_error("expected");});
    auto unknown=pool.queueWorkItem([]{throw 7;});
    auto after=pool.queueWorkItem([&]{++count;});
    pool.waitAll({ex,unknown,after});assert(count==33);
    auto parent=pool.queueWorkItem([&]{++count;pool.queueWorkItem([&]{++count;});});
    pool.wait();assert(parent->isDone() && count==35);
    ThreadPool concurrent("producers",-2);concurrent.start();
    std::vector<std::thread> producers;
    for(int i=0;i<4;++i)producers.emplace_back([&]{for(int j=0;j<1000;++j)concurrent.queueWorkItem([&]{++count;});});
    for(auto& t:producers)t.join();concurrent.wait();assert(count==4035);
    ThreadPool cancelled("cancel",-1);
    std::promise<void> entered,release;auto released=release.get_future().share();
    auto active=cancelled.queueWorkItem([&]{entered.set_value();released.wait();});cancelled.start();
    entered.get_future().wait();
    auto queued=cancelled.queueWorkItem([]{std::abort();});
    cancelled.cancel();assert(queued->isDone() && !cancelled.isRunning());
    auto rejected=cancelled.queueWorkItem([]{std::abort();});assert(rejected->isDone());
    auto stopping=std::async(std::launch::async,[&]{cancelled.stop();});
    assert(stopping.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout);
    release.set_value();stopping.get();assert(active->isDone());
    cancelled.queueWorkItem([&]{++count;});cancelled.wait();assert(count==4036);
    WorkItemPtr abandoned;
    {ThreadPool pending;abandoned=pending.queueWorkItem([]{std::abort();});}
    abandoned->wait();assert(abandoned->isDone());
    ThreadPool except("except",-2);std::promise<void> finish;auto finishing=finish.get_future().share();
    auto excluded=except.queueWorkItem([&]{finishing.wait();});auto done=except.queueWorkItem([&]{++count;});
    except.waitAllExcept(excluded);assert(done->isDone() && !excluded->isDone());
    finish.set_value();except.wait();
    std::cout<<"pool: zero workers, 32 reuse cycles, exceptions, nested queue, 4000 concurrent jobs, cancellation, restart and exclusions passed\n";
}
