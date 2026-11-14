#include "utils/Similarity.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
using std::transform;
#include "similarity_reference.inc"
int main(int argc,char**)
{
    using Utils::String::jaroWinkler;
    std::mt19937 rng(1937);
    for(int test=0;test<1240;++test)
    {
        size_t n=test<1200?rng()%80+1:1100+rng()%100;
        size_t m=test<1200?rng()%80+1:1100+rng()%100;
        std::string a(n,'A'),b(m,'A');const char letters[]="ABCDabcd1234";
        for(auto& c:a)c=letters[rng()%12];for(auto& c:b)c=letters[rng()%12];
        if(test%7==0)b=a;
        bool sensitive=test%2==0;
        const auto old=referenceJaro(a,b,sensitive),now=jaroWinkler(a,b,sensitive);
        if(old!=now){std::fprintf(stderr,"score mismatch %.9f %.9f sizes %zu %zu\n",old,now,n,m);return 1;}
    }
    assert(jaroWinkler("","")==0 && jaroWinkler("x","X",false)==1);
    assert(std::fabs(jaroWinkler("MARTHA","MARHTA")-.9611111f)<.000001f);
    assert(std::fabs(jaroWinkler("DWAYNE","DUANE")-.84f)<.000001f);
    assert(std::isfinite(jaroWinkler(std::string(65001,'A'),"A")));
    auto longA=std::string(100000,'A'),longB=longA;longB.back()='B';
    assert(jaroWinkler(longA,longB)>.99f && jaroWinkler(longA,longB)<1);
    assert(std::isfinite(jaroWinkler(std::string("a\0\0\0b",5),std::string("a\0\0\0c",5))));
    std::printf("similarity: 1240 exact reference scores, bounded prefix and 100000-byte metadata passed\n");
    if(argc>1)
    {
        std::vector<std::string> names;
        for(int i=0;i<2000;++i)names.push_back("Super Mario World "+std::to_string(i)+" (USA)");
        volatile float total=0;
        for(int mode=0;mode<2;++mode)
        {
            auto start=std::chrono::steady_clock::now();
            for(const auto& name:names)total+=mode?jaroWinkler("Super MarioWorld 1",name,false):referenceJaro("Super MarioWorld 1",name,false);
            auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::printf("search workload %d: %.3f ms (2000 pairs)\n",mode,ms);
        }
    }
}
