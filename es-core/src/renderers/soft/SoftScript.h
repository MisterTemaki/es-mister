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
//   quit            ES closes

#include <cstdint>
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
			void frame(const uint32_t* pixels, int width, int height, bool drewNew);

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
		};
	}
}

#endif
