// Include the production backend to inspect its output without adding test APIs to the app.
#include "../../es-core/src/renderers/Renderer_Soft.cpp"
#include <cassert>
#include <iostream>

// Only external services unused by this test are isolated. Recording, dirty regions, workers,
// rasterization, texture upload, stencil, output and frame pacing use production implementations.
Settings::Settings() : mWasChanged(false), mLoaded(false) {}
Settings* Settings::getInstance() { static Settings settings; return &settings; }
bool Settings::getBool(const std::string& name) { assert(name == "VSync"); return true; }
namespace StartupTrace {
void frameShown() {} bool enabled() { return false; }
bool inputAnswered(double&, double*) { return false; } double inputWaitingMs() { return -1; }
void mark(const std::string&, const std::string&) {}
}
namespace Renderer {
void drawRect(float,float,float,float,unsigned int,Blend::Factor,Blend::Factor) { std::abort(); }
std::vector<Vertex> createRoundRect(float,float,float,float,float,unsigned int) { std::abort(); }
namespace Soft {
bool Script::load(const std::string&) { std::abort(); }
void Script::frame(const uint32_t*,int,int,bool) { std::abort(); }
}
}
static std::vector<Renderer::Vertex> box(unsigned color)
{
    using Renderer::Vertex;
    return {Vertex({0,0},{0,0},color),Vertex({0,32},{0,1},color),
            Vertex({48,0},{1,0},color),Vertex({48,32},{1,1},color)};
}
static void snapshot(const char* label)
{
    auto& raster=Renderer::S().raster;
    uint64_t hash=1469598103934665603ULL;
    for(int i=0;i<raster.width()*raster.height();++i) hash=(hash^raster.pixels()[i])*1099511628211ULL;
    std::cout<<label<<" "<<std::hex<<hash<<std::dec<<"\n";
}
static void uniform(uint32_t expected)
{
    auto& raster=Renderer::S().raster;
    for(int i=0;i<raster.width()*raster.height();++i) assert(raster.pixels()[i]==expected);
}
int main()
{
    setenv("ES_SOFT_OUTPUT","null",1);setenv("ES_SOFT_SIZE","48x32",1);
    setenv("ES_SOFT_FPS","0",1);setenv("ES_SOFT_THREADS","2",1);
    setenv("ES_SOFT_STRIP","8",1);setenv("ES_SOFT_STATS","0",1);
    Renderer::SoftRenderer renderer;
    renderer.createContext();renderer.setSwapInterval();
    auto red=box(0xFF0000FF),clear=box(0x00123456),blue=box(0xFFFF0000);
    renderer.drawTriangleStrips(red.data(),4);renderer.swapBuffers();uniform(0xFFFF0000);snapshot("red");
    renderer.swapBuffers();uniform(0xFFFF0000);snapshot("no-draw-keeps-red");
    renderer.drawTriangleStrips(clear.data(),4);renderer.swapBuffers();uniform(0);snapshot("transparent-only-clears");
    renderer.swapBuffers();uniform(0);snapshot("no-draw-keeps-clear");
    unsigned char white[4]={255,255,255,255};
    const auto tex=renderer.createTexture(Renderer::Texture::RGBA,true,false,1,1,white);
    renderer.bindTexture(tex);
    renderer.drawTriangleStrips(blue.data(),4);renderer.drawTriangleStrips(clear.data(),4);
    renderer.swapBuffers();uniform(0xFF0000FF);snapshot("blue-with-transparent-texture");
    auto mixed=clear;mixed[3].col=0xFF445566;
    renderer.drawTriangleStrips(mixed.data(),4);renderer.swapBuffers();snapshot("mixed-alpha-kept");
    renderer.drawTriangleStrips(clear.data(),4,Renderer::Blend::ONE,Renderer::Blend::ONE);
    renderer.swapBuffers();snapshot("additive-zero-alpha-kept");
    assert((Renderer::S().raster.pixels()[0]&0xFFFFFF)!=0);
    renderer.bindTexture(0);
    auto mask=clear;for(auto& v:mask) v.pos=Vector2f(v.pos.x()*.5f,v.pos.y()*.5f);
    renderer.setStencil(mask.data(),4);
    renderer.drawTriangleStrips(red.data(),4);renderer.disableStencil();renderer.swapBuffers();snapshot("transparent-stencil-kept");
    assert(Renderer::S().raster.pixels()[0]==0xFFFF0000);
    assert(Renderer::S().raster.pixels()[47+31*48]==0);
    renderer.drawTriangleStrips(clear.data(),4);renderer.destroyContext();
    renderer.createContext();renderer.swapBuffers();assert(!Renderer::S().hadCommands);
    renderer.destroyContext();
}
