#pragma once
#include <cstdint>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace Renderer { namespace Soft {
// Dense alpha edges (e.g. glyphs or stippled art) retain the established scalar loop.
// Three small windows keep dispatch cheap without scanning the row twice.
inline bool detailedAlpha(const uint32_t* row,int width)
{
    unsigned changes=0;
    for(int window=0;window<3;++window)
    {
        const int start=window==0?0:window==1?(width-9)/2:width-9;
        bool clear=(row[start]>>24)==0;
        for(int i=1;i<9;++i)
        {
            const bool next=(row[start+i]>>24)==0;
            if(next!=clear && ++changes>=3)return true;
            clear=next;
        }
    }
    return false;
}
// Return the first pixel whose alpha does not match CLEAR. A vector is skipped
// only when every one of its eight pixels matches; scalar code resolves edges.
template<bool CLEAR>
inline int alphaBoundary(const uint32_t* row,int x,int width)
{
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // Fine alpha detail (glyph edges) usually ends quickly; avoid vector setup there.
    const int probe=x+(width-x<8?width-x:8);
    while(x<probe && (((row[x]>>24)==0)==CLEAR))++x;
    if(x<probe)return x;
    const auto mask=vdupq_n_u32(0xff000000u),zero=vdupq_n_u32(0);
    for(;width-x>=8;x+=8)
    {
        const auto a=vceqq_u32(vandq_u32(vld1q_u32(row+x),mask),zero);
        const auto b=vceqq_u32(vandq_u32(vld1q_u32(row+x+4),mask),zero);
        const auto combined=CLEAR?vandq_u32(a,b):vorrq_u32(a,b);
        const auto halves=CLEAR?vand_u32(vget_low_u32(combined),vget_high_u32(combined)):
            vorr_u32(vget_low_u32(combined),vget_high_u32(combined));
        if(vget_lane_u64(vreinterpret_u64_u32(halves),0)!=(CLEAR?~uint64_t(0):uint64_t(0)))break;
    }
#endif
    while(x<width && (((row[x]>>24)==0)==CLEAR))++x;
    return x;
}
} }
