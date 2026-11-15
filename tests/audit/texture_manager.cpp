// Production TextureDataManager/TextureLoader, with only decoding/storage and settings isolated.
// Large logical image dimensions exercise budgets without allocating hundreds of MiB on ARM.
#include "resources/TextureDataManager.h"
#include "resources/TextureData.h"
#include "resources/TextureResource.h"
#include "Settings.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <array>
#include <future>
#undef assert
#define assert(condition) do { if (!(condition)) { std::cerr << "CHECK FAILED: " << #condition << '\n'; std::abort(); } } while (0)
static TextureDataManager* manager=nullptr;
static int maxVRAM=512;
static std::atomic<int> loads{0};
static std::atomic<bool> gateNextWrite{false};
static std::mutex writeGate;
static std::condition_variable writeEvent;
static bool writeEntered=false,writeReleased=false;
Settings::Settings():mWasChanged(false),mLoaded(false){}
Settings* Settings::getInstance(){static Settings s;return &s;}
int Settings::getInt(const std::string& name){assert(name=="MaxVRAM");return maxVRAM;}
size_t TextureResource::getTotalMemoryUsage(MemoryUsageType type){return manager?manager->getTotalMemoryUsage(type):0;}
TextureData::TextureData(bool tile,bool linear):mRequired(false),mTile(tile),mLinear(linear),mTextureID(0),mDataRGBA(nullptr),mReloadable(false),mDynamic(true),mMaxSize(0,0),mSize(0,0),mPhysicalSize(0,0),mScalable(false),mScalableMinimumSize(0,0),mIsExternalDataRGBA(false){}
TextureData::~TextureData(){releaseVRAM();releaseRAM();}
bool TextureData::initFromRGBA(unsigned char*,size_t w,size_t h,bool)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if(gateNextWrite.exchange(false))
    {
        std::unique_lock<std::mutex> gate(writeGate);writeEntered=true;writeEvent.notify_all();
        writeEvent.wait(gate,[]{return writeReleased;});
    }
    delete[] mDataRGBA;mDataRGBA=new unsigned char[1];mSize=Vector2i((int)w,(int)h);publishMemoryUsage();return true;
}
void TextureData::initFromPath(const std::string& path){mPath=path;mReloadable=true;}
bool TextureData::isLoaded(){std::lock_guard<std::mutex> lock(mMutex);return mTextureID||mDataRGBA;}
bool TextureData::isMaxSizeValid(){return true;}
bool TextureData::uploadAndBind(){std::lock_guard<std::mutex> lock(mMutex);if(!mDataRGBA)return false;mTextureID=1;delete[] mDataRGBA;mDataRGBA=nullptr;publishMemoryUsage();return true;}
bool TextureData::load(){std::this_thread::sleep_for(std::chrono::milliseconds(4));initFromRGBA(nullptr,64,64);++loads;return true;}
void TextureData::releaseVRAM(){std::lock_guard<std::mutex> lock(mMutex);mTextureID=0;publishMemoryUsage();}
void TextureData::releaseRAM(){std::lock_guard<std::mutex> lock(mMutex);delete[] mDataRGBA;mDataRGBA=nullptr;publishMemoryUsage();}
int main(int argc,char** argv)
{
    setenv("ES_TEXTURE_THREADS","2",1);
    if(argc>1 && std::strcmp(argv[1],"ram-under-limit")==0)
    {
        TextureDataManager pool;manager=&pool;
        std::array<char,6> keys{};std::array<std::shared_ptr<TextureData>,6> textures;
        for(int i=0;i<6;++i)
        {
            textures[i]=pool.add(reinterpret_cast<const TextureResource*>(&keys[i]),false,true);
            textures[i]->initFromPath("mixed-storage-fixture");textures[i]->initFromRGBA(nullptr,4096,4096);
            if(i<5)assert(textures[i]->uploadAndBind());
        }
        assert(pool.getTotalMemoryUsage(MemoryUsageType::Allocated)==384u*1024*1024);
        assert(pool.getTotalMemoryUsage(MemoryUsageType::RAM)==64u*1024*1024);
        pool.cleanupVRAM();
        assert(textures[5]->getMemoryUsage(MemoryUsageType::RAM)==64u*1024*1024);
        assert(pool.getTotalMemoryUsage(MemoryUsageType::VRAM)==320u*1024*1024);
        manager=nullptr;
        std::cout<<"texture manager: allocated above 250 MiB with RAM below limit retained; pointer bits="<<sizeof(void*)*8<<"\n";
        return 0;
    }
    const bool ramOnly=argc>1 && std::strcmp(argv[1],"ram")==0;
    if(!ramOnly)
    {
        for(int i=0;i<64;++i){TextureLoader::paused=true;TextureLoader loader(nullptr);}
        TextureLoader::paused=false;
        TextureLoader loader(nullptr);
        std::array<std::shared_ptr<TextureData>,8> entries;
        for(auto& t:entries){t=std::make_shared<TextureData>(false,true);t->initFromPath("fixture");loader.load(t);}
        for(int i=0;i<1000 && loader.getQueueSize();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        assert(loader.getQueueSize()==0 && loads==8);
        // A decoder owns the data mutex; accounting must return the previously
        // committed size without waiting for that decoder to finish.
        TextureData probe(false,true);probe.initFromRGBA(nullptr,64,64);
        gateNextWrite=true;
        std::thread writer([&]{probe.initFromRGBA(nullptr,128,128);});
        {
            std::unique_lock<std::mutex> gate(writeGate);
            assert(writeEvent.wait_for(gate,std::chrono::seconds(2),[]{return writeEntered;}));
        }
        auto reader=std::async(std::launch::async,[&]{return probe.getMemoryUsage();});
        auto status=reader.wait_for(std::chrono::milliseconds(500));
        {std::lock_guard<std::mutex> gate(writeGate);writeReleased=true;}writeEvent.notify_all();
        writer.join();auto before=reader.get();
        assert(status==std::future_status::ready && before==64u*64*4);
        assert(probe.getMemoryUsage()==128u*128*4);
        probe.releaseRAM();assert(probe.getMemoryUsage()==0 && probe.getMemoryUsage(MemoryUsageType::Estimated)==128u*128*4);
        assert(std::atomic<size_t>{}.is_lock_free());
    }
    TextureDataManager pool;manager=&pool;
    std::array<char,5> keys{};std::array<std::shared_ptr<TextureData>,5> textures;
    for(int i=0;i<5;++i)
    {
        textures[i]=pool.add(reinterpret_cast<const TextureResource*>(&keys[i]),false,true);
        textures[i]->initFromPath("reloadable-fixture");textures[i]->initFromRGBA(nullptr,4096,4096);
    }
    textures[0]->setRequired(true);
    assert(pool.getTotalMemoryUsage(MemoryUsageType::Allocated)==320u*1024*1024);
    pool.cleanupVRAM();
    auto ram=pool.getTotalMemoryUsage(MemoryUsageType::RAM);
    if(sizeof(void*)==4)assert(ram<=250u*1024*1024);
    else assert(ram==320u*1024*1024);
    assert(textures[0]->isLoaded());
    maxVRAM=96;pool.cleanupVRAM();
    assert(pool.getTotalMemoryUsage(MemoryUsageType::Allocated)<=96u*1024*1024);
    assert(textures[0]->isLoaded());
    manager=nullptr;TextureLoader::paused=false;
    std::cout<<"texture manager: paused shutdown, 64 lifetimes, queued decode completion, required residency, independent budgets passed; pointer bits="<<sizeof(void*)*8<<"\n";
}
