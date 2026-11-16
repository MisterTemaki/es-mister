#include "SoftRaster.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define SOFT_NEON 1
#endif

namespace Renderer
{
	namespace Soft
	{
		namespace
		{
			// Renderer::Blend::Factor
			enum
			{
				F_ZERO = 0, F_ONE = 1, F_SRC_COLOR = 2, F_ONE_MINUS_SRC_COLOR = 3, F_SRC_ALPHA = 4, F_ONE_MINUS_SRC_ALPHA = 5,
				F_DST_COLOR = 6, F_ONE_MINUS_DST_COLOR = 7, F_DST_ALPHA = 8, F_ONE_MINUS_DST_ALPHA = 9
			};

			// (int)std::floor(f) and (int)std::ceil(f), the same for every value, without the library call: the A9 has no
			// rounding instruction, and floorf/ceilf move the value to the ARM registers and back. The conversion
			// truncates toward zero, one step down (up) when that was above (below) the value. Out of the int's range,
			// and NaN, as before.
			template<typename F>
			inline int floorInt(F f)
			{
				if (!(f > (F)-2147483648.0 && f < (F)2147483648.0))
					return (int)std::floor(f);
				const int t = (int)f;
				return (F)t > f ? t - 1 : t;
			}

			template<typename F>
			inline int ceilInt(F f)
			{
				if (!(f > (F)-2147483648.0 && f < (F)2147483648.0))
					return (int)std::ceil(f);
				const int t = (int)f;
				return (F)t < f ? t + 1 : t;
			}

			enum TexMode { TEX_NONE = 0, TEX_NEAREST = 1, TEX_LINEAR = 2, TEX_DIRECT = 3 };
			enum ModMode { MOD_NONE = 0, MOD_CONST = 1, MOD_VARY = 2 };
			enum BlendMode { BLEND_NORMAL = 0, BLEND_ADD = 1, BLEND_GENERIC = 2 };

			inline uint32_t div255(uint32_t x) { return (x + 128 + ((x + 128) >> 8)) >> 8; }

			// two 8-bit products in bits 0-15 and 16-31, each divided by 255
			inline uint32_t div255x2(uint32_t x) { return ((x + 0x00800080 + ((x >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF; }

			inline uint32_t lerp8(uint32_t a, uint32_t b, uint32_t w) // w: 0..256
			{
				const uint32_t iw = 256 - w;
				const uint32_t rb = (((a & 0x00FF00FF) * iw + (b & 0x00FF00FF) * w) >> 8) & 0x00FF00FF;
				const uint32_t ag = (((a >> 8) & 0x00FF00FF) * iw + ((b >> 8) & 0x00FF00FF) * w) & 0xFF00FF00;
				return rb | ag;
			}

			inline uint32_t modulate(uint32_t t, uint32_t c)
			{
				const uint32_t a = div255((t >> 24) * (c >> 24));
				const uint32_t r = div255(((t >> 16) & 0xFF) * ((c >> 16) & 0xFF));
				const uint32_t g = div255(((t >> 8) & 0xFF) * ((c >> 8) & 0xFF));
				const uint32_t b = div255((t & 0xFF) * (c & 0xFF));
				return (a << 24) | (r << 16) | (g << 8) | b;
			}

			inline uint32_t desaturate(uint32_t c, int sat) // sat: 0..256
			{
				const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
				const int gray = (r * 77 + g * 150 + b * 29) >> 8;
				const int nr = gray + (((r - gray) * sat) >> 8);
				const int ng = gray + (((g - gray) * sat) >> 8);
				const int nb = gray + (((b - gray) * sat) >> 8);
				return (c & 0xFF000000) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
			}

			inline uint32_t blendNormal(uint32_t s, uint32_t d)
			{
				const uint32_t a = s >> 24;
				if (a == 255)
					return s;
				if (a == 0)
					return d;
				const uint32_t ia = 255 - a;
				const uint32_t rb = div255x2((s & 0x00FF00FF) * a + (d & 0x00FF00FF) * ia);
				const uint32_t ag = div255x2(((s >> 8) & 0x00FF00FF) * a + ((d >> 8) & 0x00FF00FF) * ia) << 8;
				return rb | ag;
			}

			inline uint32_t blendAdd(uint32_t s, uint32_t d) // ONE, ONE: saturated
			{
				uint32_t out = 0;
				for (int shift = 0; shift < 32; shift += 8)
				{
					const uint32_t v = ((s >> shift) & 0xFF) + ((d >> shift) & 0xFF);
					out |= (v > 255 ? 255 : v) << shift;
				}
				return out;
			}

			inline uint32_t factor(int f, uint32_t sc, uint32_t sa, uint32_t dc, uint32_t da)
			{
				switch (f)
				{
				case F_ZERO: return 0;
				case F_ONE: return 255;
				case F_SRC_COLOR: return sc;
				case F_ONE_MINUS_SRC_COLOR: return 255 - sc;
				case F_SRC_ALPHA: return sa;
				case F_ONE_MINUS_SRC_ALPHA: return 255 - sa;
				case F_DST_COLOR: return dc;
				case F_ONE_MINUS_DST_COLOR: return 255 - dc;
				case F_DST_ALPHA: return da;
				case F_ONE_MINUS_DST_ALPHA: return 255 - da;
				}
				return 0;
			}

			inline uint32_t blendGeneric(uint32_t s, uint32_t d, int sf, int df)
			{
				const uint32_t sa = s >> 24, da = d >> 24;
				uint32_t out = 0;
				for (int shift = 0; shift < 32; shift += 8)
				{
					const uint32_t sc = (s >> shift) & 0xFF, dc = (d >> shift) & 0xFF;
					const uint32_t v = div255(sc * factor(sf, sc, sa, dc, da) + dc * factor(df, sc, sa, dc, da));
					out |= (v > 255 ? 255 : v) << shift;
				}
				return out;
			}

			// into[i] = lerp8(src[x0s[i]], src[x1s[i]], fxs[i]) for n pixels: a texture row sampled across, the first
			// step of a bilinear sample (the rows path's and the scaled pictures'). NEON: four pixels at a time, the
			// same sums (each channel a * (256 - w) + b * w, at most 255 * 256, shifted down 8), so the same pixels.
			inline void sampleAcross(uint32_t* into, const uint32_t* src, const int32_t* x0s, const int32_t* x1s, const uint16_t* fxs, int n)
			{
				int i = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
				const uint16x8_t full = vdupq_n_u16(256);
				for (; i + 4 <= n; i += 4)
				{
					uint32x4_t a = vdupq_n_u32(src[x0s[i]]), b = vdupq_n_u32(src[x1s[i]]);
					a = vsetq_lane_u32(src[x0s[i + 1]], a, 1); b = vsetq_lane_u32(src[x1s[i + 1]], b, 1);
					a = vsetq_lane_u32(src[x0s[i + 2]], a, 2); b = vsetq_lane_u32(src[x1s[i + 2]], b, 2);
					a = vsetq_lane_u32(src[x0s[i + 3]], a, 3); b = vsetq_lane_u32(src[x1s[i + 3]], b, 3);
					// each pixel's weight on its four bytes: w0 w0 w0 w0 w1 w1 w1 w1 | w2 ... w3
					const uint16x4_t w4 = vld1_u16(fxs + i);
					const uint16x4x2_t pairs = vzip_u16(w4, w4);
					const uint16x4x2_t first = vzip_u16(pairs.val[0], pairs.val[0]);
					const uint16x4x2_t second = vzip_u16(pairs.val[1], pairs.val[1]);
					const uint16x8_t wlo = vcombine_u16(first.val[0], first.val[1]);
					const uint16x8_t whi = vcombine_u16(second.val[0], second.val[1]);
					const uint8x16_t a8 = vreinterpretq_u8_u32(a), b8 = vreinterpretq_u8_u32(b);
					const uint16x8_t lo = vmlaq_u16(vmulq_u16(vmovl_u8(vget_low_u8(a8)), vsubq_u16(full, wlo)), vmovl_u8(vget_low_u8(b8)), wlo);
					const uint16x8_t hi = vmlaq_u16(vmulq_u16(vmovl_u8(vget_high_u8(a8)), vsubq_u16(full, whi)), vmovl_u8(vget_high_u8(b8)), whi);
					vst1q_u8((uint8_t*)(into + i), vcombine_u8(vshrn_n_u16(lo, 8), vshrn_n_u16(hi, 8)));
				}
#endif
				for (; i < n; i++)
					into[i] = lerp8(src[x0s[i]], src[x1s[i]], fxs[i]);
			}

			// out = lerp8(top, bottom, w) for n pixels: a bilinear sample's last step, on two rows already
			// interpolated across
			inline void lerpRows(uint32_t* out, const uint32_t* top, const uint32_t* bottom, uint32_t w, int n)
			{
				if (w == 0)
				{
					memcpy(out, top, (size_t)n * 4);
					return;
				}
				int i = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
				const uint16x8_t wv = vdupq_n_u16((uint16_t)w), iwv = vdupq_n_u16((uint16_t)(256 - w));
				for (; i + 4 <= n; i += 4)
				{
					const uint8x16_t a = vld1q_u8((const uint8_t*)(top + i)), b = vld1q_u8((const uint8_t*)(bottom + i));
					const uint16x8_t lo = vmlaq_u16(vmulq_u16(vmovl_u8(vget_low_u8(a)), iwv), vmovl_u8(vget_low_u8(b)), wv);
					const uint16x8_t hi = vmlaq_u16(vmulq_u16(vmovl_u8(vget_high_u8(a)), iwv), vmovl_u8(vget_high_u8(b)), wv);
					vst1q_u8((uint8_t*)(out + i), vcombine_u8(vshrn_n_u16(lo, 8), vshrn_n_u16(hi, 8)));
				}
#endif
				for (; i < n; i++)
					out[i] = lerp8(top[i], bottom[i], w);
			}

			// How far ahead the memory is asked for (PLD) in the spans and copies, in pixels (ES_SOFT_PLD, in
			// bytes, default 256; 0: not asked for). Measured on the DE10-Nano (es-mister-bench): a faded
			// layer 1.7 times faster, the copy to the screen 1.25; from 64 to 1024 bytes nearly the same. Only a
			// hint to the processor: no pixel changes.
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
			int aheadPixels()
			{
				static const int ahead = []
				{
					const char* value = getenv("ES_SOFT_PLD");
					const int bytes = value != nullptr ? atoi(value) : 256;
					return std::max(0, std::min(4096, bytes)) / 4;
				}();
				return ahead;
			}
#endif

			// n pixels copied, the source asked for ahead (memcpy elsewhere)
			inline void copyPixels(uint32_t* to, const uint32_t* from, int n)
			{
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
				const int ahead = aheadPixels();
				int i = 0;
				for (; i + 16 <= n; i += 16)
				{
					if (ahead)
						__builtin_prefetch(from + i + ahead, 0);
					const uint32x4_t a = vld1q_u32(from + i), b = vld1q_u32(from + i + 4);
					const uint32x4_t c = vld1q_u32(from + i + 8), d = vld1q_u32(from + i + 12);
					vst1q_u32(to + i, a);
					vst1q_u32(to + i + 4, b);
					vst1q_u32(to + i + 8, c);
					vst1q_u32(to + i + 12, d);
				}
				if (i < n)
					memcpy(to + i, from + i, (size_t)(n - i) * 4);
#else
				memcpy(to, from, (size_t)n * 4);
#endif
			}

			struct Tex
			{
				const uint32_t* px = nullptr;
				int w = 1, h = 1;
				bool repeat = false;
			};

			inline int wrapCoord(int x, int size, bool repeat)
			{
				if (repeat)
				{
					if ((unsigned)x < (unsigned)size)
						return x; // inside the texture: no division (the A9 divides in software)
					x %= size;
					return x < 0 ? x + size : x;
				}
				return x < 0 ? 0 : (x >= size ? size - 1 : x);
			}

			inline uint32_t fetchNearest(const Tex& t, int u, int v)
			{
				const int x = wrapCoord(u >> 16, t.w, t.repeat);
				const int y = wrapCoord(v >> 16, t.h, t.repeat);
				return t.px[y * t.w + x];
			}

			inline uint32_t fetchLinear(const Tex& t, int u, int v)
			{
				const int ix = u >> 16, iy = v >> 16;
				const uint32_t fx = ((uint32_t)u >> 8) & 0xFF, fy = ((uint32_t)v >> 8) & 0xFF;
				const int x0 = wrapCoord(ix, t.w, t.repeat), x1 = wrapCoord(ix + 1, t.w, t.repeat);
				const int y0 = wrapCoord(iy, t.h, t.repeat), y1 = wrapCoord(iy + 1, t.h, t.repeat);
				const uint32_t* r0 = t.px + y0 * t.w;
				const uint32_t* r1 = t.px + y1 * t.w;
				const uint32_t top = lerp8(r0[x0], r0[x1], fx);
				const uint32_t bottom = lerp8(r1[x0], r1[x1], fx);
				return lerp8(top, bottom, fy);
			}

			// One span's attributes at its first pixel, and their steps per pixel
			struct Span
			{
				int u = 0, du = 0, v = 0, dv = 0;           // 16.16 texels
				int r = 0, g = 0, b = 0, a = 0;             // 16.16, 0..255
				int dr = 0, dg = 0, db = 0, da = 0;
				uint32_t color = 0xFFFFFFFF;                // MOD_CONST
				int saturation = 256;                       // 256: untouched
				int srcBlend = F_SRC_ALPHA, dstBlend = F_ONE_MINUS_SRC_ALPHA;
			};

			inline uint32_t clampChannel(int v)
			{
				v >>= 16;
				return (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
			}

			template<int TEX, int MOD, int BLEND>
			void spanT(uint32_t* dst, const uint8_t* mask, int maskTest, int n, const Tex& tex, Span s)
			{
				const uint32_t* direct = nullptr;
				if (TEX == TEX_DIRECT)
					direct = tex.px + (s.v >> 16) * tex.w + (s.u >> 16);

				for (int i = 0; i < n; i++)
				{
					bool draw = true;
					if (maskTest != 0)
						draw = (maskTest == 1) == (mask[i] == 1);

					if (draw)
					{
						uint32_t c;
						if (TEX == TEX_NONE)
						{
							c = MOD == MOD_VARY ? ((clampChannel(s.a) << 24) | (clampChannel(s.r) << 16) | (clampChannel(s.g) << 8) | clampChannel(s.b)) : s.color;
						}
						else
						{
							uint32_t t;
							if (TEX == TEX_DIRECT)
								t = direct[i];
							else if (TEX == TEX_NEAREST)
								t = fetchNearest(tex, s.u, s.v);
							else
								t = fetchLinear(tex, s.u, s.v);

							if (s.saturation < 256)
								t = desaturate(t, s.saturation);

							if (MOD == MOD_NONE)
								c = t;
							else if (MOD == MOD_CONST)
								c = modulate(t, s.color);
							else
								c = modulate(t, (clampChannel(s.a) << 24) | (clampChannel(s.r) << 16) | (clampChannel(s.g) << 8) | clampChannel(s.b));
						}

						if (BLEND == BLEND_NORMAL)
							dst[i] = blendNormal(c, dst[i]);
						else if (BLEND == BLEND_ADD)
							dst[i] = blendAdd(c, dst[i]);
						else
							dst[i] = blendGeneric(c, dst[i], s.srcBlend, s.dstBlend);
					}

					if (TEX == TEX_NEAREST || TEX == TEX_LINEAR)
					{
						s.u += s.du;
						s.v += s.dv;
					}
					if (MOD == MOD_VARY)
					{
						s.r += s.dr; s.g += s.dg; s.b += s.db; s.a += s.da;
					}
				}
			}

			typedef void (*SpanFn)(uint32_t*, const uint8_t*, int, int, const Tex&, Span);

			template<int TEX, int MOD>
			SpanFn pickBlend(int blend)
			{
				switch (blend)
				{
				case BLEND_NORMAL: return &spanT<TEX, MOD, BLEND_NORMAL>;
				case BLEND_ADD: return &spanT<TEX, MOD, BLEND_ADD>;
				default: return &spanT<TEX, MOD, BLEND_GENERIC>;
				}
			}

			template<int TEX>
			SpanFn pickMod(int mod, int blend)
			{
				switch (mod)
				{
				case MOD_NONE: return pickBlend<TEX, MOD_NONE>(blend);
				case MOD_CONST: return pickBlend<TEX, MOD_CONST>(blend);
				default: return pickBlend<TEX, MOD_VARY>(blend);
				}
			}

			SpanFn pickSpan(int tex, int mod, int blend)
			{
				if (tex == TEX_NONE && mod == MOD_NONE)
					mod = MOD_CONST; // an untextured shape always takes its colour

				switch (tex)
				{
				case TEX_NONE: return pickMod<TEX_NONE>(mod, blend);
				case TEX_NEAREST: return pickMod<TEX_NEAREST>(mod, blend);
				case TEX_LINEAR: return pickMod<TEX_LINEAR>(mod, blend);
				default: return pickMod<TEX_DIRECT>(mod, blend);
				}
			}

#ifdef SOFT_NEON
			// The spans most of a frame is (pictures and text over what is drawn, plain colours), eight pixels at
			// a time with NEON: the same pixels as spanT's. ES_SOFT_NEON=0 leaves them to spanT, to compare.
			bool neonEnabled()
			{
				static const bool enabled = []
				{
					const char* value = getenv("ES_SOFT_NEON");
					return value == nullptr || strcmp(value, "0") != 0;
				}();
				return enabled;
			}

			// div255, and div255x2's (x + 128 + (x >> 8)) >> 8, on 8 lanes
			inline uint8x8_t div255n(uint16x8_t x) { return vrshrn_n_u16(vaddq_u16(x, vrshrq_n_u16(x, 8)), 8); }
			inline uint8x8_t div255xn(uint16x8_t x) { return vrshrn_n_u16(vaddq_u16(x, vshrq_n_u16(x, 8)), 8); }

			// lerp8 on 8 lanes: (a * (256 - w) + b * w) >> 8
			inline uint8x8_t lerpn(uint8x8_t a, uint8x8_t b, uint16x8_t w, uint16x8_t iw)
			{
				return vshrn_n_u16(vmlaq_u16(vmulq_u16(vmovl_u8(a), iw), vmovl_u8(b), w), 8);
			}

			// 8 source pixels (their B, G, R and A apart), times the colour (MOD_CONST) as modulate does, blended
			// over the 8 at dst as blendNormal does
			template<int MOD>
			inline void finish8(uint32_t* dst, uint8x8x4_t src, const uint8x8x4_t& color)
			{
				if (MOD == MOD_CONST)
				{
					src.val[0] = div255n(vmull_u8(src.val[0], color.val[0]));
					src.val[1] = div255n(vmull_u8(src.val[1], color.val[1]));
					src.val[2] = div255n(vmull_u8(src.val[2], color.val[2]));
					src.val[3] = div255n(vmull_u8(src.val[3], color.val[3]));
				}
				uint8x8x4_t d = vld4_u8((const uint8_t*)dst);
				const uint8x8_t a = src.val[3], ia = vmvn_u8(a);
				d.val[0] = div255xn(vmlal_u8(vmull_u8(src.val[0], a), d.val[0], ia));
				d.val[1] = div255xn(vmlal_u8(vmull_u8(src.val[1], a), d.val[1], ia));
				d.val[2] = div255xn(vmlal_u8(vmull_u8(src.val[2], a), d.val[2], ia));
				d.val[3] = div255xn(vmlal_u8(vmull_u8(src.val[3], a), d.val[3], ia));
				vst4_u8((uint8_t*)dst, d);
			}

			// The pixels i in [first, last) of [0, n) whose c0 + i * dc (16.16) is in [low, high)
			inline void rangeInside(int64_t c0, int64_t dc, int64_t low, int64_t high, int n, int& first, int& last)
			{
				int64_t a, b;
				if (dc == 0)
				{
					a = 0;
					b = (c0 >= low && c0 < high) ? n : 0;
				}
				else if (dc > 0)
				{
					a = c0 >= low ? 0 : (low - c0 + dc - 1) / dc;
					b = c0 >= high ? 0 : (high - c0 + dc - 1) / dc;
				}
				else
				{
					const int64_t step = -dc;
					a = c0 < high ? 0 : (c0 - high) / step + 1;
					b = c0 < low ? 0 : (c0 - low) / step + 1;
				}
				first = (int)std::max<int64_t>(0, std::min<int64_t>(a, n));
				last = (int)std::max<int64_t>(first, std::min<int64_t>(b, n));
			}

			template<int TEX, int MOD>
			void spanNeon(uint32_t* dst, const uint8_t* mask, int maskTest, int n, const Tex& tex, Span s)
			{
				const SpanFn scalar = &spanT<TEX, MOD, BLEND_NORMAL>;

				// the pixels whose texels need no clamping or wrapping (all of a 1:1 picture's)
				int first = 0, last = n;
				if (TEX == TEX_NEAREST || TEX == TEX_LINEAR)
				{
					const int edge = TEX == TEX_LINEAR ? 1 : 0; // bilinear reads the next texel too
					int fx, lx, fy, ly;
					rangeInside(s.u, s.du, 0, (int64_t)(tex.w - edge) << 16, n, fx, lx);
					rangeInside(s.v, s.dv, 0, (int64_t)(tex.h - edge) << 16, n, fy, ly);
					first = std::max(fx, fy);
					last = std::min(lx, ly);
				}
				if (last - first < 8)
				{
					scalar(dst, mask, maskTest, n, tex, s);
					return;
				}

				auto at = [&s](int i)
				{
					Span t = s;
					t.u = (int)(s.u + (int64_t)i * s.du);
					t.v = (int)(s.v + (int64_t)i * s.dv);
					return t;
				};

				if (first > 0)
					scalar(dst, mask, maskTest, first, tex, s);

				uint8x8x4_t color;
				color.val[0] = vdup_n_u8((uint8_t)s.color);
				color.val[1] = vdup_n_u8((uint8_t)(s.color >> 8));
				color.val[2] = vdup_n_u8((uint8_t)(s.color >> 16));
				color.val[3] = vdup_n_u8((uint8_t)(s.color >> 24));

				const int end = first + ((last - first) & ~7);
				const uint32_t* direct = TEX == TEX_DIRECT ? tex.px + (s.v >> 16) * tex.w + (s.u >> 16) : nullptr;
				Span p = at(first);
				alignas(16) uint32_t c00[8], c10[8], c01[8], c11[8];
				alignas(16) uint16_t wx[8], wy[8];

				const int ahead = aheadPixels();
				for (int i = first; i < end; i += 8)
				{
					// a cache line (8 pixels) a step: the frame's row to be written, and a 1:1 picture's row
					if (ahead)
					{
						__builtin_prefetch(dst + i + ahead, 1);
						if (TEX == TEX_DIRECT)
							__builtin_prefetch(direct + i + ahead, 0);
					}
					uint8x8x4_t src;
					if (TEX == TEX_NONE)
						src = color;
					else if (TEX == TEX_DIRECT)
						src = vld4_u8((const uint8_t*)(direct + i));
					else if (TEX == TEX_NEAREST)
					{
						for (int j = 0; j < 8; j++)
						{
							c00[j] = tex.px[(p.v >> 16) * tex.w + (p.u >> 16)];
							p.u += p.du;
							p.v += p.dv;
						}
						src = vld4_u8((const uint8_t*)c00);
					}
					else
					{
						for (int j = 0; j < 8; j++)
						{
							const uint32_t* t = tex.px + (p.v >> 16) * tex.w + (p.u >> 16);
							c00[j] = t[0];
							c10[j] = t[1];
							c01[j] = t[tex.w];
							c11[j] = t[tex.w + 1];
							wx[j] = (uint16_t)(((uint32_t)p.u >> 8) & 0xFF);
							wy[j] = (uint16_t)(((uint32_t)p.v >> 8) & 0xFF);
							p.u += p.du;
							p.v += p.dv;
						}
						const uint16x8_t fx = vld1q_u16(wx), ifx = vsubq_u16(vdupq_n_u16(256), fx);
						const uint16x8_t fy = vld1q_u16(wy), ify = vsubq_u16(vdupq_n_u16(256), fy);
						const uint8x8x4_t a = vld4_u8((const uint8_t*)c00), b = vld4_u8((const uint8_t*)c10);
						const uint8x8x4_t c = vld4_u8((const uint8_t*)c01), d = vld4_u8((const uint8_t*)c11);
						for (int k = 0; k < 4; k++)
							src.val[k] = lerpn(lerpn(a.val[k], b.val[k], fx, ifx), lerpn(c.val[k], d.val[k], fx, ifx), fy, ify);
					}

					finish8<TEX == TEX_NONE ? MOD_NONE : MOD>(dst + i, src, color);
				}

				if (end < n)
					scalar(dst + end, mask ? mask + end : nullptr, maskTest, n - end, tex, at(end));
			}

			// An opaque picture faded (its colour white with alpha a), its rows read as they are: a texel times the
			// colour is the texel with alpha a (div255(x * 255) is x for every x in 0..255, checked for all of
			// them, and the texel's alpha is 255), so only the blend is worked out, the same alpha for every pixel:
			// finish8's sums without the colour's. On the A9's NEON the colour was 12 of the 30 operations of 8
			// pixels; two backgrounds crossing in a carousel's move are two such pictures over the whole frame.
			void spanNeonFade(uint32_t* dst, const uint8_t* mask, int maskTest, int n, const Tex& tex, Span s)
			{
				const SpanFn scalar = &spanT<TEX_DIRECT, MOD_CONST, BLEND_NORMAL>;
				const int end = n & ~7;
				if (end == 0)
				{
					scalar(dst, mask, maskTest, n, tex, s);
					return;
				}
				const uint8x8_t a = vdup_n_u8((uint8_t)(s.color >> 24)), ia = vmvn_u8(a);
				const uint32_t* direct = tex.px + (s.v >> 16) * tex.w + (s.u >> 16);
				const int ahead = aheadPixels();
				for (int i = 0; i < end; i += 8)
				{
					if (ahead)
					{
						__builtin_prefetch(dst + i + ahead, 1);
						__builtin_prefetch(direct + i + ahead, 0);
					}
					const uint8x8x4_t src = vld4_u8((const uint8_t*)(direct + i));
					uint8x8x4_t d = vld4_u8((const uint8_t*)(dst + i));
					d.val[0] = div255xn(vmlal_u8(vmull_u8(src.val[0], a), d.val[0], ia));
					d.val[1] = div255xn(vmlal_u8(vmull_u8(src.val[1], a), d.val[1], ia));
					d.val[2] = div255xn(vmlal_u8(vmull_u8(src.val[2], a), d.val[2], ia));
					d.val[3] = div255xn(vmlal_u8(vmull_u8(a, a), d.val[3], ia));
					vst4_u8((uint8_t*)(dst + i), d);
				}
				if (end < n)
				{
					Span t = s;
					t.u = (int)(s.u + (int64_t)end * s.du);
					scalar(dst + end, mask ? mask + end : nullptr, maskTest, n - end, tex, t);
				}
			}

			// Pictures with rounded corners (Carbon's game grids) are stencil-tested: drawn by runs of the mask.
			// The first i in [from, n) where (mask[i] == 1) is want, or n: 8 mask bytes at a time while none of
			// them is (a byte is 1 where x, the bytes xor 1, has a zero byte; the test for one is exact)
			inline int scanMask(const uint8_t* mask, int from, int n, bool want)
			{
				const uint64_t ones = 0x0101010101010101ULL;
				int i = from;
				while (i + 8 <= n)
				{
					uint64_t w;
					memcpy(&w, mask + i, 8);
					const uint64_t x = w ^ ones;
					const bool anyOne = ((x - ones) & ~x & 0x8080808080808080ULL) != 0;
					if (want ? anyOne : x != 0)
						break;
					i += 8;
				}
				while (i < n && (mask[i] == 1) != want)
					i++;
				return i;
			}

			// A span tested against the stencil: its runs of pixels the test lets through, each drawn by RUN (a
			// pixel's value does not depend on where its span starts, as with clipping); the others left as they
			// are, as spanT leaves them. Not for colours by vertex (RUN's spans start at u and v only).
			template<SpanFn RUN>
			void spanMaskedRuns(uint32_t* dst, const uint8_t* mask, int maskTest, int n, const Tex& tex, Span s)
			{
				if (mask == nullptr || maskTest == 0)
				{
					RUN(dst, nullptr, 0, n, tex, s);
					return;
				}
				const bool inside = maskTest == 1; // drawn where (mask == 1) is inside
				int i = 0;
				while (i < n)
				{
					i = scanMask(mask, i, n, inside);
					if (i >= n)
						break;
					const int j = scanMask(mask, i, n, !inside);
					Span t = s;
					t.u = (int)(s.u + (int64_t)i * s.du);
					t.v = (int)(s.v + (int64_t)i * s.dv);
					RUN(dst + i, nullptr, 0, j - i, tex, t);
					i = j;
				}
			}

			template<int TEX, int MOD>
			void spanNeonMasked(uint32_t* dst, const uint8_t* mask, int maskTest, int n, const Tex& tex, Span s)
			{
				spanMaskedRuns<&spanNeon<TEX, MOD>>(dst, mask, maskTest, n, tex, s);
			}

			// An opaque picture, its rows read as they are, untinted (MOD_NONE) or tinted by an opaque colour: every
			// pixel it gives has alpha 255 (the texel's 255 times 255 is 255), and blended at 255 a pixel is that
			// value whatever was under it (finish8's sums give it): written, what was there not read. The game
			// tiles of Carbon's grids (an opaque colour on the tiles not selected).
			template<int MOD>
			void spanNeonCover(uint32_t* dst, const uint8_t* mask, int maskTest, int n, const Tex& tex, Span s)
			{
				const uint32_t* direct = tex.px + (s.v >> 16) * tex.w + (s.u >> 16);
				if (MOD == MOD_NONE)
				{
					copyPixels(dst, direct, n);
					return;
				}
				const int end = n & ~7;
				const uint8x8_t r = vdup_n_u8((uint8_t)(s.color >> 16)), g = vdup_n_u8((uint8_t)(s.color >> 8)), b = vdup_n_u8((uint8_t)s.color);
				const int ahead = aheadPixels();
				for (int i = 0; i < end; i += 8)
				{
					if (ahead)
						__builtin_prefetch(direct + i + ahead, 0);
					uint8x8x4_t src = vld4_u8((const uint8_t*)(direct + i));
					src.val[0] = div255n(vmull_u8(src.val[0], b));
					src.val[1] = div255n(vmull_u8(src.val[1], g));
					src.val[2] = div255n(vmull_u8(src.val[2], r));
					vst4_u8((uint8_t*)(dst + i), src);
				}
				if (end < n)
				{
					Span t = s;
					t.u = (int)(s.u + (int64_t)end * s.du);
					spanT<TEX_DIRECT, MOD_CONST, BLEND_NORMAL>(dst + end, mask ? mask + end : nullptr, maskTest, n - end, tex, t);
				}
			}

			// A picture tinted black (a shadow, a shade; not opaque: an opaque one is drawn as a colour): a texel
			// times the colour is black with the texel's alpha times the colour's, so blended normally only what
			// was there is darkened, by that alpha (finish8's sums with the source's colour 0, whose products are
			// 0). The shadows under Carbon's game tiles.
			void spanNeonShade(uint32_t* dst, const uint8_t* mask, int maskTest, int n, const Tex& tex, Span s)
			{
				const uint32_t* direct = tex.px + (s.v >> 16) * tex.w + (s.u >> 16);
				const int end = n & ~7;
				const uint8x8_t ta = vdup_n_u8((uint8_t)(s.color >> 24));
				const int ahead = aheadPixels();
				for (int i = 0; i < end; i += 8)
				{
					if (ahead)
					{
						__builtin_prefetch(dst + i + ahead, 1);
						__builtin_prefetch(direct + i + ahead, 0);
					}
					const uint8x8x4_t src = vld4_u8((const uint8_t*)(direct + i));
					const uint8x8_t a = div255n(vmull_u8(src.val[3], ta)), ia = vmvn_u8(a);
					uint8x8x4_t d = vld4_u8((const uint8_t*)(dst + i));
					d.val[0] = div255xn(vmull_u8(d.val[0], ia));
					d.val[1] = div255xn(vmull_u8(d.val[1], ia));
					d.val[2] = div255xn(vmull_u8(d.val[2], ia));
					d.val[3] = div255xn(vmlal_u8(vmull_u8(a, a), d.val[3], ia));
					vst4_u8((uint8_t*)(dst + i), d);
				}
				if (end < n)
				{
					Span t = s;
					t.u = (int)(s.u + (int64_t)end * s.du);
					spanT<TEX_DIRECT, MOD_CONST, BLEND_NORMAL>(dst + end, mask ? mask + end : nullptr, maskTest, n - end, tex, t);
				}
			}

			// An opaque colour: blended at 255, the colour, whatever was under it
			void spanFill(uint32_t* dst, const uint8_t*, int, int n, const Tex&, Span s)
			{
				std::fill_n(dst, n, s.color);
			}
#endif

			// spanT, or its NEON version where there is one (blended normally, not desaturated; tested against
			// the stencil, by runs)
			SpanFn pickSpanFor(int tex, int mod, int blend, bool masked, bool desaturated)
			{
#ifdef SOFT_NEON
				if (!desaturated && blend == BLEND_NORMAL && neonEnabled())
				{
					if (tex == TEX_NONE && mod != MOD_VARY)
						return masked ? &spanNeonMasked<TEX_NONE, MOD_CONST> : &spanNeon<TEX_NONE, MOD_CONST>;
					if (mod == MOD_NONE || mod == MOD_CONST)
					{
						const bool constant = mod == MOD_CONST;
						switch (tex)
						{
						case TEX_DIRECT:
							if (masked)
								return constant ? &spanNeonMasked<TEX_DIRECT, MOD_CONST> : &spanNeonMasked<TEX_DIRECT, MOD_NONE>;
							return constant ? &spanNeon<TEX_DIRECT, MOD_CONST> : &spanNeon<TEX_DIRECT, MOD_NONE>;
						case TEX_NEAREST:
							if (masked)
								return constant ? &spanNeonMasked<TEX_NEAREST, MOD_CONST> : &spanNeonMasked<TEX_NEAREST, MOD_NONE>;
							return constant ? &spanNeon<TEX_NEAREST, MOD_CONST> : &spanNeon<TEX_NEAREST, MOD_NONE>;
						case TEX_LINEAR:
							if (masked)
								return constant ? &spanNeonMasked<TEX_LINEAR, MOD_CONST> : &spanNeonMasked<TEX_LINEAR, MOD_NONE>;
							return constant ? &spanNeon<TEX_LINEAR, MOD_CONST> : &spanNeon<TEX_LINEAR, MOD_NONE>;
						}
					}
				}
#endif
				(void)masked;
				(void)desaturated;
				return pickSpan(tex, mod, blend);
			}

			// The span of rows read as they are (the scaled path, the rows path, a picture 1:1): pickSpanFor's, or (its
			// colour constant, not by vertex; by runs of the mask when stencil-tested) for an opaque picture
			// spanNeonCover untinted or tinted by an opaque colour, spanNeonFade faded (white with an alpha), and
			// for any picture tinted black spanNeonShade
			SpanFn pickRowSpan(SpanFn span, const Texture* t, int mod, uint32_t color, int blend, bool masked, bool desaturated)
			{
#ifdef SOFT_NEON
				if (neonEnabled() && t != nullptr && t->opaque && blend == BLEND_NORMAL && !desaturated)
				{
					if (mod == MOD_NONE)
						return masked ? &spanMaskedRuns<&spanNeonCover<MOD_NONE>> : &spanNeonCover<MOD_NONE>;
					if (mod == MOD_CONST && (color >> 24) == 255)
						return masked ? &spanMaskedRuns<&spanNeonCover<MOD_CONST>> : &spanNeonCover<MOD_CONST>;
					if (mod == MOD_CONST && (color & 0x00FFFFFF) == 0x00FFFFFF)
						return masked ? &spanMaskedRuns<&spanNeonFade> : &spanNeonFade;
				}
				if (neonEnabled() && t != nullptr && mod == MOD_CONST && (color & 0x00FFFFFF) == 0 && blend == BLEND_NORMAL && !desaturated)
					return masked ? &spanMaskedRuns<&spanNeonShade> : &spanNeonShade;
#endif
				(void)t; (void)mod; (void)color; (void)blend; (void)masked; (void)desaturated;
				return span;
			}

			// A layer's pixels in a stretch of a row (drawLayers): its texels from px (none: its colour), times
			// its colour (MOD_CONST) or as they are, blended normally; drawn by fn on its own
			struct LayerRow
			{
				const uint32_t* px;
				uint32_t color;
				int mod;
				bool opaque; // every pixel it gives has alpha 255: what was under it is not read
				bool fade;   // an opaque picture faded: its texels with the colour's alpha (spanNeonFade)
				SpanFn fn;
			};

			// n pixels through the k layers, the first first: each pixel's value through each layer in turn, as
			// each layer's span drawn after the last gives. NEON: 8 pixels at a time, with finish8's sums (the
			// colour times the texels, then the blend) on the pixels kept in registers between the layers, read
			// and written once; the rest by each layer's span, one after another. cleared: the pixels are not
			// drawn yet, and start as the part's clear colour, 0 (not read: written once, with their value).
			void layersSpan(uint32_t* dst, int n, const LayerRow* rows, int k, bool cleared)
			{
				int i = 0;
#ifdef SOFT_NEON
				if (neonEnabled() && n >= 8)
				{
					uint8x8x4_t colors[8];
					for (int l = 0; l < k; l++)
						for (int c = 0; c < 4; c++)
							colors[l].val[c] = vdup_n_u8((uint8_t)(rows[l].color >> (8 * c)));
					const int ahead = aheadPixels();
					// an opaque first layer leaves nothing of what was there
					const int from = rows[0].opaque ? 1 : 0;
					for (; i + 8 <= n; i += 8)
					{
						if (ahead)
						{
							__builtin_prefetch(dst + i + ahead, 1);
							for (int l = 0; l < k; l++)
								if (rows[l].px)
									__builtin_prefetch(rows[l].px + i + ahead, 0);
						}
						uint8x8x4_t d;
						for (int l = 0; l < k; l++)
						{
							uint8x8x4_t src;
							if (rows[l].px == nullptr)
								src = colors[l];
							else
							{
								src = vld4_u8((const uint8_t*)(rows[l].px + i));
								if (rows[l].fade)
									src.val[3] = colors[l].val[3];
								else if (rows[l].mod == MOD_CONST)
									for (int c = 0; c < 4; c++)
										src.val[c] = div255n(vmull_u8(src.val[c], colors[l].val[c]));
							}
							if (l < from)
							{
								d = src; // blended with alpha 255: itself (finish8's sums give it, whatever was there)
								continue;
							}
							const uint8x8_t a = src.val[3];
							if (l == 0 && cleared)
							{
								// over the clear colour, 0: its part of the sum is 0
								for (int c = 0; c < 4; c++)
									d.val[c] = div255xn(vmull_u8(src.val[c], a));
								continue;
							}
							if (l == 0)
								d = vld4_u8((const uint8_t*)(dst + i));
							const uint8x8_t ia = vmvn_u8(a);
							for (int c = 0; c < 4; c++)
								d.val[c] = div255xn(vmlal_u8(vmull_u8(src.val[c], a), d.val[c], ia));
						}
						vst4_u8((uint8_t*)(dst + i), d);
					}
				}
#endif
				if (i >= n)
					return;
				if (cleared)
					std::fill(dst + i, dst + n, 0u);
				for (int l = 0; l < k; l++)
				{
					Tex t;
					t.px = rows[l].px ? rows[l].px + i : nullptr;
					t.w = n - i;
					t.h = 1;
					Span s;
					s.u = 0; s.du = 1 << 16;
					s.v = 0; s.dv = 0;
					s.color = rows[l].color;
					rows[l].fn(dst + i, nullptr, 0, n - i, t, s);
				}
			}

			int blendModeOf(const DrawState& state)
			{
				if (state.srcBlend == F_SRC_ALPHA && state.dstBlend == F_ONE_MINUS_SRC_ALPHA)
					return BLEND_NORMAL;
				if (state.srcBlend == F_ONE && state.dstBlend == F_ONE)
					return BLEND_ADD;
				return BLEND_GENERIC;
			}

			int saturationOf(const DrawState& state)
			{
				if (state.saturation >= 0.999f)
					return 256;
				const float s = state.saturation < 0 ? 0 : state.saturation;
				return (int)(s * 256.0f);
			}

			inline int toFixed(float v)
			{
				const float f = v * 65536.0f;
				if (f > 2.0e9f) return 2000000000;
				if (f < -2.0e9f) return -2000000000;
				return floorInt(f);
			}

			// Whether every texel a quad samples (columns from u, rows from v, as the samplers step them; the
			// next column and row too, for bilinear) is the same, and which: a picture's flat part stretched
			inline bool uniformTexels(const Tex& tex, bool linear, int uFirst, int du, int columns, int vFirst, int vLast, uint32_t& texel)
			{
				const int uLast = uFirst + (columns - 1) * du;
				const int x0 = std::min(uFirst, uLast) >> 16, x1 = (std::max(uFirst, uLast) >> 16) + (linear ? 1 : 0);
				const int y0 = std::min(vFirst, vLast) >> 16, y1 = (std::max(vFirst, vLast) >> 16) + (linear ? 1 : 0);
				if ((int64_t)(x1 - x0 + 1) * (y1 - y0 + 1) > 4096)
					return false;
				texel = tex.px[wrapCoord(y0, tex.h, tex.repeat) * tex.w + wrapCoord(x0, tex.w, tex.repeat)];
				for (int y = y0; y <= y1; y++)
				{
					const uint32_t* row = tex.px + (size_t)wrapCoord(y, tex.h, tex.repeat) * tex.w;
					for (int x = x0; x <= x1; x++)
						if (row[wrapCoord(x, tex.w, tex.repeat)] != texel)
							return false;
				}
				return true;
			}

			// A big picture drawn scaled and smoothed (a background stretched to the screen): sampled on its own
			// grid, as if the quad started on a whole pixel (one at a fraction of a pixel draws the same pixels as
			// at the pixel it starts on, wherever it moves, as in a slide), and those pixels kept: a row sampled the
			// first time a frame needs it, then the next frames draw it 1:1.
			struct Scaled
			{
				unsigned int id = 0;
				uint64_t generation = 0;
				int w = 0, h = 0;             // its size in pixels: the quad's, rounded
				float u0 = 0, u1 = 0, t0 = 0, t1 = 0;
				// a column and a row more than w x h, for a quad that covers one more
				std::vector<int32_t> x0s, x1s;
				std::vector<uint16_t> fxs;
				int vStart = 0, dv = 0;       // 16.16
				std::unique_ptr<uint32_t[]> px; // (w + 1) x (h + 1), kept (none: sampled each time, into the caller's row)
				std::unique_ptr<std::atomic<uint8_t>[]> state; // a row: 0 not sampled, 1 being sampled, 2 sampled
				uint64_t used = 0;            // the frame it was last drawn in
				size_t bytes() const { return px ? (size_t)(w + 1) * (h + 1) * 4 : 0; }
			};

			struct ScaledCache
			{
				std::mutex lock;
				std::vector<std::shared_ptr<Scaled>> entries;
				size_t bytes = 0;
				size_t budget = 0;
				std::atomic<uint64_t> frame{ 1 };
				bool ready = false;
			};

			ScaledCache& scaledCache()
			{
				static ScaledCache cache;
				return cache;
			}

			// texture rows sampled for a scaled picture's rows, a thread's own (the bands sample at once)
			struct ScaledScratch
			{
				std::vector<uint32_t> a, b, out;
				const Scaled* owner = nullptr;
				int keyA = -1, keyB = -1;
			};

			// the picture's columns and rows: as drawQuad would sample them for a quad on whole pixels
			void setGrid(Scaled& sc, const Texture* t)
			{
				const int columns = sc.w + 1;
				const int du = toFixed((sc.u1 - sc.u0) * t->width / sc.w);
				const int uStart = toFixed((sc.u0 + 0.5f * (sc.u1 - sc.u0) / sc.w) * t->width - 0.5f);
				sc.x0s.resize(columns);
				sc.x1s.resize(columns);
				sc.fxs.resize(columns);
				for (int i = 0; i < columns; i++)
				{
					const int u = (int)((int64_t)uStart + (int64_t)i * du);
					sc.x0s[i] = wrapCoord(u >> 16, t->width, t->repeat);
					sc.x1s[i] = wrapCoord((u >> 16) + 1, t->width, t->repeat);
					sc.fxs[i] = (uint16_t)(((uint32_t)u >> 8) & 0xFF);
				}
				sc.dv = toFixed((sc.t1 - sc.t0) * t->height / sc.h);
				sc.vStart = toFixed((sc.t0 + 0.5f * (sc.t1 - sc.t0) / sc.h) * t->height - 0.5f);
			}

			// the scaled picture of a quad: kept, or (no room) one whose rows are sampled each time
			std::shared_ptr<Scaled> scaledFor(const Texture* t, int w, int h, float u0, float u1, float t0, float t1)
			{
				ScaledCache& cache = scaledCache();
				std::lock_guard<std::mutex> guard(cache.lock);
				if (!cache.ready)
				{
					cache.ready = true;
					const char* mb = getenv("ES_SOFT_SCALED_MB");
					cache.budget = (size_t)(mb ? std::max(0, atoi(mb)) : 40) << 20;
				}
				const uint64_t frame = cache.frame.load();
				for (auto& entry : cache.entries)
					if (entry->id == t->id && entry->generation == t->generation && entry->w == w && entry->h == h &&
						entry->u0 == u0 && entry->u1 == u1 && entry->t0 == t0 && entry->t1 == t1)
					{
						entry->used = frame;
						return entry;
					}

				std::shared_ptr<Scaled> sc = std::make_shared<Scaled>();
				sc->id = t->id; sc->generation = t->generation;
				sc->w = w; sc->h = h;
				sc->u0 = u0; sc->u1 = u1; sc->t0 = t0; sc->t1 = t1;
				sc->used = frame;
				setGrid(*sc, t);

				// room: the pictures not drawn in this frame, the least lately drawn first
				const size_t bytes = (size_t)(w + 1) * (h + 1) * 4;
				if (bytes > cache.budget)
					return sc; // not kept
				while (cache.bytes + bytes > cache.budget)
				{
					auto oldest = cache.entries.end();
					for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it)
						if ((*it)->used != frame && (oldest == cache.entries.end() || (*it)->used < (*oldest)->used))
							oldest = it;
					if (oldest == cache.entries.end())
						return sc; // not kept
					cache.bytes -= (*oldest)->bytes();
					cache.entries.erase(oldest);
				}
				sc->px.reset(new (std::nothrow) uint32_t[(size_t)(w + 1) * (h + 1)]);
				if (!sc->px)
					return sc;
				sc->state.reset(new std::atomic<uint8_t>[h + 1]());
				cache.bytes += bytes;
				cache.entries.push_back(sc);
				return sc;
			}

			// row r of a scaled picture: kept, or sampled now (into the scratch's row)
			const uint32_t* scaledRow(Scaled& sc, const Texture* t, int r, ScaledScratch& scratch)
			{
				const int columns = sc.w + 1;
				uint32_t* keep = nullptr;
				if (sc.px)
				{
					uint32_t* row = sc.px.get() + (size_t)r * columns;
					uint8_t state = sc.state[r].load(std::memory_order_acquire);
					if (state == 2)
						return row;
					// not sampled: this thread samples it (another sampling it now: sampled here too, not kept)
					if (state == 0 && sc.state[r].compare_exchange_strong(state, 1, std::memory_order_acq_rel))
						keep = row;
				}

				if (scratch.owner != &sc || (int)scratch.out.size() < columns)
				{
					scratch.owner = &sc;
					scratch.keyA = scratch.keyB = -1;
					if ((int)scratch.out.size() < columns)
					{
						scratch.a.resize(columns); scratch.b.resize(columns); scratch.out.resize(columns);
					}
				}
				auto sample = [&](int y, uint32_t* into)
				{
					const uint32_t* src = t->pixels.data() + (size_t)y * t->width;
					sampleAcross(into, src, sc.x0s.data(), sc.x1s.data(), sc.fxs.data(), columns);
				};
				auto rowOf = [&](int y, int other) -> const uint32_t*
				{
					if (scratch.keyA == y) return scratch.a.data();
					if (scratch.keyB == y) return scratch.b.data();
					if (scratch.keyA != other) { sample(y, scratch.a.data()); scratch.keyA = y; return scratch.a.data(); }
					sample(y, scratch.b.data()); scratch.keyB = y; return scratch.b.data();
				};
				const int v = (int)((int64_t)sc.vStart + (int64_t)r * sc.dv);
				const int r0 = wrapCoord(v >> 16, t->height, t->repeat), r1 = wrapCoord((v >> 16) + 1, t->height, t->repeat);
				const uint32_t* top = rowOf(r0, r1);
				const uint32_t* bottom = rowOf(r1, r0);
				uint32_t* out = keep ? keep : scratch.out.data();
				lerpRows(out, top, bottom, ((uint32_t)v >> 8) & 0xFF, columns);
				if (keep)
					sc.state[r].store(2, std::memory_order_release);
				return out;
			}

			struct ColorF { float r, g, b, a; };

			inline ColorF unpack(uint32_t c)
			{
				return { (float)((c >> 16) & 0xFF), (float)((c >> 8) & 0xFF), (float)(c & 0xFF), (float)(c >> 24) };
			}

			inline ColorF mixColor(const ColorF& a, const ColorF& b, float t)
			{
				return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
			}

			// The pixels a triangle covers, a span per row: f(y, x0, x1), x1 excluded
			template<typename F>
			void coverTriangle(const SVertex& a, const SVertex& b, const SVertex& c, int cx0, int cy0, int cx1, int cy1, F&& f)
			{
				const SVertex* v0 = &a;
				const SVertex* v1 = &b;
				const SVertex* v2 = &c;
				if (v1->y < v0->y) std::swap(v0, v1);
				if (v2->y < v1->y) std::swap(v1, v2);
				if (v1->y < v0->y) std::swap(v0, v1);

				const int yStart = std::max(cy0, ceilInt(v0->y - 0.5f));
				const int yEnd = std::min(cy1, ceilInt(v2->y - 0.5f));
				if (yStart >= yEnd)
					return;

				const float longDy = v2->y - v0->y;
				if (longDy <= 0)
					return;
				const float longSlope = (v2->x - v0->x) / longDy;

				for (int y = yStart; y < yEnd; y++)
				{
					const float yc = y + 0.5f;
					const float xl = v0->x + (yc - v0->y) * longSlope;
					float xs;
					if (yc < v1->y)
					{
						const float dy = v1->y - v0->y;
						xs = dy > 0 ? v0->x + (yc - v0->y) * (v1->x - v0->x) / dy : v0->x;
					}
					else
					{
						const float dy = v2->y - v1->y;
						xs = dy > 0 ? v1->x + (yc - v1->y) * (v2->x - v1->x) / dy : v1->x;
					}

					const float left = std::min(xl, xs), right = std::max(xl, xs);
					const int first = ceilInt(left - 0.5f); // the row's first column, before the clip
					const int x0 = std::max(cx0, first);
					const int x1 = std::min(cx1, ceilInt(right - 0.5f));
					if (x0 < x1)
						f(y, x0, x1, first);
				}
			}
		}

		bool coversOpaque(const SVertex* v, int count, Topology topology, const DrawState& state, const Bounds& part)
		{
			if (topology != Topology::Strip || count != 4 || state.stencil != StencilTest::Off || blendModeOf(state) != BLEND_NORMAL)
				return false;
			const Texture* t = state.texture != nullptr && !state.texture->pixels.empty() ? state.texture : nullptr;
			if (!(v[0].x == v[1].x && v[2].x == v[3].x && v[0].y == v[2].y && v[1].y == v[3].y &&
				(state.texture == nullptr || (v[0].u == v[1].u && v[2].u == v[3].u && v[0].v == v[2].v && v[1].v == v[3].v))))
				return false;
			// alpha 255 at every pixel: its colours' (interpolated or times a texel's), its texels'
			for (int i = 0; i < 4; i++)
				if ((v[i].color >> 24) != 255)
					return false;
			if (t != nullptr && !t->opaque)
				return false;
			const float x0 = std::min(v[0].x, v[2].x), x1 = std::max(v[0].x, v[2].x);
			const float y0 = std::min(v[0].y, v[1].y), y1 = std::max(v[0].y, v[1].y);
			if (t != nullptr)
			{
				// not a big picture smoothed (its scaled picture may have a column fewer than a quad on a fraction
				// of a pixel covers): 1:1, or made bigger and not smoothed (drawn 1:1 or nearest, every pixel)
				const float su = std::fabs((v[2].u - v[0].u) * t->width / (x1 - x0)), sv = std::fabs((v[1].v - v[0].v) * t->height / (y1 - y0));
				const bool oneToOne = std::fabs(su - 1.0f) < 0.002f && std::fabs(sv - 1.0f) < 0.002f;
				if (!oneToOne && !(su <= 1.0f && sv <= 1.0f && !t->linear))
					return false;
			}
			// the whole part: its scissor, and the pixels whose centres it covers (drawQuad's)
			if (!state.scissor || state.sx0 > part.x0 || state.sy0 > part.y0 || state.sx1 < part.x1 || state.sy1 < part.y1)
				return false;
			return ceilInt(x0 - 0.5f) <= part.x0 && ceilInt(x1 - 0.5f) >= part.x1 &&
				ceilInt(y0 - 0.5f) <= part.y0 && ceilInt(y1 - 0.5f) >= part.y1;
		}

		bool layersOn()
		{
			static const bool on = getenv("ES_SOFT_LAYERS") == nullptr || strcmp(getenv("ES_SOFT_LAYERS"), "0") != 0;
			return on;
		}

		// A draw drawn with others, a row at a time (drawLayers), as drawQuad describes it
		struct Raster::Layer
		{
			enum Kind { NOTHING, COLOR, ROWS, SCALED };
			bool ok = true;           // drawn this way (or draws nothing)
			Kind kind = NOTHING;
			int x0 = 0, x1 = 0, y0 = 0, y1 = 0; // the pixels it reaches
			int mod = MOD_NONE;       // its texels times color (MOD_CONST), or as they are
			uint32_t color = 0;
			const Texture* texture = nullptr;
			// ROWS: the texture 1:1, from column ux, frame row y on texture row floor(v0 + (y - qy0) * dv), drawn
			// between its clear runs if it has them
			float v0 = 0, dv = 0;
			int qy0 = 0, ux = 0;
			// SCALED: a big picture's scaled rows (frame row y on row y - qy0), from column c0
			std::shared_ptr<Scaled> scaled;
			int c0 = 0;
		};

		bool Raster::layerOf(const LayerDraw& d, Layer& out)
		{
			out = Layer();
			// a rectangle alone (draw's quad: one drawQuad)
			if (d.topology != Topology::Strip || d.count != 4)
				return false;
			const SVertex* v = d.vertices;
			const bool textured = d.state.texture != nullptr;
			if (!(v[0].x == v[1].x && v[2].x == v[3].x && v[0].y == v[2].y && v[1].y == v[3].y &&
				(!textured || (v[0].u == v[1].u && v[2].u == v[3].u && v[0].v == v[2].v && v[1].v == v[3].v))))
				return false;
			const Clip clip = clipOf(d.state);
			if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1)
				return true; // draws nothing
			if (blendModeOf(d.state) != BLEND_NORMAL)
				return false;
			if (((v[0].color | v[1].color | v[2].color | v[3].color) >> 24) == 0)
				return true; // no alpha at any vertex: draws nothing (as draw)
			mLayer = &out;
			drawQuad(v, d.state, clip);
			mLayer = nullptr;
			return out.ok;
		}

		int Raster::drawLayers(const LayerDraw* draws, int count, const Bounds* uncleared)
		{
			thread_local std::vector<Layer> layers;
			layers.clear();
			int drawing = 0;
			for (int i = 0; i < count && i < 8; i++)
			{
				layers.emplace_back();
				if (!layerOf(draws[i], layers.back()))
				{
					layers.pop_back();
					break;
				}
				if (layers.back().kind != Layer::NOTHING)
					drawing++;
			}
			if (drawing < 2)
				return 0;

			const int k = (int)layers.size();
			int top = INT32_MAX, bottom = INT32_MIN;
			for (const Layer& L : layers)
				if (L.kind != Layer::NOTHING)
				{
					top = std::min(top, L.y0);
					bottom = std::max(bottom, L.y1);
				}
			// a part not cleared yet: all its rows, cleared where no layer draws, from 0 where one does
			if (uncleared)
			{
				top = std::max(mBandY0, uncleared->y0);
				bottom = std::min(mBandY1, uncleared->y1);
			}

			// each layer's sampling of its texture rows, its own (two scaled pictures a row each, in turn)
			thread_local std::vector<ScaledScratch> scratches;
			if ((int)scratches.size() < k)
				scratches.resize(k);
			for (int l = 0; l < k; l++)
				scratches[l].owner = nullptr; // their texture rows are another picture's

			// the spans of each: as drawQuad draws it, on its own
			SpanFn fns[8];
			for (int l = 0; l < k; l++)
			{
				const Layer& L = layers[l];
				fns[l] = L.kind == Layer::COLOR ? pickSpanFor(TEX_NONE, MOD_CONST, BLEND_NORMAL, false, false) :
					pickRowSpan(pickSpanFor(TEX_DIRECT, L.mod, BLEND_NORMAL, false, false), L.texture, L.mod, L.color, BLEND_NORMAL, false, false);
			}

			thread_local std::vector<int> edges;   // the row's columns where a layer starts or stops drawing
			thread_local std::vector<int> spans[8]; // each layer's drawn columns in the row: [from, to) pairs
			const uint32_t* rowsAt[8];              // each layer's pixel for the row's column x0
			uint64_t drawn[4] = {};

			for (int y = top; y < bottom; y++)
			{
				edges.clear();
				for (int l = 0; l < k; l++)
				{
					const Layer& L = layers[l];
					spans[l].clear();
					if (L.kind == Layer::NOTHING || y < L.y0 || y >= L.y1)
						continue;
					rowsAt[l] = nullptr;
					if (L.kind == Layer::SCALED)
						rowsAt[l] = scaledRow(*L.scaled, L.texture, std::min(y - L.qy0, L.scaled->h), scratches[l]) + L.c0;
					else if (L.kind == Layer::ROWS)
					{
						const int vy = floorInt(L.v0 + (y - L.qy0) * L.dv);
						rowsAt[l] = L.texture->pixels.data() + (size_t)vy * L.texture->width + L.ux;
						if (!L.texture->clearAt.empty())
						{
							// between the row's clear runs (blending a clear texel leaves the pixel as it was)
							const Texture* t = L.texture;
							const int n = L.x1 - L.x0;
							int at = 0;
							for (uint32_t r = t->clearAt[vy]; r < t->clearAt[vy + 1]; r += 2)
							{
								const int c0 = (int)t->clear[r] - L.ux, c1 = (int)t->clear[r + 1] - L.ux;
								if (c1 <= at)
									continue;
								if (c0 >= n)
									break;
								if (c0 > at)
								{
									spans[l].push_back(L.x0 + at);
									spans[l].push_back(L.x0 + c0);
								}
								at = std::min(n, c1);
							}
							if (at < n)
							{
								spans[l].push_back(L.x0 + at);
								spans[l].push_back(L.x1);
							}
						}
					}
					if (spans[l].empty() && !(L.kind == Layer::ROWS && !L.texture->clearAt.empty()))
					{
						spans[l].push_back(L.x0);
						spans[l].push_back(L.x1);
					}
					edges.insert(edges.end(), spans[l].begin(), spans[l].end());
				}
				if (uncleared)
				{
					edges.push_back(uncleared->x0);
					edges.push_back(uncleared->x1);
				}
				if (edges.empty())
					continue;
				std::sort(edges.begin(), edges.end());
				edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

				// between two edges each layer draws all of it or none: those that do, in their order
				uint32_t* const row = mPx + (size_t)y * mWidth;
				size_t at[8] = {};
				for (size_t e = 0; e + 1 < edges.size(); e++)
				{
					const int a = edges[e], b = edges[e + 1];
					LayerRow active[8];
					int m = 0;
					for (int l = 0; l < k; l++)
					{
						const std::vector<int>& sp = spans[l];
						while (at[l] < sp.size() && sp[at[l] + 1] <= a)
							at[l] += 2;
						if (at[l] >= sp.size() || sp[at[l]] > a)
							continue;
						const Layer& L = layers[l];
						LayerRow& r = active[m++];
						r.px = L.kind == Layer::COLOR ? nullptr : rowsAt[l] + (a - L.x0);
						r.color = L.color;
						r.mod = L.kind == Layer::COLOR ? MOD_CONST : L.mod;
						r.opaque = L.kind == Layer::COLOR ? (L.color >> 24) == 255 :
							(L.texture->opaque && (L.mod == MOD_NONE || (L.color >> 24) == 255));
						r.fade = L.kind != Layer::COLOR && L.texture->opaque && L.mod == MOD_CONST && (L.color & 0x00FFFFFF) == 0x00FFFFFF;
						r.fn = fns[l];
						drawn[L.kind] += (uint64_t)(b - a);
					}
					if (m > 0)
						layersSpan(row + a, b - a, active, m, uncleared != nullptr);
					else if (uncleared && a >= uncleared->x0 && b <= uncleared->x1)
						std::fill_n(row + a, b - a, 0u); // as the part's clearing
				}
			}

			mStats.together += (uint32_t)drawing;
			mStats.pixels += drawn[Layer::COLOR] + drawn[Layer::ROWS] + drawn[Layer::SCALED];
			mStats.kinds[PX_COLOR] += drawn[Layer::COLOR];
			mStats.kinds[PX_TEX_1TO1] += drawn[Layer::ROWS];
			mStats.kinds[PX_TEX_LINEAR] += drawn[Layer::SCALED];
			return k;
		}

		void Raster::resize(int width, int height)
		{
			mWidth = std::max(1, width);
			mHeight = std::max(1, height);
			mPixels.assign((size_t)mWidth * mHeight, 0);
			mStencil.assign((size_t)mWidth * mHeight, 0);
			mPx = mPixels.data();
			mSt = mStencil.data();
			mBandY0 = 0;
			mBandY1 = mHeight;
			mStencilBox = { 0, 0, 0, 0 };
		}

		void Raster::attach(Raster& owner, int y0, int y1)
		{
			resetStencil();
			mPixels.clear();
			mStencil.clear();
			mWidth = owner.mWidth;
			mHeight = owner.mHeight;
			mPx = owner.mPx;
			mSt = owner.mSt;
			mBandY0 = std::max(0, std::min(y0, mHeight));
			mBandY1 = std::max(mBandY0, std::min(y1, mHeight));
			mStencilBox = { 0, 0, 0, 0 };
		}

		void Raster::resetStencil()
		{
			if (mSt != nullptr)
				for (int y = mStencilBox.y0; y < mStencilBox.y1; y++)
					std::memset(mSt + (size_t)y * mWidth + mStencilBox.x0, 0, mStencilBox.x1 - mStencilBox.x0);
			mStencilBox = { 0, 0, 0, 0 };
		}

		void Raster::clear(uint32_t color)
		{
			std::fill(mPx + (size_t)mBandY0 * mWidth, mPx + (size_t)mBandY1 * mWidth, color);
		}

		void Raster::clearBox(int x0, int y0, int x1, int y1, uint32_t color)
		{
			x0 = std::max(0, x0); x1 = std::min(mWidth, x1);
			y0 = std::max(mBandY0, y0); y1 = std::min(mBandY1, y1);
			if (x0 >= x1)
				return;
			for (int y = y0; y < y1; y++)
				std::fill_n(mPx + (size_t)y * mWidth + x0, x1 - x0, color);
		}

		Raster::Clip Raster::clipOf(const DrawState& state) const
		{
			Clip clip = { 0, mBandY0, mWidth, mBandY1 };
			if (state.scissor)
			{
				clip.x0 = std::max(clip.x0, state.sx0);
				clip.y0 = std::max(clip.y0, state.sy0);
				clip.x1 = std::min(clip.x1, state.sx1);
				clip.y1 = std::min(clip.y1, state.sy1);
			}
			return clip;
		}

		void Raster::draw(const SVertex* vertices, int count, Topology topology, const DrawState& state)
		{
			const Clip clip = clipOf(state);
			if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1)
				return;

			// Blended normally with no alpha at any vertex: every pixel it reaches keeps its value (its alpha,
			// interpolated or times a texel's, is 0). Texts and pictures faded out entirely are still drawn by
			// ES: Carbon's system texts, five passes each, in every frame of the carousel's move.
			if (blendModeOf(state) == BLEND_NORMAL)
			{
				bool seen = false;
				for (int i = 0; i < count && !seen; i++)
					seen = (vertices[i].color >> 24) != 0;
				if (!seen)
					return;
			}

			if (topology == Topology::Lines)
			{
				for (int i = 0; i + 1 < count; i += 2)
					drawLine(vertices[i], vertices[i + 1], state, clip);
				return;
			}

			if (topology == Topology::Strip)
			{
				// axis-aligned rectangles, alone or joined by degenerate triangles (a line of text: six vertices a
				// glyph, the first and last repeated), go the rectangle's way; anything else as triangles
				const bool textured = state.texture != nullptr;
				auto isQuad = [textured](const SVertex* v)
				{
					return v[0].x == v[1].x && v[2].x == v[3].x && v[0].y == v[2].y && v[1].y == v[3].y &&
						(!textured || (v[0].u == v[1].u && v[2].u == v[3].u && v[0].v == v[2].v && v[1].v == v[3].v));
				};

				int i = 0;
				while (i + 2 < count)
				{
					if (i + 3 < count && isQuad(vertices + i))
					{
						drawQuad(vertices + i, state, clip);
						i += 2;
						continue;
					}
					drawTriangle(vertices[i], vertices[i + 1], vertices[i + 2], state, clip);
					i++;
				}
				return;
			}

			// fan
			for (int i = 1; i + 1 < count; i++)
				drawTriangle(vertices[0], vertices[i], vertices[i + 1], state, clip);
		}

		void Raster::drawQuad(const SVertex* v, const DrawState& state, const Clip& clip)
		{
			// v0 top left, v1 bottom left, v2 top right, v3 bottom right (as ES makes them); either axis may be reversed
			float x0 = v[0].x, x1 = v[2].x, y0 = v[0].y, y1 = v[1].y;
			float u0 = v[0].u, u1 = v[2].u, t0 = v[0].v, t1 = v[1].v;
			ColorF c00 = unpack(v[0].color), c01 = unpack(v[1].color), c10 = unpack(v[2].color), c11 = unpack(v[3].color);

			if (x1 < x0)
			{
				std::swap(x0, x1); std::swap(u0, u1);
				std::swap(c00, c10); std::swap(c01, c11);
			}
			if (y1 < y0)
			{
				std::swap(y0, y1); std::swap(t0, t1);
				std::swap(c00, c01); std::swap(c10, c11);
			}

			// its first row and column, unclipped: texels and colours count from them, so a pixel is the same
			// whatever clips the quad (a band's rows, a scissor, the part of the frame drawn again)
			const int qy0 = ceilInt(y0 - 0.5f);
			const int qx0 = ceilInt(x0 - 0.5f);
			const int px0 = std::max(clip.x0, qx0);
			const int px1 = std::min(clip.x1, ceilInt(x1 - 0.5f));
			const int py0 = std::max(clip.y0, qy0);
			const int py1 = std::min(clip.y1, ceilInt(y1 - 0.5f));
			if (px0 >= px1 || py0 >= py1)
				return;

			mStats.quads++;

			const float w = x1 - x0, h = y1 - y0;
			const bool sameColor = v[0].color == v[1].color && v[0].color == v[2].color && v[0].color == v[3].color;
			const bool rowColor = !sameColor && v[0].color == v[2].color && v[1].color == v[3].color; // one colour a row (a vertical gradient)

			Span s;
			s.saturation = saturationOf(state);
			s.srcBlend = state.srcBlend;
			s.dstBlend = state.dstBlend;
			const int blend = blendModeOf(state);

			Tex tex;
			int texMode = TEX_NONE;
			float texelU0 = 0, texelDu = 0, texelV0 = 0, texelDv = 0; // at the quad's first pixel's centre, per pixel
			const int skip = px0 - qx0; // the quad's columns left of the clip
			if (state.texture != nullptr && !state.texture->pixels.empty())
			{
				const Texture* t = state.texture;
				tex.px = t->pixels.data();
				tex.w = t->width;
				tex.h = t->height;
				tex.repeat = t->repeat;

				texelDu = (u1 - u0) * t->width / w;
				texelDv = (t1 - t0) * t->height / h;
				texelU0 = (u0 + (qx0 + 0.5f - x0) * (u1 - u0) / w) * t->width;
				texelV0 = (t0 + (qy0 + 0.5f - y0) * (t1 - t0) / h) * t->height;

				const float su = std::fabs(texelDu), sv = std::fabs(texelDv);
				const bool oneToOne = std::fabs(su - 1.0f) < 0.002f && std::fabs(sv - 1.0f) < 0.002f;
				const bool magnified = su <= 1.0f && sv <= 1.0f;

				if (oneToOne)
				{
					texMode = TEX_NEAREST;
					texelDu = texelDu > 0 ? 1.0f : -1.0f;
					texelDv = texelDv > 0 ? 1.0f : -1.0f;
					// runs of a texture row read straight, left to right, with no wrapping (the rows may go either way:
					// images are uploaded bottom up)
					const int ux = floorInt(texelU0) + skip;
					const int ulast = ux + (px1 - px0 - 1);
					const int vfirst = floorInt(texelV0 + (py0 - qy0) * texelDv);
					const int vlast = floorInt(texelV0 + (py1 - 1 - qy0) * texelDv);
					if (texelDu > 0 && ux >= 0 && ulast < t->width && std::min(vfirst, vlast) >= 0 && std::max(vfirst, vlast) < t->height)
						texMode = TEX_DIRECT;
				}
				else if (magnified && !t->linear)
					texMode = TEX_NEAREST;
				else
				{
					texMode = TEX_LINEAR;
					texelU0 -= 0.5f;
					texelV0 -= 0.5f;
				}
			}

			const int du = toFixed(texelDu);
			// the first drawn column's texel, in 16.16: the quad's first, and a step a column skipped
			const int uStart = (int)((int64_t)toFixed(texelU0) + (int64_t)skip * du);
			const int uxStart = floorInt(texelU0) + skip; // the same, 1:1

			// An opaque picture tinted black (a shade over the screen, Carbon's grid): every texel times the
			// tint is the tint's alpha and black (an opaque texel's alpha is 255, x * 255 / 255 is x, and its
			// colour times 0 is 0, desaturated or not), whatever the picture: the quad is drawn untextured, with
			// the same colours, and nothing is sampled.
			if (texMode != TEX_NONE && state.texture->opaque && ((v[0].color | v[1].color | v[2].color | v[3].color) & 0x00FFFFFF) == 0)
				texMode = TEX_NONE;

			int mod = MOD_VARY;
			if (sameColor)
			{
				mod = v[0].color == 0xFFFFFFFF && texMode != TEX_NONE ? MOD_NONE : MOD_CONST;
				s.color = v[0].color;
				if (texMode == TEX_NONE && (s.color >> 24) == 0 && blend == BLEND_NORMAL)
					return; // transparent: nothing to draw
			}

			// a big picture scaled and smoothed (a quarter of the frame or more, by its own size, not the part
			// drawn: the same choice in every band and part): its scaled picture, kept (Scaled)
			std::shared_ptr<Scaled> scaled;
			static const int scaledOn = getenv("ES_SOFT_SCALED_MB") == nullptr || atoi(getenv("ES_SOFT_SCALED_MB")) > 0 ? 1 : 0;
			if (scaledOn && texMode == TEX_LINEAR && !state.texture->pixels.empty())
			{
				const int sw = (int)std::lround(w), sh = (int)std::lround(h);
				if (sw >= 8 && sh >= 8 && (int64_t)sw * sh * 4 >= (int64_t)mWidth * mHeight && (int64_t)(sw + 1) * (sh + 1) <= (int64_t)1 << 24)
					scaled = scaledFor(state.texture, sw, sh, u0, u1, t0, t1);
			}

			// a picture's flat part stretched (the middle of a frame drawn in nine parts, a white square): every
			// pixel samples the same texel, so the quad is that colour (desaturated and tinted as a texel is)
			uint32_t flat;
			if (!scaled && (texMode == TEX_LINEAR || texMode == TEX_NEAREST) && mod != MOD_VARY &&
				uniformTexels(tex, texMode == TEX_LINEAR, uStart, du, px1 - px0,
					toFixed(texelV0 + (py0 - qy0) * texelDv), toFixed(texelV0 + (py1 - 1 - qy0) * texelDv), flat))
			{
				if (s.saturation < 256)
					flat = desaturate(flat, s.saturation);
				s.color = mod == MOD_CONST ? modulate(flat, s.color) : flat;
				mod = MOD_CONST;
				texMode = TEX_NONE;
				if ((s.color >> 24) == 0 && blend == BLEND_NORMAL)
					return;
			}

			const uint8_t* stencil = state.stencil != StencilTest::Off ? mSt : nullptr;
			const int maskTest = state.stencil == StencilTest::Inside ? 1 : (state.stencil == StencilTest::Outside ? 2 : 0);

			// As a layer of drawLayers: described, not drawn. A layer when what follows would draw it a row at a
			// time from rows read as they are, blended normally: a colour (a fill, a shade), a picture 1:1 (copied,
			// blended, between its clear runs), a big picture's scaled rows; not one sampled here, tinted by vertex,
			// desaturated or tested against the stencil.
			if (mLayer)
			{
				Layer& L = *mLayer;
				L.ok = false;
				L.x0 = px0; L.x1 = px1; L.y0 = py0; L.y1 = py1;
				if (blend != BLEND_NORMAL || maskTest != 0 || s.saturation != 256 || mod == MOD_VARY)
					return;
				L.ok = true;
				L.mod = mod;
				L.color = s.color;
				if (texMode == TEX_NONE)
				{
					L.kind = Layer::COLOR;
					L.mod = MOD_CONST;
				}
				else if (scaled)
				{
					// as the scaled path below: its columns from c0, at most its width and one
					const int columns = std::min(px1 - px0, scaled->w + 1 - skip);
					if (columns <= 0)
						return; // draws nothing
					L.kind = Layer::SCALED;
					L.scaled = scaled;
					L.texture = state.texture;
					L.c0 = skip;
					L.qy0 = qy0;
					L.x1 = px0 + columns;
				}
				else if (texMode == TEX_DIRECT)
				{
					L.kind = Layer::ROWS;
					L.texture = state.texture;
					L.v0 = texelV0;
					L.dv = texelDv;
					L.qy0 = qy0;
					L.ux = uxStart;
				}
				else
					L.ok = false; // sampled: drawn on its own
				return;
			}

			// an opaque untextured rectangle: a fill
			if (texMode == TEX_NONE && mod == MOD_CONST && (s.color >> 24) == 255 && blend == BLEND_NORMAL && maskTest == 0)
			{
				for (int y = py0; y < py1; y++)
					std::fill_n(mPx + (size_t)y * mWidth + px0, px1 - px0, s.color);
				mStats.pixels += (uint64_t)(px1 - px0) * (py1 - py0);
				mStats.kinds[PX_FILL] += (uint64_t)(px1 - px0) * (py1 - py0);
				return;
			}

			// an opaque picture drawn as it is: its rows copied
			if (texMode == TEX_DIRECT && mod == MOD_NONE && blend == BLEND_NORMAL && state.texture->opaque && s.saturation == 256 && maskTest == 0)
			{
				const int ux = uxStart;
				for (int y = py0; y < py1; y++)
				{
					const int vy = floorInt(texelV0 + (y - qy0) * texelDv);
					copyPixels(mPx + (size_t)y * mWidth + px0, tex.px + (size_t)vy * tex.w + ux, px1 - px0);
				}
				mStats.pixels += (uint64_t)(px1 - px0) * (py1 - py0);
				mStats.kinds[PX_COPY] += (uint64_t)(px1 - px0) * (py1 - py0);
				return;
			}

			const int rowMod = (mod == MOD_VARY && rowColor) ? MOD_CONST : mod;

			// the row's colour (or colours, from left to right) when they vary from top to bottom
			auto rowColors = [&](int y)
			{
				const float ty = (y + 0.5f - y0) / h;
				const ColorF left = mixColor(c00, c01, ty);
				if (rowColor)
				{
					s.color = ((uint32_t)(left.a + 0.5f) << 24) | ((uint32_t)(left.r + 0.5f) << 16) | ((uint32_t)(left.g + 0.5f) << 8) | (uint32_t)(left.b + 0.5f);
					return;
				}
				const ColorF right = mixColor(c10, c11, ty);
				const float tx = (qx0 + 0.5f - x0) / w;
				const ColorF start = mixColor(left, right, tx);
				s.dr = toFixed((right.r - left.r) / w); s.dg = toFixed((right.g - left.g) / w);
				s.db = toFixed((right.b - left.b) / w); s.da = toFixed((right.a - left.a) / w);
				s.r = toFixed(start.r) + skip * s.dr; s.g = toFixed(start.g) + skip * s.dg;
				s.b = toFixed(start.b) + skip * s.db; s.a = toFixed(start.a) + skip * s.da;
			};

			// the scaled picture's rows, from the quad's first row and column, drawn 1:1
			if (scaled)
			{
				thread_local ScaledScratch scratch;
				scratch.owner = nullptr; // its texture rows are another picture's (one let go of may have had this address)
				// the picture's column at the first drawn one (it has a column more than the quad's rounded
				// width, for the one more a quad on a fraction of a pixel may cover)
				const int c0 = skip;
				const int columns = std::min(px1 - px0, scaled->w + 1 - c0);
				if (columns <= 0)
					return;
				const bool copy = state.texture->opaque && mod == MOD_NONE && blend == BLEND_NORMAL && s.saturation == 256 && maskTest == 0;
				const SpanFn finish = pickRowSpan(pickSpanFor(TEX_DIRECT, rowMod, blend, maskTest != 0, s.saturation != 256), state.texture, mod, s.color, blend, maskTest != 0, s.saturation != 256);
				Tex row;
				row.w = columns;
				row.h = 1;
				for (int y = py0; y < py1; y++)
				{
					const int r = std::min(y - qy0, scaled->h);
					const uint32_t* sampled = scaledRow(*scaled, state.texture, r, scratch) + c0;
					const size_t offset = (size_t)y * mWidth + px0;
					if (copy)
					{
						copyPixels(mPx + offset, sampled, columns);
						continue;
					}
					if (mod == MOD_VARY)
						rowColors(y);
					Span fs = s;
					fs.u = 0; fs.du = 1 << 16;
					fs.v = 0; fs.dv = 0;
					row.px = sampled;
					finish(mPx + offset, stencil ? stencil + offset : nullptr, maskTest, columns, row, fs);
				}
				mStats.pixels += (uint64_t)columns * (py1 - py0);
				mStats.kinds[PX_TEX_LINEAR] += (uint64_t)columns * (py1 - py0);
				return;
			}

			// A picture sampled (bilinear or nearest) and not made much smaller: every row of the quad reads the same
			// texture columns, so the columns' texels and weights are worked out once, each texture row is
			// sampled once, and the frame rows that fall on it reuse it; then the row is drawn as a 1:1 picture.
			// The pixels are fetchLinear's and fetchNearest's.
			static const bool rowsSampled = getenv("ES_SOFT_ROWS") == nullptr || strcmp(getenv("ES_SOFT_ROWS"), "0") != 0; // 0: per pixel, to compare
			// Made smaller too (a logo in the carousel, a picture shrunk to its tile), however much: a frame row
			// samples at most two texture rows across, as many sums as a pixel's own sample takes, and the rows are
			// sampled four pixels at a time (sampleAcross); the v of each row and the u of each column are the
			// per-pixel path's own, so the pixels are the same.
			if (rowsSampled && (texMode == TEX_LINEAR || texMode == TEX_NEAREST) && px1 - px0 >= 8)
			{
				const int n = px1 - px0;
				const bool linear = texMode == TEX_LINEAR;
				if ((int)mRowOut.size() < n)
				{
					mRowA.resize(n); mRowB.resize(n); mRowOut.resize(n);
					mColX0.resize(n); mColX1.resize(n); mColFx.resize(n);
				}

				for (int i = 0; i < n; i++)
				{
					const int u = uStart + i * du;
					mColX0[i] = wrapCoord(u >> 16, tex.w, tex.repeat);
					if (linear)
					{
						mColX1[i] = wrapCoord((u >> 16) + 1, tex.w, tex.repeat);
						mColFx[i] = (uint16_t)(((uint32_t)u >> 8) & 0xFF);
					}
				}

#ifdef SOFT_NEON
				// Nearest and made bigger (or 1:1) across, not repeated: 8 pixels in a row read at most 8 texels
				// side by side, from the first one's (mNearAt); each pixel's 4 bytes picked from those 32 by
				// index (vtbl4), the indices worked out once from the columns: the same texels, 8 at a time.
				// Carbon's grid background (853 texels to 1920 pixels) and its tiles sampled a texel at a time.
				int nearBlocks = 0; // the first ones of the row done this way (their 8 texels inside the row)
				if (!linear && !tex.repeat && du > 0 && du <= (1 << 16) && neonEnabled())
				{
					const int blocks = n / 8;
					if ((int)mNearAt.size() < blocks)
					{
						mNearAt.resize(blocks);
						mNearIdx.resize((size_t)blocks * 32);
					}
					const int32_t* x0s = mColX0.data();
					for (int b = 0; b < blocks; b++)
					{
						const int at = x0s[b * 8];
						if (at < 0 || at + 8 > tex.w || x0s[b * 8 + 7] - at > 7)
							break;
						mNearAt[b] = at;
						// a pixel's 4 indices: its texel's bytes, 4 * offset + 0, 1, 2, 3 (little endian)
						uint32_t* idx = (uint32_t*)(mNearIdx.data() + (size_t)b * 32);
						for (int j = 0; j < 8; j++)
							idx[j] = (uint32_t)(x0s[b * 8 + j] - at) * 0x04040404u + 0x03020100u;
						nearBlocks = b + 1;
					}
				}
#endif

				uint32_t* rowA = mRowA.data();
				uint32_t* rowB = mRowB.data();
				uint32_t* out = mRowOut.data();
				int keyA = -1, keyB = -1;
				auto sample = [&](int r, uint32_t* into)
				{
					const uint32_t* src = tex.px + (size_t)r * tex.w;
					const int32_t* x0s = mColX0.data();
					if (linear)
						sampleAcross(into, src, x0s, mColX1.data(), mColFx.data(), n);
					else
					{
						int i = 0;
#ifdef SOFT_NEON
						for (int b = 0; b < nearBlocks; b++, i += 8)
						{
							const uint8_t* texels = (const uint8_t*)(src + mNearAt[b]);
							const uint8x16_t lo = vld1q_u8(texels), hi = vld1q_u8(texels + 16);
							uint8x8x4_t table;
							table.val[0] = vget_low_u8(lo); table.val[1] = vget_high_u8(lo);
							table.val[2] = vget_low_u8(hi); table.val[3] = vget_high_u8(hi);
							const uint8_t* idx = mNearIdx.data() + (size_t)b * 32;
							const uint8x16_t i0 = vld1q_u8(idx), i1 = vld1q_u8(idx + 16);
							vst1q_u8((uint8_t*)(into + i), vcombine_u8(vtbl4_u8(table, vget_low_u8(i0)), vtbl4_u8(table, vget_high_u8(i0))));
							vst1q_u8((uint8_t*)(into + i + 4), vcombine_u8(vtbl4_u8(table, vget_low_u8(i1)), vtbl4_u8(table, vget_high_u8(i1))));
						}
#endif
						for (; i < n; i++)
							into[i] = src[x0s[i]];
					}
				};
				// row r sampled, in a slot that does not hold row keep
				auto rowOf = [&](int r, int keep) -> const uint32_t*
				{
					if (keyA == r) return rowA;
					if (keyB == r) return rowB;
					if (keyA != keep) { sample(r, rowA); keyA = r; return rowA; }
					sample(r, rowB); keyB = r; return rowB;
				};

				// opaque, untinted, plain: the sampled row is the frame's row
				const bool copy = state.texture->opaque && mod == MOD_NONE && blend == BLEND_NORMAL && s.saturation == 256 && maskTest == 0;
				// opaque and covering what is under it (a tint at full alpha): a frame row that samples the same
				// texture row as the one above is the same as it
				const bool covers = state.texture->opaque && blend == BLEND_NORMAL && maskTest == 0 &&
					(mod == MOD_NONE || (mod == MOD_CONST && (s.color >> 24) == 255));
				const SpanFn finish = pickRowSpan(pickSpanFor(TEX_DIRECT, rowMod, blend, maskTest != 0, s.saturation != 256), state.texture, mod, s.color, blend, maskTest != 0, s.saturation != 256);
				Tex row;
				row.w = n;
				row.h = 1;
				int lastKey = -1; // the texture row (nearest) the row above drew

				// A flat middle (a nine-patch's: a shadow's, a frame's), whose border varies: the columns
				// [flatX0, flatX1) whose taps, in the texture rows [flatY0, flatY1], all read texels of one value
				// draw that value's colour, as a flat quad does (a bilinear sample of four equal texels is that
				// texel); the rest is sampled as before. The pixels are the same.
				int flatX0 = 0, flatX1 = 0, flatY0 = 0, flatY1 = -1;
				uint32_t flatTexel = 0;
				const bool flatMiddle = !copy && rowMod != MOD_VARY && !tex.repeat && n >= 16 && py1 - py0 >= 4 && [&]
				{
					auto rowsAt = [&](int y, int& r0, int& r1)
					{
						const int vy = toFixed(texelV0 + (y - qy0) * texelDv);
						r0 = wrapCoord(vy >> 16, tex.h, false);
						r1 = linear ? wrapCoord((vy >> 16) + 1, tex.h, false) : r0;
					};
					const int32_t* x0s = mColX0.data();
					const int32_t* x1s = linear ? mColX1.data() : mColX0.data();
					int a0, a1, b0, b1, c0, c1;
					rowsAt(py0, a0, a1);
					rowsAt(py1 - 1, b0, b1);
					rowsAt((py0 + py1) / 2, c0, c1);
					const int rowLo = std::min(a0, b0), rowHi = std::max(a1, b1);
					const int colLo = std::min(x0s[0], x0s[n - 1]), colHi = std::max(x1s[0], x1s[n - 1]);
					const int ic = n / 2, cx = x0s[ic];
					const uint32_t texel = tex.px[(size_t)c0 * tex.w + cx];
					auto at = [&](int x, int y) { return tex.px[(size_t)y * tex.w + x]; };
					int budget = 65536; // texels looked at, at most
					int ay = c0, by = c0;
					while (ay > rowLo && at(cx, ay - 1) == texel) ay--;
					while (by < rowHi && at(cx, by + 1) == texel) by++;
					budget -= by - ay + 1;
					auto columnFlat = [&](int x)
					{
						budget -= by - ay + 1;
						for (int y = ay; y <= by; y++)
							if (at(x, y) != texel)
								return false;
						return true;
					};
					int ax = cx, bx = cx;
					while (ax > colLo && budget > 0 && columnFlat(ax - 1)) ax--;
					while (bx < colHi && budget > 0 && columnFlat(bx + 1)) bx++;
					if (budget <= 0)
						return false;
					auto inside = [&](int i) { return x0s[i] >= ax && x0s[i] <= bx && x1s[i] >= ax && x1s[i] <= bx; };
					if (!inside(ic))
						return false;
					int ia = ic, ib = ic + 1;
					while (ia > 0 && inside(ia - 1)) ia--;
					while (ib < n && inside(ib)) ib++;
					if (ib - ia < 16)
						return false;
					flatX0 = ia; flatX1 = ib; flatY0 = ay; flatY1 = by;
					flatTexel = s.saturation < 256 ? desaturate(texel, s.saturation) : texel;
					return true;
				}();
				const SpanFn flatFill = pickSpanFor(TEX_NONE, MOD_CONST, blend, maskTest != 0, false);

				for (int y = py0; y < py1; y++)
				{
					const int v = toFixed(texelV0 + (y - qy0) * texelDv);
					// this row's taps in the flat middle's texture rows
					bool flatRow = false;
					if (flatMiddle)
					{
						const int r0 = wrapCoord(v >> 16, tex.h, false), r1 = linear ? wrapCoord((v >> 16) + 1, tex.h, false) : r0;
						flatRow = r0 >= flatY0 && r0 <= flatY1 && r1 >= flatY0 && r1 <= flatY1;
					}
					if (!linear && covers)
					{
						const int key = wrapCoord(v >> 16, tex.h, tex.repeat);
						if (key == lastKey)
						{
							copyPixels(mPx + (size_t)y * mWidth + px0, mPx + (size_t)(y - 1) * mWidth + px0, n);
							continue;
						}
						lastKey = key;
					}
					const uint32_t* sampled;
					if (!linear)
						sampled = rowOf(wrapCoord(v >> 16, tex.h, tex.repeat), -1);
					else
					{
						const int r0 = wrapCoord(v >> 16, tex.h, tex.repeat), r1 = wrapCoord((v >> 16) + 1, tex.h, tex.repeat);
						const uint32_t* top = rowOf(r0, r1);
						const uint32_t* bottom = rowOf(r1, r0);
						const uint32_t fy = ((uint32_t)v >> 8) & 0xFF;
						if (flatRow)
						{
							// the flat middle is not sampled
							lerpRows(out, top, bottom, fy, flatX0);
							lerpRows(out + flatX1, top + flatX1, bottom + flatX1, fy, n - flatX1);
						}
						else
							lerpRows(out, top, bottom, fy, n);
						sampled = out;
					}

					const size_t offset = (size_t)y * mWidth + px0;
					if (copy)
					{
						copyPixels(mPx + offset, sampled, n);
						continue;
					}
					if (mod == MOD_VARY)
						rowColors(y);
					Span fs = s;
					fs.u = 0; fs.du = 1 << 16; // the row read 1:1
					fs.v = 0; fs.dv = 0;
					row.px = sampled;
					uint32_t* const to = mPx + offset;
					const uint8_t* const mask = stencil ? stencil + offset : nullptr;
					if (!flatRow)
					{
						finish(to, mask, maskTest, n, row, fs);
						continue;
					}
					// sampled left of the flat middle, its colour across it (the texel as the row's colour tints
					// it), sampled right of it
					finish(to, mask, maskTest, flatX0, row, fs);
					Span flat = s;
					flat.color = rowMod == MOD_CONST ? modulate(flatTexel, s.color) : flatTexel;
					flatFill(to + flatX0, mask ? mask + flatX0 : nullptr, maskTest, flatX1 - flatX0, row, flat);
					fs.u = flatX1 << 16;
					finish(to + flatX1, mask ? mask + flatX1 : nullptr, maskTest, n - flatX1, row, fs);
				}

				mStats.pixels += (uint64_t)n * (py1 - py0);
				mStats.kinds[linear ? PX_TEX_LINEAR : PX_TEX_NEAREST] += (uint64_t)n * (py1 - py0);
				return;
			}

			SpanFn fn = pickSpanFor(texMode, rowMod, blend, maskTest != 0, s.saturation != 256);
			if (texMode == TEX_DIRECT)
				fn = pickRowSpan(fn, state.texture, mod, s.color, blend, maskTest != 0, s.saturation != 256);
#ifdef SOFT_NEON
			// an opaque colour tested against the stencil (a game tile's background): its runs filled (an
			// opaque colour not tested is filled above)
			if (texMode == TEX_NONE && mod == MOD_CONST && (s.color >> 24) == 255 && blend == BLEND_NORMAL && maskTest != 0 &&
				s.saturation == 256 && neonEnabled())
				fn = &spanMaskedRuns<&spanFill>;
#endif

			// a picture 1:1 with clear runs (an overlay that leaves much of the frame as it is): drawn
			// between them (blending a clear texel leaves the pixel as it was)
			if (texMode == TEX_DIRECT && blend == BLEND_NORMAL && mod != MOD_VARY && !state.texture->clearAt.empty())
			{
				const Texture* t = state.texture;
				const int n = px1 - px0;
				uint64_t drawn = 0;
				for (int y = py0; y < py1; y++)
				{
					const int vy = floorInt(texelV0 + (y - qy0) * texelDv);
					const size_t offset = (size_t)y * mWidth + px0;
					s.v = vy << 16;
					s.du = du;
					s.dv = 0;
					// the row's columns [ux, ux + n), drawn up to each clear run in it, then after it
					const int ux = uxStart;
					int at = 0;
					auto draw = [&](int from, int to)
					{
						if (to <= from)
							return;
						s.u = (ux + from) << 16;
						fn(mPx + offset + from, stencil ? stencil + offset + from : nullptr, maskTest, to - from, tex, s);
						drawn += (uint64_t)(to - from);
					};
					for (uint32_t r = t->clearAt[vy]; r < t->clearAt[vy + 1]; r += 2)
					{
						const int c0 = (int)t->clear[r] - ux, c1 = (int)t->clear[r + 1] - ux;
						if (c1 <= at)
							continue;
						if (c0 >= n)
							break;
						draw(at, std::max(at, c0));
						at = std::min(n, c1);
					}
					draw(at, n);
				}
				mStats.pixels += drawn;
				mStats.kinds[PX_TEX_1TO1] += drawn;
				return;
			}

			for (int y = py0; y < py1; y++)
			{
				if (texMode != TEX_NONE)
				{
					const float tv = texelV0 + (y - qy0) * texelDv;
					s.u = uStart;
					s.du = du;
					s.v = toFixed(tv);
					s.dv = 0;
					if (texMode == TEX_DIRECT)
					{
						s.u = uxStart << 16;
						s.v = floorInt(tv) << 16;
					}
				}

				if (mod == MOD_VARY)
					rowColors(y);

				const size_t offset = (size_t)y * mWidth + px0;
				fn(mPx + offset, stencil ? stencil + offset : nullptr, maskTest, px1 - px0, tex, s);
			}

			mStats.pixels += (uint64_t)(px1 - px0) * (py1 - py0);
			static const int kindOfTex[4] = { PX_COLOR, PX_TEX_NEAREST, PX_TEX_LINEAR, PX_TEX_1TO1 };
			mStats.kinds[kindOfTex[texMode]] += (uint64_t)(px1 - px0) * (py1 - py0);
		}

		void Raster::drawTriangle(const SVertex& a, const SVertex& b, const SVertex& c, const DrawState& state, const Clip& clip)
		{
			const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
			if (std::fabs(area) < 1e-6f)
				return;

			mStats.triangles++;

			const float inv = 1.0f / area;
			auto gradient = [&](float fa, float fb, float fc, float& dx, float& dy)
			{
				dx = ((fb - fa) * (c.y - a.y) - (fc - fa) * (b.y - a.y)) * inv;
				dy = ((fc - fa) * (b.x - a.x) - (fb - fa) * (c.x - a.x)) * inv;
			};

			Span s;
			s.saturation = saturationOf(state);
			s.srcBlend = state.srcBlend;
			s.dstBlend = state.dstBlend;
			const int blend = blendModeOf(state);

			Tex tex;
			int texMode = TEX_NONE;
			float uA = 0, uDx = 0, uDy = 0, vA = 0, vDx = 0, vDy = 0;
			if (state.texture != nullptr && !state.texture->pixels.empty())
			{
				const Texture* t = state.texture;
				tex.px = t->pixels.data();
				tex.w = t->width;
				tex.h = t->height;
				tex.repeat = t->repeat;
				gradient(a.u * t->width, b.u * t->width, c.u * t->width, uDx, uDy);
				gradient(a.v * t->height, b.v * t->height, c.v * t->height, vDx, vDy);
				uA = a.u * t->width;
				vA = a.v * t->height;
				const float scale = std::sqrt(std::fabs(uDx * vDy - uDy * vDx)); // texels per pixel
				if (scale <= 1.0f && !t->linear)
					texMode = TEX_NEAREST;
				else
				{
					texMode = TEX_LINEAR;
					uA -= 0.5f;
					vA -= 0.5f;
				}
			}

			const bool sameColor = a.color == b.color && a.color == c.color;
			int mod = MOD_VARY;
			ColorF ca = unpack(a.color), cb = unpack(b.color), cc = unpack(c.color);
			float rDx = 0, rDy = 0, gDx = 0, gDy = 0, bDx = 0, bDy = 0, aDx = 0, aDy = 0;
			if (sameColor)
			{
				mod = a.color == 0xFFFFFFFF && texMode != TEX_NONE ? MOD_NONE : MOD_CONST;
				s.color = a.color;
				if (texMode == TEX_NONE && (s.color >> 24) == 0 && blend == BLEND_NORMAL)
					return;
			}
			else
			{
				gradient(ca.r, cb.r, cc.r, rDx, rDy);
				gradient(ca.g, cb.g, cc.g, gDx, gDy);
				gradient(ca.b, cb.b, cc.b, bDx, bDy);
				gradient(ca.a, cb.a, cc.a, aDx, aDy);
			}

			const uint8_t* stencil = state.stencil != StencilTest::Off ? mSt : nullptr;
			const int maskTest = state.stencil == StencilTest::Inside ? 1 : (state.stencil == StencilTest::Outside ? 2 : 0);
			const SpanFn fn = pickSpanFor(texMode, mod, blend, maskTest != 0, s.saturation != 256);

			// each row's values at its own first column, then a step a column the clip left out, so a pixel is
			// the same however the triangle is clipped
			coverTriangle(a, b, c, clip.x0, clip.y0, clip.x1, clip.y1, [&](int y, int x0, int x1, int first)
			{
				const float dx = first + 0.5f - a.x, dy = y + 0.5f - a.y;
				const int skipped = x0 - first;
				if (texMode != TEX_NONE)
				{
					s.du = toFixed(uDx);
					s.dv = toFixed(vDx);
					s.u = toFixed(uA + uDx * dx + uDy * dy) + skipped * s.du;
					s.v = toFixed(vA + vDx * dx + vDy * dy) + skipped * s.dv;
				}
				if (mod == MOD_VARY)
				{
					s.dr = toFixed(rDx); s.r = toFixed(ca.r + rDx * dx + rDy * dy) + skipped * s.dr;
					s.dg = toFixed(gDx); s.g = toFixed(ca.g + gDx * dx + gDy * dy) + skipped * s.dg;
					s.db = toFixed(bDx); s.b = toFixed(ca.b + bDx * dx + bDy * dy) + skipped * s.db;
					s.da = toFixed(aDx); s.a = toFixed(ca.a + aDx * dx + aDy * dy) + skipped * s.da;
				}
				const size_t offset = (size_t)y * mWidth + x0;
				fn(mPx + offset, stencil ? stencil + offset : nullptr, maskTest, x1 - x0, tex, s);
				mStats.pixels += (uint64_t)(x1 - x0);
				mStats.kinds[PX_TRIANGLE] += (uint64_t)(x1 - x0);
			});
		}

		void Raster::drawLine(const SVertex& a, const SVertex& b, const DrawState& state, const Clip& clip)
		{
			const float dx = b.x - a.x, dy = b.y - a.y;
			const int steps = ceilInt(std::max(std::fabs(dx), std::fabs(dy)));
			if (steps <= 0)
				return;

			const int blend = blendModeOf(state);
			const ColorF ca = unpack(a.color), cb = unpack(b.color);
			for (int i = 0; i < steps; i++)
			{
				const float t = (i + 0.5f) / steps;
				const int x = floorInt(a.x + dx * t);
				const int y = floorInt(a.y + dy * t);
				if (x < clip.x0 || x >= clip.x1 || y < clip.y0 || y >= clip.y1)
					continue;
				const size_t offset = (size_t)y * mWidth + x;
				if (state.stencil != StencilTest::Off && ((state.stencil == StencilTest::Inside) != (mSt[offset] == 1)))
					continue;
				const ColorF c = mixColor(ca, cb, t);
				const uint32_t color = ((uint32_t)(c.a + 0.5f) << 24) | ((uint32_t)(c.r + 0.5f) << 16) | ((uint32_t)(c.g + 0.5f) << 8) | (uint32_t)(c.b + 0.5f);
				uint32_t& d = mPx[offset];
				d = blend == BLEND_NORMAL ? blendNormal(color, d) : (blend == BLEND_ADD ? blendAdd(color, d) : blendGeneric(color, d, state.srcBlend, state.dstBlend));
			}
		}

		void Raster::setStencil(const SVertex* vertices, int count, const DrawState& state)
		{
			// clear what the last shape marked
			resetStencil();

			// A clip with nothing in it (a scissor that misses this band or part: the renderer narrows each
			// command's scissor to the part it draws) marks nothing. Its box below would start inverted in both
			// axes and pass for a marked one, and the next resetStencil cleared it: rows of the other band
			// (drawing at the same time) or, with a scissor off the screen's left or top, memory before or
			// after the stencil.
			const Clip clip = clipOf(state);
			if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1)
				return;
			Clip box = { clip.x1, clip.y1, clip.x0, clip.y0 };
			for (int i = 1; i + 1 < count; i++)
			{
				coverTriangle(vertices[0], vertices[i], vertices[i + 1], clip.x0, clip.y0, clip.x1, clip.y1, [&](int y, int x0, int x1, int)
				{
					std::memset(mSt + (size_t)y * mWidth + x0, 1, x1 - x0);
					box.x0 = std::min(box.x0, x0); box.x1 = std::max(box.x1, x1);
					box.y0 = std::min(box.y0, y); box.y1 = std::max(box.y1, y + 1);
				});
			}
			mStencilBox = box.x0 < box.x1 && box.y0 < box.y1 ? box : Clip{ 0, 0, 0, 0 };
		}

		Bounds drawBounds(const SVertex* v, int count, Topology topology, const DrawState& state, int width, int height)
		{
			Bounds box = { 0, 0, width, height };
			if (count <= 0)
				return Bounds();
			float x0 = v[0].x, y0 = v[0].y, x1 = v[0].x, y1 = v[0].y;
			bool finite = true;
			for (int i = 0; i < count; i++)
			{
				finite = finite && std::isfinite(v[i].x) && std::isfinite(v[i].y);
				x0 = std::min(x0, v[i].x); x1 = std::max(x1, v[i].x);
				y0 = std::min(y0, v[i].y); y1 = std::max(y1, v[i].y);
			}
			if (finite)
			{
				const int margin = topology == Topology::Lines ? 2 : 1;
				auto limit = [](float value, int size) { return std::max(-8.0f, std::min((float)size + 8.0f, value)); };
				box.x0 = std::max(box.x0, floorInt(limit(x0, width)) - margin);
				box.y0 = std::max(box.y0, floorInt(limit(y0, height)) - margin);
				box.x1 = std::min(box.x1, ceilInt(limit(x1, width)) + margin);
				box.y1 = std::min(box.y1, ceilInt(limit(y1, height)) + margin);
			}
			if (state.scissor)
			{
				box.x0 = std::max(box.x0, state.sx0);
				box.y0 = std::max(box.y0, state.sy0);
				box.x1 = std::min(box.x1, state.sx1);
				box.y1 = std::min(box.y1, state.sy1);
			}
			return box.empty() ? Bounds() : box;
		}

		void findClearRuns(Texture& texture)
		{
			texture.clearAt.clear();
			texture.clear.clear();
			const int w = texture.width, h = texture.height;
			if (texture.opaque || w < 64 || h < 1 || texture.pixels.size() < (size_t)w * h)
				return;
			std::vector<uint32_t> at, runs;
			at.reserve(h + 1);
			for (int y = 0; y < h; y++)
			{
				at.push_back((uint32_t)runs.size());
				const uint32_t* row = texture.pixels.data() + (size_t)y * w;
				for (int x = 0; x < w;)
				{
					if (row[x] >> 24)
					{
						x++;
						continue;
					}
					int end = x;
					while (end < w && (row[end] >> 24) == 0)
						end++;
					if (end - x >= 32)
					{
						runs.push_back((uint32_t)x);
						runs.push_back((uint32_t)end);
					}
					x = end;
				}
			}
			at.push_back((uint32_t)runs.size());
			// worth it only when the runs are much of the picture
			uint64_t clear = 0;
			for (size_t i = 0; i < runs.size(); i += 2)
				clear += runs[i + 1] - runs[i];
			if (clear * 8 < (uint64_t)w * h)
				return;
			texture.clearAt.swap(at);
			texture.clear.swap(runs);
		}

		void scaledFrame()
		{
			ScaledCache& cache = scaledCache();
			const uint64_t frame = cache.frame.fetch_add(1) + 1;
			// pictures not drawn for a while (600 drawn frames) let go of
			std::lock_guard<std::mutex> guard(cache.lock);
			for (auto it = cache.entries.begin(); it != cache.entries.end();)
				if (frame - (*it)->used > 600)
				{
					cache.bytes -= (*it)->bytes();
					it = cache.entries.erase(it);
				}
				else
					++it;
		}

		size_t scaledBytes()
		{
			ScaledCache& cache = scaledCache();
			std::lock_guard<std::mutex> guard(cache.lock);
			return cache.bytes;
		}
	}
}
