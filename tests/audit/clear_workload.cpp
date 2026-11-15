#include "SoftRaster.h"
#ifdef AUDIT_CLEAR
#include "SoftAlphaScan.h"
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
using namespace Renderer::Soft;
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"CHECK FAILED: %s\n",#c);std::abort();}} while(0)
static uint32_t pixel(int x,int y,int pattern)
{
    const unsigned alpha=pattern==0?0:pattern==1?255:pattern==2?127:
        pattern==3?(x%2?255:0):pattern==4?(x%32?255:0):pattern==5?(x<33?0:255):
        pattern==6?(x>=31&&x<63?0:1):pattern==7?(y%2?255:0):pattern==8?(x%97<40?0:128):
        pattern==9?((x*37+y*19)%256):pattern==10?(x%64<31?0:255):(x%64<32?0:254);
    return (alpha<<24)|((x*1234567u+y*123u)&0xffffff);
}
int main(int argc,char** argv)
{
    if(argc>1 && std::strcmp(argv[1],"bench")==0)
    {
        for(int scene=0;scene<4;++scene)
        {
            Texture t;t.width=1920;t.height=1080;t.opaque=false;t.pixels.resize(1920*1080);
            for(int y=0;y<1080;++y)for(int x=0;x<1920;++x)
                t.pixels[y*1920+x]=scene==0?((y<270?0u:255u)<<24):scene==1?((x<400?0u:127u)<<24):
                    scene==2?0x7f123456u:pixel(x,y,3);
            std::vector<double> times;
            for(int i=0;i<23;++i)
            {
                auto start=std::chrono::steady_clock::now();findClearRuns(t);auto end=std::chrono::steady_clock::now();
                if(i>=3)times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
            }
            std::sort(times.begin(),times.end());std::printf("scene %d median_ms %.6f p95_ms %.6f at %zu runs %zu\n",scene,(times[9]+times[10])/2,times[18],t.clearAt.size(),t.clear.size());
        }
        return 0;
    }
    unsigned cases=0;
    for(int width:{0,1,7,8,31,32,33,63,64,65,95,96,127,128,1919,1920,1921})
    for(int height:{0,1,2,4})for(int pattern=0;pattern<12;++pattern)for(bool opaque:{false,true})
    {
        Texture t;t.width=width;t.height=height;t.opaque=opaque;t.pixels.resize(width*height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)t.pixels[y*width+x]=pixel(x,y,pattern);
        if(cases%7==0 && !t.pixels.empty())t.pixels.pop_back();
        t.clearAt={123,456};t.clear={123,456};findClearRuns(t);
        std::printf("case %u at",cases++);for(auto n:t.clearAt)std::printf(" %u",n);
        std::printf(" runs");for(auto n:t.clear)std::printf(" %u",n);std::puts("");
    }
#ifdef AUDIT_CLEAR
    unsigned guards=0;
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    for(int n:{0,1,7,8,9,31,32,33,63,64,65,127,128,129,1919,1920,1921})for(int pattern=0;pattern<12;++pattern)
    {
        const size_t bytes=(size_t)n*4,accessible=(bytes+page-1)/page*page;
        auto map=(uint8_t*)mmap(nullptr,accessible+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(map!=MAP_FAILED);
        CHECK(mprotect(map+accessible,page,PROT_NONE)==0);
        auto row=(uint32_t*)(map+accessible-bytes);for(int x=0;x<n;++x)row[x]=pixel(x,0,pattern);
        for(int x=0;x<=n;++x)
        {
            int zero=x,nonzero=x;while(zero<n && !(row[zero]>>24))++zero;while(nonzero<n && (row[nonzero]>>24))++nonzero;
            CHECK(alphaBoundary<true>(row,x,n)==zero);CHECK(alphaBoundary<false>(row,x,n)==nonzero);guards+=2;
        }
        CHECK(munmap(map,accessible+page)==0);
    }
    std::fprintf(stderr,"alpha boundary: %u guarded cases passed\n",guards);
#endif
    std::fprintf(stderr,"clear metadata: %u cases\n",cases);
}
