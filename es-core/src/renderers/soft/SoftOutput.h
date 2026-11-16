#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_OUTPUT_H
#define ES_CORE_RENDERERS_SOFT_OUTPUT_H

// Where the software renderer's frames go. On the MiSTer, the Linux framebuffer (/dev/fb0) that the
// FPGA's scaler shows; elsewhere, nowhere (frames are only kept, for the screenshots of a test script).
//
//   ES_SOFT_OUTPUT  fbdev[:path] (the default when /dev/fb0 exists) or null
//   ES_SOFT_SIZE    WIDTHxHEIGHT for the null output (default 640x360)
//   ES_SOFT_TTY     the console put in graphics mode while drawing (default: the active one), "none" to leave it
//   ES_SOFT_MISTER_MODE=1  the MiSTer's framebuffer mode kept at ES's size (es-mister.sh sets it)
//
// The console ES draws on is in graphics mode, where the kernel switches away from it only when the
// program drawing agrees (VT_PROCESS, as X servers do): asked to (Main_MiSTer runs a script on another
// console, F9...), the frames stop going to the screen and the switch is let through at once; back on
// it, the next frame is shown whole. In graphics mode without that, the kernel ignored every switch:
// Main_MiSTer waited for its switch for ever (the MiSTer's menu frozen after F12 and a script).

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
			// A whole frame of width x height pixels, 0xAARRGGBB.
			void present(const uint32_t* pixels, int width, int height)
			{
				finishFrame(presentRect(pixels, width, height, 0, 0, width, height));
			}
			// A part of a frame (width x height pixels): columns x0 to x1 of rows y0 to y1 (x1 and y1 not in
			// it), copied as they are to the screen, which keeps the rest; the pixels copied. Parts that do not
			// overlap may be presented at once by threads of their own (a frame's bands), then finishFrame with
			// the pixels they copied.
			virtual int64_t presentRect(const uint32_t* pixels, int width, int height, int x0, int y0, int x1, int y1) = 0;
			virtual void finishFrame(int64_t pixelsCopied) { mCopied = pixelsCopied; }
			virtual std::string describe() const = 0;
			// the screen was given back (a console switch back to ES's): what it shows is not the last frame,
			// the next one goes to it whole. True once each time.
			virtual bool takeRedraw() { return false; }
			// pixels copied by the last frame, for the statistics
			int64_t lastPixelsCopied() const { return mCopied; }

		protected:
			int64_t mCopied = 0;
		};

		// The output ES_SOFT_OUTPUT names (kind: that one in its place, as "null")
		std::unique_ptr<Output> createOutput(const char* kind = nullptr);
	}
}

#endif
