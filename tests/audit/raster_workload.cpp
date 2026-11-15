// Differential workload: compile against the preserved renderer and the candidate.
// All output pixels are hashed; timing covers draw only, never hashing or file output.
#include "SoftRaster.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <string>
#include <vector>
using namespace Renderer::Soft;
struct Draw { std::vector<SVertex> v; DrawState state; Topology topology = Topology::Strip; };
static Draw quad(float x,float y,float w,float h,const Texture* t,uint32_t color)
{
    Draw d;
    d.v={{x,y,0,0,color},{x,y+h,0,1,color},{x+w,y,1,0,color},{x+w,y+h,1,1,color}};
    d.state.texture=t;
    return d;
}
static Texture texture(unsigned id,int w,int h,bool opaque)
{
    Texture t; t.id=id;t.width=w;t.height=h;t.opaque=opaque;t.generation=1;
    t.pixels.resize((size_t)w*h);
    uint32_t random=id*7301+17;
    for(auto& p:t.pixels) { random=random*1664525+1013904223; p=(random&0xFFFFFF)|(opaque?0xFF000000:((random&255)<<24)); }
    findClearRuns(t);
    return t;
}
static void drawBand(Raster& raster,const std::vector<Draw>& commands,int width,int height,int strip)
{
    for(int y=raster.bandTop();y<raster.bandBottom();y+=strip)
    {
        int end=std::min(raster.bandBottom(),y+strip);
        size_t first=0;
        if(!first)raster.clearBox(0,y,width,end,0);
        for(size_t i=first;i<commands.size();++i)
        {
            const auto& d=commands[i];
            DrawState s=d.state;
            s.scissor=true;s.sx0=0;s.sx1=width;s.sy0=y;s.sy1=end;
            if(d.state.scissor) {s.sx0=std::max(s.sx0,d.state.sx0);s.sx1=std::min(s.sx1,d.state.sx1);s.sy0=std::max(s.sy0,d.state.sy0);s.sy1=std::min(s.sy1,d.state.sy1);}
            raster.draw(d.v.data(),(int)d.v.size(),d.topology,s);
        }
    }
}
int main(int argc,char** argv)
{
    const bool cold=argc>1 && std::string(argv[1])=="coldbench";
    const bool probe=argc>1 && std::string(argv[1])=="probe";
    const bool bench=probe || cold || (argc>1 && std::string(argv[1])=="bench");
    const bool extended=argc>1 && std::string(argv[1])=="composite";
    const int width=bench?1920:320,height=bench?1080:180;
    const int repetitions=probe?1:bench?12:1;
    Texture a=texture(1,bench?1280:211,bench?720:117,true);
    Texture b=texture(2,a.width,a.height,true);
    Texture picture=texture(3,width,height,false);
    Texture font=texture(4,160,32,false);
    Raster owner;owner.resize(width,height);
    Raster bands[2]; bands[0].attach(owner,0,height/2);bands[1].attach(owner,height/2,height);
    for(int scene=0;scene<(bench?3:extended?96:16);++scene)
    {
        std::vector<Draw> commands;
        commands.push_back(quad(0,0,width,height,&a,0xD9FFFFFF));
        commands.push_back(quad(scene%3,0,width,height,&b,0x25FFFFFF));
        commands.push_back(quad(0,0,width,height,&picture,0xFFFFFFFF));
        if(extended)
        {
            const unsigned alpha[]={0,1,2,7,64,127,128,199,253,254,255};
            for(int i=0;i<3;++i)
            {
                auto& d=commands[i];
                for(auto& vertex:d.v)
                {
                    vertex.color=(alpha[(scene+i*3)%11]<<24)|(scene%3==0?0xFFFFFF:scene%3==1?0:0xC34578);
                    if(i<2)vertex.x+=(scene%8-3)*13.25f;
                    if(scene&1)vertex.v=1-vertex.v;
                    if(scene&2)vertex.u=.125f+.75f*vertex.u;
                }
                if(scene&4){d.state.scissor=true;d.state.sx0=7;d.state.sx1=width-13;d.state.sy0=9;d.state.sy1=height-17;}
                if((scene&31)==31)d.v[3].color^=0x80000000;
                if((scene&31)==30)d.state.srcBlend=1,d.state.dstBlend=1;
            }
        }
        commands.push_back(quad(0,height/3.f,width,height/3.f,nullptr,0x40000000));
        for(int j=0;j<12;++j)
        {
            const uint32_t color=scene%3==0?0x00D0D0D0:0xCCDFCAD0;
            Draw d=quad(12+j*18,height/2.f,16,24,&font,color);
            if(scene%4==1) d.v[3].color=0x80FFFFFF;
            if(scene%4==2) {d.state.srcBlend=1;d.state.dstBlend=1;}
            if(scene%4==3) {d.state.scissor=true;d.state.sx0=width/4;d.state.sx1=width/2;d.state.sy0=0;d.state.sy1=height;}
            commands.push_back(std::move(d));
        }
        Draw d=quad(13.25f,17.75f,width/3.f,height/4.f,&a,0x80FFA07F);
        if(scene&1) std::swap(d.v[0].u,d.v[2].u),std::swap(d.v[1].u,d.v[3].u);
        if(scene&2) for(auto& v:d.v) v.x+=v.y*.04f;
        if(scene&4) {d.v[0].color=0x00223344;d.v[3].color=0xFF445566;}
        if(scene&8) d.topology=Topology::Fan;
        commands.push_back(std::move(d));
        std::vector<double> times;
#ifdef AUDIT_OPAQUE
        CacheTrace proof;
        if(probe)traceCache(&proof);
#endif
        for(int r=-1;r<repetitions;++r)
        {
            if(cold){a.generation=(uint64_t)(r+2);b.generation=(uint64_t)(r+2);}
            auto start=std::chrono::steady_clock::now();
            if(bench && scene==2)
            {
                std::thread worker([&]{drawBand(bands[1],commands,width,height,8);});
                drawBand(bands[0],commands,width,height,8);worker.join();
            }
            else { Raster full;full.attach(owner,0,height);drawBand(full,commands,width,height,8); }
            if(r>=0) times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            scaledFrame();
        }
#ifdef AUDIT_OPAQUE
        if(probe){traceCache(nullptr);std::printf("opaque dispatch pixels scene %d: %llu\n",scene,(unsigned long long)proof.opaqueFadePixels);}
#endif
        uint64_t hash=1469598103934665603ULL;
        for(size_t i=0;i<(size_t)width*height;++i) hash=(hash^owner.pixels()[i])*1099511628211ULL;
        std::sort(times.begin(),times.end());
        std::printf("scene %d hash %016llx median_ms %.3f\n",scene,(unsigned long long)hash,times[times.size()/2]);
    }
}
