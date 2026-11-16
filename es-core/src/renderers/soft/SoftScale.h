#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_SCALE_H
#define ES_CORE_RENDERERS_SOFT_SCALE_H

// ES's frames drawn at one size and shown on a framebuffer of another, enlarged (or made smaller) as they
// are copied to it. On the MiSTer, the FPGA's scaler enlarges the Linux framebuffer only by whole times
// (Main_MiSTer's fb_cmd1), so 1280x720 on a 1080p screen stayed in the middle, at two thirds of it. Drawn
// at 1280x720 as before and copied to a framebuffer of the screen's own size, 1.5 times bigger, it fills the
// screen: the drawing costs the same, and only the copy grows.
//
//   copy          the same size
//   three halves  1.5 times both ways (1280x720 on 1920x1080): each 2x2 pixels become 3x3, the middle
//                 column and row the mean of their two neighbours. Every pixel of the frame covers the same
//                 area of the screen, and nothing reaches across a pair: a band of the frame needs both rows
//                 of a pair (rowStep 2), and each part drawn again is enlarged alone.
//   nearest       any other sizes: the picture as big as fits with its shape kept, in the middle (black
//                 around it), each pixel of the screen the frame's nearest

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer
{
	namespace Soft
	{
		class Scale
		{
		public:
			enum class Kind { Copy, ThreeHalves, Nearest };
			// a row's bytes to the screen (the framebuffer's memory takes some stores better than others)
			typedef void (*CopyRow)(uint8_t* to, const uint8_t* from, size_t bytes);

			// frames of drawWidth x drawHeight, a screen of screenWidth x screenHeight
			void setup(int drawWidth, int drawHeight, int screenWidth, int screenHeight);
			Kind kind() const { return mKind; }
			// the rows a band of the frame starts at a multiple of
			int rowStep() const { return mKind == Kind::ThreeHalves ? 2 : 1; }
			// where the picture is on the screen
			int left() const { return mLeft; }
			int top() const { return mTop; }
			int width() const { return mWidth; }
			int height() const { return mHeight; }
			int drawWidth() const { return mDrawWidth; }
			int drawHeight() const { return mDrawHeight; }
			std::string describe() const;

			// Columns x0 to x1 of rows y0 to y1 of a frame (0xAARRGGBB) to the screen (32 bits a pixel, rows of
			// pitch bytes), and what of the screen they show; the screen's pixels written. Parts in different
			// bands (rows apart at rowStep) may be presented at once by threads of their own.
			int64_t present(const uint32_t* frame, int x0, int y0, int x1, int y1, uint8_t* screen, size_t pitch, CopyRow copy) const;
			// the screen around the picture black (nearest, when it does not fill it)
			void clearAround(uint8_t* screen, size_t pitch, CopyRow copy) const;

		private:
			Kind mKind = Kind::Copy;
			int mDrawWidth = 0, mDrawHeight = 0, mScreenWidth = 0, mScreenHeight = 0;
			int mLeft = 0, mTop = 0, mWidth = 0, mHeight = 0;
			// nearest: the frame's column (row) each of the picture's shows, and the picture's first column
			// (row) showing each of the frame's, with its end after them
			std::vector<int> mColumn, mRow, mFirstColumn, mFirstRow;
		};

		// a row of pairs of pixels as three halves: count (even) pixels to count * 3 / 2
		void widenRow(const uint32_t* from, uint32_t* to, int count);
		// the same, of the mean of two rows (the middle row of three)
		void widenMeanRow(const uint32_t* a, const uint32_t* b, uint32_t* to, int count);
	}
}

#endif
