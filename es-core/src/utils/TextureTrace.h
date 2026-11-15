#pragma once
// Optional bounded diagnostics. No logging or allocation when ES_TEXTURE_TRACE is absent.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace TextureTrace {
inline int64_t now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline long threadId() {
#if defined(__linux__)
    return (long)syscall(SYS_gettid);
#else
    return 0;
#endif
}
struct Event { int64_t at, elapsed; char kind; std::string path; long thread; };
struct Sample { int64_t at; size_t allocated, vram, ram, managed, queue, resident, decoded; uint64_t loads, repeated, uploads, evictions, active, peakActive; };
class Recorder {
public:
    explicit Recorder(const char* p):path(p) { events.reserve(4096);samples.reserve(4096); }
    void begin() { std::lock_guard<std::mutex> guard(lock);++active;if(active>peakActive)peakActive=active; }
    void loaded(const std::string& name,int64_t start)
    {
        std::lock_guard<std::mutex> guard(lock);--active;++loads;
        auto found=seen.find(name);
        if(found!=seen.end()){++found->second;++repeated;}
        else if(seen.size()<1024)seen.emplace(name,1);
        append('L',name,now()-start);
    }
    void uploaded(const std::string& name,int64_t elapsed=0) { std::lock_guard<std::mutex> guard(lock);++uploads;append('U',name,elapsed); }
    void waited(const std::string& name,int64_t elapsed) { if(elapsed>=1000000){std::lock_guard<std::mutex> guard(lock);append('M',name,elapsed);} }
    void stage(char kind,const std::string& name,int64_t elapsed) { std::lock_guard<std::mutex> guard(lock);append(kind,name,elapsed); }
    void evicted(const std::string& name) { std::lock_guard<std::mutex> guard(lock);++evictions;append('E',name,0); }
    void sample(size_t allocated,size_t vram,size_t ram,size_t managed,size_t queue,size_t resident,size_t decoded)
    {
        const auto at=now();std::lock_guard<std::mutex> guard(lock);
        if(at<next || samples.size()>=4096)return;
        next=at+250000000;samples.push_back({at,allocated,vram,ram,managed,queue,resident,decoded,loads,repeated,uploads,evictions,active,peakActive});
    }
    bool due() { std::lock_guard<std::mutex> guard(lock);return samples.size()<4096 && now()>=next; }
    void save()
    {
        std::lock_guard<std::mutex> guard(lock);
        if(FILE* out=std::fopen(path.c_str(),"w"))
        {
            std::fprintf(out,"at_ns,allocated_bytes,vram_bytes,decoded_ram_bytes,managed_entries,queue_including_active,resident_textures,decoded_textures,load_calls,repeated_path_loads,uploads,evictions,active_loads,peak_active_loads\n");
            for(const auto& s:samples)std::fprintf(out,"%lld,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu\n",(long long)s.at,s.allocated,s.vram,s.ram,s.managed,s.queue,s.resident,s.decoded,(unsigned long long)s.loads,(unsigned long long)s.repeated,(unsigned long long)s.uploads,(unsigned long long)s.evictions,(unsigned long long)s.active,(unsigned long long)s.peakActive);
            std::fclose(out);
        }
        if(FILE* out=std::fopen((path+".events.csv").c_str(),"w"))
        {
            std::fprintf(out,"at_ns,kind,elapsed_ns,thread_id,path\n");
            for(const auto& e:events)
            {
                std::fprintf(out,"%lld,%c,%lld,%ld,\"",(long long)e.at,e.kind,(long long)e.elapsed,e.thread);
                for(char c:e.path){if(c=='\"')std::fputc('"',out);std::fputc(c,out);}
                std::fputs("\"\n",out);
            }
            std::fclose(out);
        }
    }
private:
    void append(char kind,const std::string& name,int64_t elapsed){if(events.size()<4096)events.push_back({now(),elapsed,kind,name,threadId()});}
    std::string path;std::mutex lock;
    std::vector<Event> events;std::vector<Sample> samples;
    std::unordered_map<std::string,uint32_t> seen;
    int64_t next=0;uint64_t loads=0,repeated=0,uploads=0,evictions=0,active=0,peakActive=0;
};
inline Recorder* get()
{
    // Kept for process lifetime: image workers may outlive other static destructors.
    // TextureDataManager explicitly saves after joining its workers.
    static Recorder* recorder=[] {const char* p=std::getenv("ES_TEXTURE_TRACE");return p&&*p?new Recorder(p):nullptr;}();
    return recorder;
}
struct Load {
    Recorder* recorder;const std::string& path;int64_t start=0;
    explicit Load(const std::string& p):recorder(get()),path(p){if(recorder){start=now();recorder->begin();}}
    ~Load(){if(recorder)recorder->loaded(path,start);}
};
}
