#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_SCRIPT_H
#define ES_CORE_RENDERERS_SOFT_SCRIPT_H

// A test script for the software renderer, to see ES's screens on a computer with no display and no
// controller: ES_SOFT_SCRIPT names a file of one command a line ('#' starts a comment), run as frames
// are shown; ES_SOFT_SHOTS names the folder its screenshots go in (default: the current one). Times
// are in milliseconds of real time, as ES's animations are.
//
//   wait MS         MS milliseconds
//   settle [MS]     until nothing new has been drawn for a second (at most MS, default 15000)
//   key NAME        the key down for 60 ms, then up, then 150 ms (SDL's names: Up, Down, Left, Right,
//                   Return, Escape, Backspace, Space, F1, a...)
//   hold NAME MS    the key down for MS milliseconds
//   shot NAME       the frame as NAME.png
//   log TEXT        a line in ES's log
//   stats           the renderer's numbers since the last ones written now (ES_SOFT_STATS_FILE), and begun again
//   capture NAME    the next frame drawn, its commands and textures, as NAME.escf (es-mister-replay draws it again)
//   quit            ES closes

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace Renderer
{
	namespace Soft
	{
		class Script
		{
		public:
			bool load(const std::string& path);
			bool active() const { return mNext < mLines.size() || !mReleases.empty(); }
			// After each frame shown: drewNew tells whether it drew anything new
			// pixels: the frame's, asked for only when a screenshot is taken
			void frame(const std::function<const uint32_t*()>& pixels, int width, int height, bool drewNew);
			// "dump NAME": the file the next drawn frame's commands are written to, once ("" when none)
			std::string takeDump() { std::string path; path.swap(mDump); return path; }
			// "capture NAME": the file the next drawn frame is captured to, once ("" when none)
			std::string takeCapture() { std::string path; path.swap(mCapture); return path; }
			// "stats": whether the renderer's numbers are to be written now (once)
			bool takeStats() { const bool wanted = mStats; mStats = false; return wanted; }

		private:
			struct Release { int key; uint32_t at; };

			void pressKey(int key, uint32_t forMs);
			void savePng(const std::string& name, const uint32_t* pixels, int width, int height);

			std::vector<std::string> mLines;
			size_t mNext = 0;
			uint32_t mWaitUntil = 0;
			uint32_t mSettleUntil = 0, mLastNew = 0;
			bool mSettling = false;
			std::vector<Release> mReleases;
			std::string mShots;
			std::string mDump;
			std::string mCapture;
			bool mStats = false;
			// "hashes": a hash of every frame from then on (the theme's clock left out), one line each, to compare
			// the frames in motion of two builds; "hashes off" stops
			FILE* mHashes = nullptr;
			uint64_t mFrames = 0;
		};
	}
}

#endif
