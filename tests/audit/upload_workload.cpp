// The reference function is extracted verbatim from cba2b66 by build_upload.py.
#include "SoftTextureUpload.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif
extern bool referenceUpload(uint32_t*,const uint8_t*,unsigned int,bool);
using Fn=bool(*)(uint32_t*,const uint8_t*,unsigned int,bool);
static bool candidate(uint32_t* d,const uint8_t* s,unsigned int n,bool o){return Renderer::Soft::uploadRgba(d,s,n,o);}
static void check(bool ok){if(!ok){std::fputs("UPLOAD DIFFERENTIAL FAILED\n",stderr);std::abort();}}
static uint32_t randomValue=1234567;
static uint8_t nextByte(){randomValue=randomValue*1664525u+1013904223u;return (uint8_t)(randomValue>>24);}
int main(int argc,char** argv)
{
    const bool bench=argc>1 && std::strcmp(argv[1],"bench")==0;
    if(bench)
    {
        const unsigned n=1920*1080;
        std::vector<uint8_t> src((size_t)n*4);std::vector<uint32_t> dst(n);
        for(auto& p:src)p=nextByte();
        for(int alpha: {255,127})
        {
            for(unsigned i=0;i<n;++i)src[(size_t)i*4+3]=(uint8_t)alpha;
            for(int mode: {0,1,1,0})
            {
                Fn fn=mode?candidate:referenceUpload;std::vector<double> times;
                for(int r=-2;r<20;++r)
                {
                    auto begin=std::chrono::steady_clock::now();
                    bool opaque=fn(dst.data(),src.data(),n,true);
                    asm volatile(""::"r"(dst.data()):"memory");
                    auto end=std::chrono::steady_clock::now();
                    check(opaque==(alpha==255));
                    if(r>=0)times.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
                }
                uint64_t hash=1469598103934665603ULL;for(auto p:dst)hash=(hash^p)*1099511628211ULL;
                std::sort(times.begin(),times.end());
                std::printf("alpha %d mode %d median_ms %.3f p95_ms %.3f hash %016llx\n",alpha,mode,(times[9]+times[10])/2,times[18],(unsigned long long)hash);
            }
        }
        return 0;
    }
    size_t cases=0;
    for(unsigned n: {0u,1u,2u,3u,4u,7u,8u,9u,15u,16u,17u,31u,32u,33u,63u,64u,65u,127u,128u,129u,511u,512u,513u,1023u,1024u,1025u})
    for(unsigned alignment=0;alignment<16;++alignment)
    for(int pattern=0;pattern<8;++pattern)
    for(bool initial: {false,true})
    {
        std::vector<uint8_t> bytes((size_t)n*4+32);for(auto& b:bytes)b=nextByte();
        uint8_t* src=bytes.data()+alignment;
        for(unsigned i=0;i<n;++i)src[(size_t)i*4+3]=pattern==0?255:pattern==1?0:pattern==2?127:pattern==3?128:pattern==4?254:pattern==5?(i==n/2?0:255):pattern==6?(i==n-1?254:255):nextByte();
        std::vector<uint32_t> a(n+34,0xDEADBEEF),b=a;
        bool expected=referenceUpload(a.data()+17,src,n,initial);
        bool actual=candidate(b.data()+17,src,n,initial);
        check(actual==expected && a==b);++cases;
    }
    for(unsigned n: {0u,1u,7u,8u,9u,16u,17u,64u,65u})
    for(int shift=-2;shift<=2;++shift)
    for(unsigned offset=0;offset<8;++offset)
    {
        std::vector<uint32_t> a(n+100);for(auto& w:a)w=uint32_t(nextByte())*0x01010101u;
        auto b=a;
        auto srcA=reinterpret_cast<uint8_t*>(a.data()+17)+offset;
        auto srcB=reinterpret_cast<uint8_t*>(b.data()+17)+offset;
        bool expected=referenceUpload(a.data()+17+shift,srcA,n,true);
        check(candidate(b.data()+17+shift,srcB,n,true)==expected && a==b);++cases;
    }
#if defined(__linux__)
    // Buffers ending at PROT_NONE catch reads or writes beyond a short SIMD tail.
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    for(unsigned n: {0u,1u,7u,8u,9u,15u,16u,17u,1023u,1024u,1025u})
    {
        const size_t accessible=std::max(page,((size_t)n*4+page-1)/page*page);
        auto a=(uint8_t*)mmap(nullptr,accessible+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        auto b=(uint8_t*)mmap(nullptr,accessible+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        check(a!=MAP_FAILED && b!=MAP_FAILED);
        check(mprotect(a+accessible,page,PROT_NONE)==0 && mprotect(b+accessible,page,PROT_NONE)==0);
        auto src=a+accessible-(size_t)n*4;auto dst=(uint32_t*)(b+accessible-(size_t)n*4);
        for(size_t i=0;i<(size_t)n*4;++i)src[i]=nextByte();
        std::vector<uint32_t> expected(n);
        bool opaque=referenceUpload(expected.data(),src,n,true);
        check(candidate(dst,src,n,true)==opaque);
        for(unsigned i=0;i<n;++i)check(dst[i]==expected[i]);
        check(munmap(a,accessible+page)==0 && munmap(b,accessible+page)==0);++cases;
    }
#endif
    std::printf("RGBA upload: %zu independent cases, opacity, byte offsets, guards and protected-page tails passed\n",cases);
}
