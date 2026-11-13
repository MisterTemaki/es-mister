#include "SoftRaster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

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

				for (int i = first; i < end; i += 8)
				{
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
#endif

			// spanT, or its NEON version where there is one: plain is no stencil and no desaturation
			SpanFn pickSpanFor(int tex, int mod, int blend, bool plain)
			{
#ifdef SOFT_NEON
				if (plain && blend == BLEND_NORMAL && neonEnabled())
				{
					if (tex == TEX_NONE && mod != MOD_VARY)
						return &spanNeon<TEX_NONE, MOD_CONST>;
					if (mod == MOD_NONE || mod == MOD_CONST)
					{
						const bool constant = mod == MOD_CONST;
						switch (tex)
						{
						case TEX_DIRECT: return constant ? &spanNeon<TEX_DIRECT, MOD_CONST> : &spanNeon<TEX_DIRECT, MOD_NONE>;
						case TEX_NEAREST: return constant ? &spanNeon<TEX_NEAREST, MOD_CONST> : &spanNeon<TEX_NEAREST, MOD_NONE>;
						case TEX_LINEAR: return constant ? &spanNeon<TEX_LINEAR, MOD_CONST> : &spanNeon<TEX_LINEAR, MOD_NONE>;
						}
					}
				}
#endif
				(void)plain;
				return pickSpan(tex, mod, blend);
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
				return (int)std::floor(f);
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

				const int yStart = std::max(cy0, (int)std::ceil(v0->y - 0.5f));
				const int yEnd = std::min(cy1, (int)std::ceil(v2->y - 0.5f));
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
					const int x0 = std::max(cx0, (int)std::ceil(left - 0.5f));
					const int x1 = std::min(cx1, (int)std::ceil(right - 0.5f));
					if (x0 < x1)
						f(y, x0, x1);
				}
			}
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

			const int qy0 = (int)std::ceil(y0 - 0.5f); // its first row, unclipped: the texture rows count from it
			const int px0 = std::max(clip.x0, (int)std::ceil(x0 - 0.5f));
			const int px1 = std::min(clip.x1, (int)std::ceil(x1 - 0.5f));
			const int py0 = std::max(clip.y0, qy0);
			const int py1 = std::min(clip.y1, (int)std::ceil(y1 - 0.5f));
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
			float texelU0 = 0, texelDu = 0, texelV0 = 0, texelDv = 0; // at the first pixel's centre, per pixel
			if (state.texture != nullptr && !state.texture->pixels.empty())
			{
				const Texture* t = state.texture;
				tex.px = t->pixels.data();
				tex.w = t->width;
				tex.h = t->height;
				tex.repeat = t->repeat;

				texelDu = (u1 - u0) * t->width / w;
				texelDv = (t1 - t0) * t->height / h;
				texelU0 = (u0 + (px0 + 0.5f - x0) * (u1 - u0) / w) * t->width;
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
					const int ux = (int)std::floor(texelU0);
					const int ulast = ux + (px1 - px0 - 1);
					const int vfirst = (int)std::floor(texelV0 + (py0 - qy0) * texelDv);
					const int vlast = (int)std::floor(texelV0 + (py1 - 1 - qy0) * texelDv);
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

			int mod = MOD_VARY;
			if (sameColor)
			{
				mod = v[0].color == 0xFFFFFFFF && texMode != TEX_NONE ? MOD_NONE : MOD_CONST;
				s.color = v[0].color;
				if (texMode == TEX_NONE && (s.color >> 24) == 0 && blend == BLEND_NORMAL)
					return; // transparent: nothing to draw
			}

			const uint8_t* stencil = state.stencil != StencilTest::Off ? mSt : nullptr;
			const int maskTest = state.stencil == StencilTest::Inside ? 1 : (state.stencil == StencilTest::Outside ? 2 : 0);

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
				const int ux = (int)std::floor(texelU0);
				for (int y = py0; y < py1; y++)
				{
					const int vy = (int)std::floor(texelV0 + (y - qy0) * texelDv);
					memcpy(mPx + (size_t)y * mWidth + px0, tex.px + (size_t)vy * tex.w + ux, (size_t)(px1 - px0) * 4);
				}
				mStats.pixels += (uint64_t)(px1 - px0) * (py1 - py0);
				mStats.kinds[PX_COPY] += (uint64_t)(px1 - px0) * (py1 - py0);
				return;
			}

			const int rowMod = (mod == MOD_VARY && rowColor) ? MOD_CONST : mod;
			const SpanFn fn = pickSpanFor(texMode, rowMod, blend, maskTest == 0 && s.saturation == 256);
			const int du = toFixed(texelDu);

			for (int y = py0; y < py1; y++)
			{
				const float ty = (y + 0.5f - y0) / h;

				if (texMode != TEX_NONE)
				{
					const float tv = texelV0 + (y - qy0) * texelDv;
					s.u = toFixed(texelU0);
					s.du = du;
					s.v = toFixed(tv);
					s.dv = 0;
					if (texMode == TEX_DIRECT)
					{
						s.u = (int)std::floor(texelU0) << 16;
						s.v = (int)std::floor(tv) << 16;
					}
				}

				if (mod == MOD_VARY)
				{
					const ColorF left = mixColor(c00, c01, ty);
					if (rowColor)
					{
						s.color = ((uint32_t)(left.a + 0.5f) << 24) | ((uint32_t)(left.r + 0.5f) << 16) | ((uint32_t)(left.g + 0.5f) << 8) | (uint32_t)(left.b + 0.5f);
					}
					else
					{
						const ColorF right = mixColor(c10, c11, ty);
						const float tx = (px0 + 0.5f - x0) / w;
						const ColorF start = mixColor(left, right, tx);
						s.r = toFixed(start.r); s.g = toFixed(start.g); s.b = toFixed(start.b); s.a = toFixed(start.a);
						s.dr = toFixed((right.r - left.r) / w); s.dg = toFixed((right.g - left.g) / w);
						s.db = toFixed((right.b - left.b) / w); s.da = toFixed((right.a - left.a) / w);
					}
				}

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
			const SpanFn fn = pickSpanFor(texMode, mod, blend, maskTest == 0 && s.saturation == 256);

			coverTriangle(a, b, c, clip.x0, clip.y0, clip.x1, clip.y1, [&](int y, int x0, int x1)
			{
				const float dx = x0 + 0.5f - a.x, dy = y + 0.5f - a.y;
				if (texMode != TEX_NONE)
				{
					s.u = toFixed(uA + uDx * dx + uDy * dy);
					s.v = toFixed(vA + vDx * dx + vDy * dy);
					s.du = toFixed(uDx);
					s.dv = toFixed(vDx);
				}
				if (mod == MOD_VARY)
				{
					s.r = toFixed(ca.r + rDx * dx + rDy * dy); s.dr = toFixed(rDx);
					s.g = toFixed(ca.g + gDx * dx + gDy * dy); s.dg = toFixed(gDx);
					s.b = toFixed(ca.b + bDx * dx + bDy * dy); s.db = toFixed(bDx);
					s.a = toFixed(ca.a + aDx * dx + aDy * dy); s.da = toFixed(aDx);
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
			const int steps = (int)std::ceil(std::max(std::fabs(dx), std::fabs(dy)));
			if (steps <= 0)
				return;

			const int blend = blendModeOf(state);
			const ColorF ca = unpack(a.color), cb = unpack(b.color);
			for (int i = 0; i < steps; i++)
			{
				const float t = (i + 0.5f) / steps;
				const int x = (int)std::floor(a.x + dx * t);
				const int y = (int)std::floor(a.y + dy * t);
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

			const Clip clip = clipOf(state);
			Clip box = { clip.x1, clip.y1, clip.x0, clip.y0 };
			for (int i = 1; i + 1 < count; i++)
			{
				coverTriangle(vertices[0], vertices[i], vertices[i + 1], clip.x0, clip.y0, clip.x1, clip.y1, [&](int y, int x0, int x1)
				{
					std::memset(mSt + (size_t)y * mWidth + x0, 1, x1 - x0);
					box.x0 = std::min(box.x0, x0); box.x1 = std::max(box.x1, x1);
					box.y0 = std::min(box.y0, y); box.y1 = std::max(box.y1, y + 1);
				});
			}
			mStencilBox = box.x0 < box.x1 ? box : Clip{ 0, 0, 0, 0 };
		}
	}
}
