#include "SoftScale.h"

#include <algorithm>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace Renderer
{
	namespace Soft
	{
		namespace
		{
			// the mean of two pixels, each byte rounded up (as NEON's vrhadd)
			inline uint32_t mean(uint32_t a, uint32_t b)
			{
				return (a | b) - (((a ^ b) & 0xFEFEFEFEu) >> 1);
			}

			// a row of the screen built here (in the cache) and copied to it whole: the framebuffer's memory
			// takes long runs of wide stores best. Each band's thread its own.
			uint32_t* rowBuffer(size_t pixels)
			{
				thread_local std::vector<uint32_t> buffer;
				if (buffer.size() < pixels)
					buffer.resize(pixels);
				return buffer.data();
			}
		}

		void widenRow(const uint32_t* from, uint32_t* to, int count)
		{
			int i = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
			// 8 pixels, the even and the odd apart, to 12: even, mean, odd, ...
			for (; i + 8 <= count; i += 8)
			{
				const uint32x4x2_t pair = vld2q_u32(from + i);
				uint32x4x3_t out;
				out.val[0] = pair.val[0];
				out.val[1] = vreinterpretq_u32_u8(vrhaddq_u8(vreinterpretq_u8_u32(pair.val[0]), vreinterpretq_u8_u32(pair.val[1])));
				out.val[2] = pair.val[1];
				vst3q_u32(to + i / 2 * 3, out);
			}
#endif
			for (; i + 2 <= count; i += 2)
			{
				uint32_t* out = to + i / 2 * 3;
				out[0] = from[i];
				out[1] = mean(from[i], from[i + 1]);
				out[2] = from[i + 1];
			}
		}

		void widenMeanRow(const uint32_t* a, const uint32_t* b, uint32_t* to, int count)
		{
			int i = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
			for (; i + 8 <= count; i += 8)
			{
				const uint32x4x2_t pa = vld2q_u32(a + i), pb = vld2q_u32(b + i);
				const uint8x16_t even = vrhaddq_u8(vreinterpretq_u8_u32(pa.val[0]), vreinterpretq_u8_u32(pb.val[0]));
				const uint8x16_t odd = vrhaddq_u8(vreinterpretq_u8_u32(pa.val[1]), vreinterpretq_u8_u32(pb.val[1]));
				uint32x4x3_t out;
				out.val[0] = vreinterpretq_u32_u8(even);
				out.val[1] = vreinterpretq_u32_u8(vrhaddq_u8(even, odd));
				out.val[2] = vreinterpretq_u32_u8(odd);
				vst3q_u32(to + i / 2 * 3, out);
			}
#endif
			for (; i + 2 <= count; i += 2)
			{
				const uint32_t even = mean(a[i], b[i]), odd = mean(a[i + 1], b[i + 1]);
				uint32_t* out = to + i / 2 * 3;
				out[0] = even;
				out[1] = mean(even, odd);
				out[2] = odd;
			}
		}

		void Scale::setup(int drawWidth, int drawHeight, int screenWidth, int screenHeight)
		{
			mDrawWidth = drawWidth;
			mDrawHeight = drawHeight;
			mScreenWidth = screenWidth;
			mScreenHeight = screenHeight;
			mColumn.clear(); mRow.clear(); mFirstColumn.clear(); mFirstRow.clear();
			mLeft = mTop = 0;
			mWidth = screenWidth;
			mHeight = screenHeight;

			if (drawWidth == screenWidth && drawHeight == screenHeight)
			{
				mKind = Kind::Copy;
				return;
			}
			// (both even, then: 3 * width = 2 * the screen's)
			if ((int64_t)drawWidth * 3 == (int64_t)screenWidth * 2 && (int64_t)drawHeight * 3 == (int64_t)screenHeight * 2)
			{
				mKind = Kind::ThreeHalves;
				return;
			}

			mKind = Kind::Nearest;
			// as wide as the screen, or as high, the frame's shape kept
			if ((int64_t)drawHeight * screenWidth <= (int64_t)screenHeight * drawWidth)
			{
				mWidth = screenWidth;
				mHeight = std::max(1, (int)((int64_t)drawHeight * screenWidth / drawWidth));
			}
			else
			{
				mHeight = screenHeight;
				mWidth = std::max(1, (int)((int64_t)drawWidth * screenHeight / drawHeight));
			}
			mLeft = (screenWidth - mWidth) / 2;
			mTop = (screenHeight - mHeight) / 2;

			auto map = [](int from, int to, std::vector<int>& source, std::vector<int>& first)
			{
				// each of the picture's at the frame's under its middle
				source.resize(to);
				for (int i = 0; i < to; i++)
					source[i] = std::min(from - 1, (int)(((int64_t)2 * i + 1) * from / ((int64_t)2 * to)));
				first.assign(from + 1, to);
				for (int i = to - 1; i >= 0; i--)
					first[source[i]] = i;
				// the frame's that no pixel shows: where the next one starts
				for (int s = from - 1; s >= 0; s--)
					if (first[s] > first[s + 1])
						first[s] = first[s + 1];
			};
			map(drawWidth, mWidth, mColumn, mFirstColumn);
			map(drawHeight, mHeight, mRow, mFirstRow);
		}

		std::string Scale::describe() const
		{
			const std::string drawn = std::to_string(mDrawWidth) + "x" + std::to_string(mDrawHeight);
			switch (mKind)
			{
				case Kind::Copy: return "drawn at " + drawn;
				case Kind::ThreeHalves: return "drawn at " + drawn + ", enlarged 1.5 times (smoothed) to fill it";
				default: return "drawn at " + drawn + ", shown at " + std::to_string(mWidth) + "x" + std::to_string(mHeight) +
					" from " + std::to_string(mLeft) + "," + std::to_string(mTop) + " (nearest pixels)";
			}
		}

		int64_t Scale::present(const uint32_t* frame, int x0, int y0, int x1, int y1, uint8_t* screen, size_t pitch, CopyRow copy) const
		{
			x0 = std::max(0, x0); y0 = std::max(0, y0);
			x1 = std::min(mDrawWidth, x1); y1 = std::min(mDrawHeight, y1);
			if (x0 >= x1 || y0 >= y1)
				return 0;
			const int width = mDrawWidth;

			if (mKind == Kind::Copy)
			{
				const size_t bytes = (size_t)(x1 - x0) * 4;
				for (int y = y0; y < y1; y++)
					copy(screen + (size_t)y * pitch + (size_t)x0 * 4, (const uint8_t*)(frame + (size_t)y * width + x0), bytes);
				return (int64_t)(x1 - x0) * (y1 - y0);
			}

			if (mKind == Kind::ThreeHalves)
			{
				// whole pairs (the frame's sizes are even): those beside the part are as they were, the same again
				x0 &= ~1; y0 &= ~1;
				x1 = (x1 + 1) & ~1; y1 = (y1 + 1) & ~1;
				// straight to the screen, a row at a time (stores in a row's order, as its memory takes them
				// best): on the DE10-Nano a whole 1080p frame 10.4 ms, 14.4 through a row built in the cache
				// and copied
				const int count = x1 - x0, out = count / 2 * 3;
				uint8_t* to = screen + (size_t)(y0 / 2 * 3) * pitch + (size_t)(x0 / 2 * 3) * 4;
				for (int y = y0; y < y1; y += 2, to += 3 * pitch)
				{
					const uint32_t* a = frame + (size_t)y * width + x0;
					const uint32_t* b = a + width;
					widenRow(a, (uint32_t*)to, count);
					widenMeanRow(a, b, (uint32_t*)(to + pitch), count);
					widenRow(b, (uint32_t*)(to + 2 * pitch), count);
				}
				return (int64_t)out * ((y1 - y0) / 2 * 3);
			}

			// nearest: the picture's columns and rows that show these
			const int c0 = mFirstColumn[x0], c1 = mFirstColumn[x1];
			const int r0 = mFirstRow[y0], r1 = mFirstRow[y1];
			if (c0 >= c1 || r0 >= r1)
				return 0;
			const int count = c1 - c0;
			uint32_t* row = rowBuffer(count);
			const int* columns = mColumn.data() + c0;
			for (int r = r0; r < r1; r++)
			{
				const uint32_t* from = frame + (size_t)mRow[r] * width;
				for (int i = 0; i < count; i++)
					row[i] = from[columns[i]];
				copy(screen + (size_t)(mTop + r) * pitch + (size_t)(mLeft + c0) * 4, (const uint8_t*)row, (size_t)count * 4);
			}
			return (int64_t)count * (r1 - r0);
		}

		void Scale::clearAround(uint8_t* screen, size_t pitch, CopyRow copy) const
		{
			if (mKind != Kind::Nearest)
				return;
			uint32_t* row = rowBuffer(mScreenWidth);
			memset(row, 0, (size_t)mScreenWidth * 4);
			for (int y = 0; y < mScreenHeight; y++)
			{
				uint8_t* line = screen + (size_t)y * pitch;
				if (y < mTop || y >= mTop + mHeight)
					copy(line, (const uint8_t*)row, (size_t)mScreenWidth * 4);
				else
				{
					copy(line, (const uint8_t*)row, (size_t)mLeft * 4);
					const int right = mLeft + mWidth;
					copy(line + (size_t)right * 4, (const uint8_t*)row, (size_t)(mScreenWidth - right) * 4);
				}
			}
		}
	}
}
