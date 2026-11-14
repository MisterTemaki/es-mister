#include "utils/FileSystemUtil.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
#include <signal.h>
namespace fs=std::filesystem;
static void write(const fs::path& p,const std::string& text) {std::ofstream f(p,std::ios::binary);f<<text;assert(f.good());}
static std::string read(const fs::path& p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main()
{
    std::string pattern=(fs::temp_directory_path()/"es-copy-test-XXXXXX").string();
    char* created=mkdtemp(pattern.data());assert(created);fs::path root(created);
    const auto parent=fs::weakly_canonical(fs::temp_directory_path());
    auto from=root/"source",to=root/"destination";
    auto copy=[](const fs::path& a,const fs::path& b){return Utils::FileSystem::copyFile(a.string(),b.string());};
    write(from,"new");write(to,"original");
    fs::permissions(to,fs::perms::owner_read|fs::perms::owner_write|fs::perms::group_read);
    assert(!copy(root/"missing",to) && read(to)=="original");
    assert(copy(from,to) && read(to)=="new");
    assert((fs::status(to).permissions()&fs::perms::all)==(fs::perms::owner_read|fs::perms::owner_write|fs::perms::group_read));
    assert(copy(from,from) && read(from)=="new");
    fs::create_hard_link(from,root/"alias");assert(copy(from,root/"alias") && read(from)=="new");
    fs::create_symlink("destination",root/"link");write(from,"updated");
    assert(copy(from,root/"link") && fs::is_symlink(root/"link") && read(to)=="updated");
    fs::create_symlink("not-yet-created",root/"dangling");
    assert(copy(from,root/"dangling") && fs::is_symlink(root/"dangling") && read(root/"not-yet-created")=="updated");
    assert(copy(from,root/"nested/sub/file") && read(root/"nested/sub/file")=="updated");
    assert(!copy(from,"/dev/full"));
    write(from,std::string(8192,'z'));write(to,"preserve-on-write-error");
    rlimit old{},limited{};assert(getrlimit(RLIMIT_FSIZE,&old)==0);limited=old;limited.rlim_cur=1024;
    auto handler=signal(SIGXFSZ,SIG_IGN);assert(setrlimit(RLIMIT_FSIZE,&limited)==0);
    const bool copied=copy(from,to);
    assert(setrlimit(RLIMIT_FSIZE,&old)==0);signal(SIGXFSZ,handler);
    assert(!copied && read(to)=="preserve-on-write-error");
    for(const auto& entry:fs::directory_iterator(root))assert(entry.path().filename().string().find(".es-copy-")!=0);
    assert(fs::weakly_canonical(root).parent_path()==parent && root.filename().string().find("es-copy-test-")==0);
    fs::remove_all(root);
    std::cout<<"copy: missing source, replacement, permissions, self/hardlink, links, nested path and real write failure passed\n";
}
