#include "ThemeInstall.h"
#include <atomic>
#include <cassert>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
static std::atomic<bool> rejectPublication{false};
extern "C" int rename(const char* from,const char* to) noexcept
{
    using Rename=int(*)(const char*,const char*);
    static auto real=(Rename)dlsym(RTLD_NEXT,"rename");
    if(rejectPublication && std::string(from).find("/repo-main")!=std::string::npos && std::string(to).find("/theme")!=std::string::npos)
    {rejectPublication=false;errno=EXDEV;return -1;}
    return real(from,to);
}
static std::string read(const std::filesystem::path& p) {std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv)
{
    assert(argc==5);namespace fs=std::filesystem;
    fs::path root=fs::weakly_canonical(argv[4]);assert(fs::is_directory(root));
    fs::create_directories(root/"theme");
    {std::ofstream f(root/"theme/theme.xml");f<<"original";}
    assert(!ThemeInstall::install(argv[2],root.string(),"theme"));
    assert(read(root/"theme/theme.xml")=="original");
    assert(!ThemeInstall::install(argv[3],root.string(),"theme"));
    assert(read(root/"theme/theme.xml")=="original");
    assert(!ThemeInstall::install(argv[1],root.string(),"../escape"));
    rejectPublication=true;
    assert(!ThemeInstall::install(argv[1],root.string(),"theme"));
    assert(!rejectPublication && read(root/"theme/theme.xml")=="original");
    assert(ThemeInstall::install(argv[1],root.string(),"theme"));
    assert(read(root/"theme/theme.xml")=="new-theme");
    for(const auto& e:fs::directory_iterator(root))assert(e.path().filename()=="theme");
    std::cout<<"theme: CRC failure, traversal rejection, forced publication failure/rollback and replacement passed\n";
}
