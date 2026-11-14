#include "HttpServerWorker.h"
#include <cassert>
#include <future>
#include <iostream>

static int freePort()
{
    int sock=::socket(AF_INET,SOCK_STREAM,0);assert(sock>=0);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(::bind(sock,(sockaddr*)&addr,sizeof(addr))==0);
    socklen_t size=sizeof(addr);assert(::getsockname(sock,(sockaddr*)&addr,&size)==0);
    int port=ntohs(addr.sin_port);::close(sock);return port;
}
int main()
{
    for(int i=0;i<64;++i)
    {
        const int port=freePort();
        HttpServerWorker worker([port](httplib::Server& server,const std::atomic<bool>& stop) {
            if(!stop)server.listen("127.0.0.1",port);
        });
    }
    const int port=freePort();std::promise<void> entered,release;
    auto released=release.get_future().share();
    auto worker=std::make_unique<HttpServerWorker>([&](httplib::Server& server,const std::atomic<bool>& stop) {
        server.new_task_queue=[] {return new httplib::ThreadPool(2);};
        server.Get("/slow",[&](const httplib::Request&,httplib::Response& response) {
            entered.set_value();released.wait();response.set_content("still alive","text/plain");
        });
        if(!stop)server.listen("127.0.0.1",port);
    });
    auto client=std::async(std::launch::async,[&] {
        for(int i=0;i<100;++i)
        {
            httplib::Client client("127.0.0.1",port);
            auto result=client.Get("/slow");
            if(result){assert(result->body=="still alive");return;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::abort();
    });
    assert(entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready);
    auto destroy=std::async(std::launch::async,[&]{worker.reset();});
    assert(destroy.wait_for(std::chrono::milliseconds(600))==std::future_status::timeout);
    release.set_value();
    assert(destroy.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    destroy.get();client.get();
    HttpServerWorker failed([](httplib::Server&,const std::atomic<bool>&){throw std::runtime_error("startup");});
    failed.stop();assert(failed.failed());
    std::cout<<"server: 64 immediate starts/stops, active handler beyond stop timeout and startup exception passed\n";
}
