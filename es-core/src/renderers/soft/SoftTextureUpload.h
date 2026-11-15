#pragma once
#include <cstdint>
#include <cstddef>

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_neon.h>
#define ES_RGBA_UPLOAD_NEON 1
#endif

namespace Renderer { namespace Soft {
// RGBA bytes -> the raster's 0xAARRGGBB words. No modulation, premultiplication,
// rounding, filtering or change to the prior opaque flag.
inline bool uploadRgba(uint32_t* dst,const uint8_t* src,unsigned int count,bool opaque)
{
    unsigned int i=0;
#ifdef ES_RGBA_UPLOAD_NEON
    uint8x8_t allAlpha=vdup_n_u8(255);
    const auto d=reinterpret_cast<std::uintptr_t>(dst),s=reinterpret_cast<std::uintptr_t>(src);
    const uint64_t bytes=uint64_t(count)*4;
    const bool overlaps=d>=s?uint64_t(d-s)<bytes:uint64_t(s-d)<bytes;
    for(;!overlaps && count-i>=8;i+=8)
    {
        auto pixels=vld4_u8(src+std::size_t(i)*4);
        allAlpha=vand_u8(allAlpha,pixels.val[3]);
        const auto red=pixels.val[0];pixels.val[0]=pixels.val[2];pixels.val[2]=red;
        vst4_u8(reinterpret_cast<uint8_t*>(dst+i),pixels);
    }
    opaque=opaque && vget_lane_u64(vreinterpret_u64_u8(allAlpha),0)==~uint64_t(0);
#endif
    for(;i<count;++i)
    {
        const uint8_t* p=src+std::size_t(i)*4;
        dst[i]=(uint32_t(p[3])<<24)|(uint32_t(p[0])<<16)|(uint32_t(p[1])<<8)|p[2];
        opaque=opaque && p[3]==255;
    }
    return opaque;
}
} }
