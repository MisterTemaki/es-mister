#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_OUTPUT_H
#define ES_CORE_RENDERERS_SOFT_OUTPUT_H

// Where the software renderer's frames go. On the MiSTer, the Linux framebuffer (/dev/fb0) that the
// FPGA's scaler shows; elsewhere, nowhere (frames are only kept, for the screenshots of a test script).
//
//   ES_SOFT_OUTPUT  fbdev[:path] (the default when /dev/fb0 exists) or null
//   ES_SOFT_SIZE    WIDTHxHEIGHT for the null output (default 640x360)
//   ES_SOFT_TTY     the console put in graphics mode while drawing (default /dev/tty0), "none" to leave it

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Renderer
{
	namespace Soft
	{
		class Output
		{
		public:
			virtual ~Output() { }
			// Opens the output; its size in width and height. False, with the reason, when it cannot.
			virtual bool open(int& width, int& height, std::string& error) = 0;
			virtual void close() { }
			// A frame of width x height pixels, 0xAARRGGBB. Only the rows that changed since the last frame need copying.
			void present(const uint32_t* pixels, int width, int height)
			{
				finishFrame(presentRows(pixels, width, height, 0, height));
			}
			// Rows y0 to y1 of a frame, the rows copied: bands of a frame may be presented at once by threads
			// of their own, then finishFrame with the rows they copied.
			virtual int presentRows(const uint32_t* pixels, int width, int height, int y0, int y1) = 0;
			virtual void finishFrame(int rowsCopied) { mRowsCopied = rowsCopied; }
			virtual std::string describe() const = 0;
			// rows copied by the last present, for the statistics
			int lastRowsCopied() const { return mRowsCopied; }

		protected:
			int mRowsCopied = 0;
		};

		std::unique_ptr<Output> createOutput();
	}
}

#endif
