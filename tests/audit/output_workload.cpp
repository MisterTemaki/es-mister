#include "../../es-core/src/renderers/soft/SoftOutput.cpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
using namespace Renderer::Soft;
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"CHECK FAILED: %s\n",#c);std::abort();}} while(0)
int main(int argc,char** argv)
{
    if(argc>1 && std::strcmp(argv[1],"bench")==0)
    {
        setenv("ES_SOFT_OUTPUT","fbdev",1);
        if(!std::getenv("ES_SOFT_TTY"))setenv("ES_SOFT_TTY","/dev/tty0",1);
        auto output=createOutput();int width=0,height=0;std::string error;
        CHECK(output->open(width,height,error));CHECK(width==1920 && height==1080);
        CHECK(output->describe().find("32 bpp")!=std::string::npos && output->describe().find("converted")==std::string::npos);
        if(std::strcmp(std::getenv("ES_SOFT_TTY"),"none")!=0)
        {
            int tty=open(std::getenv("ES_SOFT_TTY"),O_RDONLY|O_CLOEXEC),mode=0;CHECK(tty>=0);
            CHECK(ioctl(tty,KDGETMODE,&mode)==0 && mode==KD_GRAPHICS);close(tty);
        }
        std::vector<uint32_t> pixels(width*height);for(size_t i=0;i<pixels.size();++i)pixels[i]=0xff000000u|((i*1234567u)&0xffffffu);
        std::mutex lock;std::condition_variable event;unsigned phase=0,finished=0;bool stop=false;
        std::thread worker([&]{unsigned seen=0;std::unique_lock<std::mutex> guard(lock);for(;;){event.wait(guard,[&]{return stop || phase!=seen;});if(stop)break;seen=phase;guard.unlock();output->presentRect(pixels.data(),width,height,0,height/2,width,height);guard.lock();finished=seen;event.notify_all();}});
        std::vector<double> times;
        for(int i=0;i<26;++i)
        {
            auto begin=std::chrono::steady_clock::now();
            {std::lock_guard<std::mutex> guard(lock);++phase;event.notify_all();}
            output->presentRect(pixels.data(),width,height,0,0,width,height/2);
            {std::unique_lock<std::mutex> guard(lock);event.wait(guard,[&]{return finished==phase;});}
            auto end=std::chrono::steady_clock::now();if(i>=6)times.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        }
        {std::lock_guard<std::mutex> guard(lock);stop=true;event.notify_all();}worker.join();
        int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);CHECK(fd>=0);fb_fix_screeninfo info{};CHECK(ioctl(fd,FBIOGET_FSCREENINFO,&info)==0);
        auto map=(const uint8_t*)mmap(nullptr,info.smem_len,PROT_READ,MAP_SHARED,fd,0);CHECK(map!=MAP_FAILED);__sync_synchronize();
        size_t wrong=0;
        for(int y=0;y<height;++y)
        {
            const auto row=(const uint32_t*)(map+(size_t)y*info.line_length);
            if(std::memcmp(row,pixels.data()+(size_t)y*width,(size_t)width*4)==0)continue;
            for(int x=0;x<width;++x)if(row[x]!=pixels[(size_t)y*width+x])
            {
                if(wrong<8)std::fprintf(stderr,"framebuffer mismatch x=%d y=%d expected=%08x actual=%08x\n",x,y,pixels[(size_t)y*width+x],row[x]);
                ++wrong;
            }
        }
        CHECK(munmap((void*)map,info.smem_len)==0);close(fd);output->close();
        CHECK(wrong==0);
        std::sort(times.begin(),times.end());std::printf("native_1080p_2_workers median_ms %.6f p95_ms %.6f max_ms %.6f framebuffer_pixels_checked %zu\n",(times[9]+times[10])/2,times[18],times[19],pixels.size());
        return 0;
    }
    unsigned cases=0;
    for(size_t n:{0,1,2,3,4,7,8,15,16,31,32,33,63,64,65,127,128,129,255,256,257,7679,7680,7681})
    for(size_t sourceOffset:{0,1,2,3,4,8,16,31})for(size_t destinationOffset:{0,1,2,3,4,8,16,31})
    {
        std::vector<uint8_t> source(n+64),destination(n+64,0x55);
        for(size_t i=0;i<source.size();++i)source[i]=(uint8_t)(i*79+37);
        copyBytes(destination.data()+destinationOffset,source.data()+sourceOffset,n);
        CHECK(std::memcmp(destination.data()+destinationOffset,source.data()+sourceOffset,n)==0);
        for(size_t i=0;i<destinationOffset;++i)CHECK(destination[i]==0x55);
        for(size_t i=destinationOffset+n;i<destination.size();++i)CHECK(destination[i]==0x55);
        ++cases;
    }
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    for(size_t n:{0,1,31,32,63,64,65,127,128,129,255,256,257,7679,7680,7681})
    {
        const size_t accessible=(n+page-1)/page*page;
        auto source=(uint8_t*)mmap(nullptr,accessible+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        auto destination=(uint8_t*)mmap(nullptr,accessible+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(source!=MAP_FAILED && destination!=MAP_FAILED);
        CHECK(mprotect(source+accessible,page,PROT_NONE)==0 && mprotect(destination+accessible,page,PROT_NONE)==0);
        auto from=source+accessible-n;auto to=destination+accessible-n;for(size_t i=0;i<n;++i)from[i]=(uint8_t)(i*37+19);
        copyBytes(to,from,n);CHECK(std::memcmp(to,from,n)==0);
        CHECK(munmap(source,accessible+page)==0 && munmap(destination,accessible+page)==0);++cases;
    }
    std::printf("copy bytes: %u alignment/tail/guarded cases passed\n",cases);
}
