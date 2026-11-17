#include "SoftScript.h"

#include "Log.h"

#include <SDL.h>
#include <stb_image_write.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef MISTER_PERF
#include "utils/Perf.h"
#endif

// the script's clock: real time, or with ES_PERF_FIXED_STEP (measurement build) ES's fixed-step time, so that
// its waits and presses fall on the same frames on every run (golden frames)
static uint32_t scriptNowMs()
{
#ifdef MISTER_PERF
	if (Perf::fixedStep())
		return Perf::virtualMs();
#endif
	return SDL_GetTicks();
}

namespace Renderer
{
	namespace Soft
	{
		bool Script::load(const std::string& path)
		{
			std::ifstream file(path);
			if (!file)
				return false;

			std::string line;
			while (std::getline(file, line))
			{
				const size_t hash = line.find('#');
				if (hash != std::string::npos)
					line = line.substr(0, hash);
				const size_t first = line.find_first_not_of(" \t\r");
				if (first == std::string::npos)
					continue;
				const size_t last = line.find_last_not_of(" \t\r");
				mLines.push_back(line.substr(first, last - first + 1));
			}

			const char* shots = getenv("ES_SOFT_SHOTS");
			mShots = shots != nullptr ? shots : ".";
			LOG(LogInfo) << "SoftRenderer: running the test script " << path << " (" << mLines.size() << " commands)";
			return true;
		}

		void Script::pressKey(int key, uint32_t forMs)
		{
			SDL_Event event = {};
			event.type = SDL_KEYDOWN;
			event.key.state = SDL_PRESSED;
			event.key.keysym.sym = key;
			event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
			SDL_PushEvent(&event);
			mReleases.push_back({ key, scriptNowMs() + forMs });
		}

		void Script::savePng(const std::string& name, const uint32_t* pixels, int width, int height)
		{
			std::vector<uint8_t> rgb((size_t)width * height * 3);
			for (size_t i = 0; i < (size_t)width * height; i++)
			{
				const uint32_t c = pixels[i];
				rgb[i * 3 + 0] = (c >> 16) & 0xFF;
				rgb[i * 3 + 1] = (c >> 8) & 0xFF;
				rgb[i * 3 + 2] = c & 0xFF;
			}
			const std::string path = mShots + "/" + name + ".png";
			if (stbi_write_png(path.c_str(), width, height, 3, rgb.data(), width * 3))
				LOG(LogInfo) << "SoftRenderer: screenshot " << path << " at " << scriptNowMs() << " ms";
			else
				LOG(LogError) << "SoftRenderer: cannot write " << path;
		}

		void Script::frame(const std::function<const uint32_t*()>& pixels, int width, int height, bool drewNew)
		{
			const uint32_t now = scriptNowMs();

			mFrames++;
			if (mHashes != nullptr)
			{
				// FNV-1a of the pixels, the clock's corner (bottom right, 100x80 at 1280x720, scaled) left out
				const uint32_t* p = pixels();
				const int cx = width - width * 100 / 1280, cy = height - height * 80 / 720;
				uint64_t h = 1469598103934665603ULL;
				for (int y = 0; y < height; y++)
					for (int x = 0; x < (y >= cy ? cx : width); x++)
						h = (h ^ p[(size_t)y * width + x]) * 1099511628211ULL;
				fprintf(mHashes, "%llu %016llx\n", (unsigned long long)mFrames, (unsigned long long)h);
			}

			// keys let go of
			for (auto it = mReleases.begin(); it != mReleases.end(); )
			{
				if ((int32_t)(now - it->at) < 0)
				{
					++it;
					continue;
				}
				SDL_Event event = {};
				event.type = SDL_KEYUP;
				event.key.state = SDL_RELEASED;
				event.key.keysym.sym = it->key;
				event.key.keysym.scancode = SDL_GetScancodeFromKey(it->key);
				SDL_PushEvent(&event);
				it = mReleases.erase(it);
			}

			if (drewNew)
				mLastNew = now;

			if (mSettling)
			{
				if (now - mLastNew < 1000 && (int32_t)(now - mSettleUntil) < 0)
					return;
				mSettling = false;
			}

			if ((int32_t)(now - mWaitUntil) < 0)
				return;

			while (mNext < mLines.size())
			{
				std::istringstream line(mLines[mNext++]);
				std::string command;
				line >> command;

				if (command == "wait")
				{
					int ms = 0;
					line >> ms;
					mWaitUntil = now + ms;
					return;
				}
				else if (command == "settle")
				{
					int most = 15000;
					line >> most;
					mSettling = true;
					mSettleUntil = now + most;
					mLastNew = now;
					return;
				}
				else if (command == "key" || command == "hold")
				{
					std::string name;
					int ms = 60;
					line >> name;
					if (command == "hold")
						line >> ms;
					const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
					if (key == SDLK_UNKNOWN)
						LOG(LogError) << "SoftRenderer: unknown key " << name;
					else
						pressKey(key, ms);
					mWaitUntil = now + ms + 150;
					return;
				}
				else if (command == "shot")
				{
					std::string name;
					line >> name;
					savePng(name, pixels(), width, height);
				}
				else if (command == "dump")
				{
					std::string name;
					line >> name;
					mDump = (mShots.empty() ? std::string(".") : mShots) + "/" + name + ".txt";
				}
				else if (command == "capture")
				{
					std::string name;
					line >> name;
					mCapture = (mShots.empty() ? std::string(".") : mShots) + "/" + name + ".escf";
				}
				else if (command == "stats")
					mStats = true;
				else if (command == "hashes")
				{
					std::string arg;
					line >> arg;
					if (mHashes != nullptr)
						fclose(mHashes);
					mHashes = arg == "off" ? nullptr : fopen(((mShots.empty() ? std::string(".") : mShots) + "/hashes.txt").c_str(), "a");
				}
				else if (command == "log")
				{
					std::string text;
					std::getline(line, text);
					LOG(LogInfo) << "SoftRenderer script:" << text;
#ifdef MISTER_PERF
					Perf::mark(text.size() > 0 && text[0] == ' ' ? text.substr(1) : text);
#endif
				}
				else if (command == "quit")
				{
					SDL_Event event = {};
					event.type = SDL_QUIT;
					SDL_PushEvent(&event);
					return;
				}
				else
					LOG(LogError) << "SoftRenderer: unknown script command " << command;
			}
		}
	}
}
