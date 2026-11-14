#pragma once
#include "SoftRaster.h"

// Optional, bounded profiling of the actual strip renderer. No file I/O while drawing.
// ES_SOFT_TRACE=/path/run.csv; ES_SOFT_TRACE_DETAIL=32 samples every 32nd redraw.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <time.h>

namespace Renderer { namespace Soft {
inline int64_t traceNow()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline int64_t traceCpu()
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0)
        return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
#endif
    return -1;
}
struct TraceCommand { int64_t ns = 0; uint32_t calls = 0; };
struct TraceWorker
{
    int64_t start = 0, drawn = 0, copied = 0, cpuDraw = 0, cpuCopy = 0, clearNs = 0;
    uint64_t visits = 0, culled = 0, parts = 0;
    CacheTrace cache;
};
struct TraceFrame
{
    uint64_t id = 0;
    int64_t at = 0;
    bool redraw = false, partial = false;
    uint32_t commands = 0;
    int64_t ui = 0, dirty = 0, execute = 0, placed = 0, published = 0, joined = 0, finished = 0;
    int64_t post = 0, diagnostics = 0, pacing = 0;
    std::array<TraceWorker, 8> worker{};
};
struct TraceCommandSample
{
    uint64_t frame, signature;
    uint32_t worker, command, calls, vertices, alpha, texW, texH;
    int64_t ns;
};
class Trace
{
public:
    void configure()
    {
        const char* p = std::getenv("ES_SOFT_TRACE");
        path = p ? p : "";
        if (path.empty()) return;
        const char* d = std::getenv("ES_SOFT_TRACE_DETAIL");
        detailEvery = d ? std::max(0, std::atoi(d)) : 0;
        frames.reserve(8192);
        samples.reserve(65536);
        answers.reserve(8192);
    }
    bool begin(bool redraw, size_t commands)
    {
        active = !path.empty() && frames.size() < 8192;
        detail = active && redraw && detailEvery > 0 && redraws++ % detailEvery == 0;
        if (!active) return false;
        frame = TraceFrame{};
        frame.id = ++sequence;
        frame.at = traceNow();
        frame.redraw = redraw;
        frame.commands = (uint32_t)commands;
        for (auto& c : commandTimes)
            if (detail) c.assign(std::min<size_t>(commands, 4096), TraceCommand{});
        return true;
    }
    void save()
    {
        if (path.empty()) return;
        FILE* out = std::fopen(path.c_str(), "w");
        if (out)
        {
            std::fprintf(out, "frame,at_ns,redraw,partial,commands,ui_ns,dirty_ns,execute_ns,placed_ns,published_ns,joined_ns,finished_ns,post_ns,diagnostics_ns,pacing_ns,worker,start_ns,drawn_ns,copied_ns,cpu_draw_ns,cpu_copy_ns,clear_ns,visits,culled,parts,cache_lookups,cache_hits,cache_misses,cache_uncached,ready_rows,sampled_rows,duplicate_rows,allocated_bytes,cache_mutex_ns,cache_grid_ns\n");
            for (const auto& f : frames) for (unsigned i = 0; i < 8; ++i)
            {
                const auto& w = f.worker[i];
                if (i && !w.start) continue;
                std::fprintf(out, "%llu,%lld,%d,%d,%u,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%lld,%lld,%lld,%lld,%lld,%lld,%llu,%llu,%llu",
                    (unsigned long long)f.id,(long long)f.at,f.redraw,f.partial,f.commands,
                    (long long)f.ui,(long long)f.dirty,(long long)f.execute,(long long)f.placed,
                    (long long)f.published,(long long)f.joined,(long long)f.finished,(long long)f.post,
                    (long long)f.diagnostics,(long long)f.pacing,i,(long long)w.start,(long long)w.drawn,
                    (long long)w.copied,(long long)w.cpuDraw,(long long)w.cpuCopy,(long long)w.clearNs,
                    (unsigned long long)w.visits,(unsigned long long)w.culled,(unsigned long long)w.parts);
                const auto& c = w.cache;
                std::fprintf(out,",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%lld,%lld\n",
                    (unsigned long long)c.lookups,(unsigned long long)c.hits,(unsigned long long)c.misses,
                    (unsigned long long)c.uncached,(unsigned long long)c.readyRows,(unsigned long long)c.sampledRows,
                    (unsigned long long)c.duplicateRows,(unsigned long long)c.allocatedBytes,(long long)c.mutexNs,(long long)c.gridNs);
            }
            std::fclose(out);
        }
        out = std::fopen((path + ".commands.csv").c_str(), "w");
        if (out)
        {
            std::fprintf(out,"frame,worker,command,signature,calls,vertices,first_alpha,tex_w,tex_h,ns\n");
            for (const auto& s : samples)
                std::fprintf(out,"%llu,%u,%u,%llu,%u,%u,%u,%u,%u,%lld\n",(unsigned long long)s.frame,
                    s.worker,s.command,(unsigned long long)s.signature,s.calls,s.vertices,s.alpha,s.texW,s.texH,(long long)s.ns);
            std::fclose(out);
        }
        out = std::fopen((path + ".input.csv").c_str(), "w");
        if (out)
        {
            std::fprintf(out,"at_ns,dispatch_ns,event_ns\n");
            for (const auto& a : answers)
                std::fprintf(out,"%lld,%lld,%lld\n",(long long)a[0],(long long)a[1],(long long)a[2]);
            std::fclose(out);
        }
    }
    void answer(double dispatchMs, double eventMs)
    {
        if (!path.empty() && answers.size() < 8192)
            answers.push_back({traceNow(), (int64_t)(dispatchMs * 1000000), (int64_t)(eventMs * 1000000)});
    }
    bool active = false, detail = false;
    TraceFrame frame;
    std::array<std::vector<TraceCommand>, 8> commandTimes;
    std::vector<TraceFrame> frames;
    std::vector<TraceCommandSample> samples;
    std::vector<std::array<int64_t, 3>> answers;
private:
    std::string path;
    uint64_t sequence = 0, redraws = 0;
    int detailEvery = 0;
};
inline thread_local TraceWorker* tracedWorker = nullptr;
inline thread_local std::vector<TraceCommand>* tracedCommands = nullptr;
} }
