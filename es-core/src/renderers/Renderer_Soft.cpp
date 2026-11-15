#include "Renderer_Soft.h"
#include "utils/StartupTrace.h"

#ifdef RENDERER_SOFTWARE

#include "renderers/soft/SoftCapture.h"
#include "renderers/soft/SoftOutput.h"
#include "renderers/soft/SoftRaster.h"
#include "renderers/soft/SoftScript.h"
#include "math/Transform4x4f.h"
#include "Log.h"
#include "Settings.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Renderer
{
	namespace
	{
		using Soft::Output;
		using Soft::Raster;
		using Soft::RasterStats;
		using Soft::Script;
		using Soft::SVertex;
		using Soft::DrawState;
		using Soft::StencilTest;
		using Soft::Topology;
		using Soft::createOutput;
		using Clock = std::chrono::steady_clock;

		constexpr uint64_t kHashSeed = 1469598103934665603ULL;
		constexpr uint64_t kHashPrime = 1099511628211ULL;

		inline uint64_t hashWords(uint64_t hash, const void* data, size_t bytes)
		{
			const uint32_t* words = (const uint32_t*)data;
			for (size_t i = 0; i < bytes / 4; i++)
				hash = (hash ^ words[i]) * kHashPrime;
			return hash;
		}

		inline uint64_t hashValue(uint64_t hash, uint64_t value)
		{
			hash = (hash ^ (uint32_t)value) * kHashPrime;
			return (hash ^ (uint32_t)(value >> 32)) * kHashPrime;
		}

		// a part of the frame, in pixels, half-open
		struct Box
		{
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			bool empty() const { return x0 >= x1 || y0 >= y1; }
			bool meets(const Box& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
		};

		struct Command
		{
			bool stencil = false;  // the stencil shape, not a draw
			Topology topology = Topology::Strip;
			uint32_t first = 0, count = 0;
			DrawState state;
			std::shared_ptr<Soft::Texture> texture;
			uint64_t sig = 0;      // all that makes what it draws: two draws with the same draw the same pixels
			Box box;               // where it may draw (a stencil shape: where it may mark)
		};

		// a draw of the last frame drawn, to see what changed
		struct Drawn
		{
			uint64_t sig;
			Box box;
		};

		struct Stats
		{
			int frames = 0, drawn = 0;
			double frameMs = 0, frameMax = 0;
			double rasterMs = 0, rasterMax = 0;
			double copyMs = 0;
			double esMs = 0, esMax = 0;   // ES's own work before a drawn frame (update, render, recording)
			uint64_t copied = 0, vertices = 0, pixels = 0; // copied: pixels to the screen
			uint32_t quads = 0, triangles = 0;
			double bandMs[2] = { 0, 0 }; // the first two bands' drawing, to see the balance
			uint64_t kinds[Soft::PX_KINDS] = {};
			int partial = 0;             // frames drawn in part (what changed)
			uint64_t redrawn = 0;        // pixels drawn again
			int checked = 0, wrong = 0;  // ES_SOFT_DIRTY_CHECK: frames drawn in part checked against whole ones
			std::vector<float> answers;  // ms from a press of the user's to the first frame shown after it
			int overBudget = 0;          // drawn frames that took more than 1/60 s (ES, drawing and copy)
		};

		// The frame drawn in horizontal bands, one a thread: the first by the thread that swaps, the others
		// by workers. The line between the first two moves toward the one that took longer.
		struct Bands
		{
			int count = 1;
			std::vector<std::unique_ptr<Raster>> rasters;
			std::vector<double> ms;           // each band's drawing, the last frame
			std::vector<double> copyMs;       // and its copy to the output
			std::vector<int64_t> copied;      // the pixels it copied to the screen
			bool present = false;             // the bands copy their rows to the output
			int split = 0;                    // the first band's last row + 1, when there are two
			int fullSplit = 0;                // that line for a whole frame, moved toward balance after each
			bool attached = false;

			std::mutex lock;
			std::condition_variable wake, finished;
			uint64_t job = 0;                 // the frame to draw
			int pending = 0;                  // bands still drawing it
			bool quit = false;
			std::vector<std::thread> workers;

			~Bands()
			{
				{
					std::lock_guard<std::mutex> guard(lock);
					quit = true;
				}
				wake.notify_all();
				for (auto& worker : workers)
					if (worker.joinable())
						worker.join();
			}
		};

		struct SoftState
		{
			std::unique_ptr<Output> output;
			bool open = false;
			int width = 0, height = 0;

			Raster raster;
			Bands bands;

			std::mutex textureLock;
			std::map<unsigned int, std::shared_ptr<Soft::Texture>> textures;
			unsigned int nextTexture = 1;
			std::shared_ptr<Soft::Texture> bound;

			Transform4x4f projection = Transform4x4f::Identity();
			Transform4x4f modelview = Transform4x4f::Identity();
			Rect viewport;
			float m[6] = { 1, 0, 0, 0, 1, 0 }; // layout units to output pixels

			bool scissor = false;
			Rect scissorRect;
			StencilTest stencil = StencilTest::Off;

			std::vector<Command> commands;
			std::vector<SVertex> vertices;
			uint64_t hash = kHashSeed;
			uint64_t lastHash = 0;
			bool forceRedraw = true;
			uint64_t stencilSig = 0;          // the stencil shape the frame being recorded draws with

			// Only what changed drawn again (ES_SOFT_DIRTY=0: every frame whole): the raster keeps the last
			// frame drawn, and the parts of it where this frame's draws are not the last one's are drawn again
			bool dirty = true;
			bool dirtyCheck = false;          // ES_SOFT_DIRTY_CHECK=1: each part-drawn frame checked against a whole one
			int stripRows = 24;               // ES_SOFT_STRIP: rows drawn at a time through all the commands (executeBand)
			bool lastValid = false;           // the raster holds the frame of lastDrawn
			std::vector<Drawn> lastDrawn;
			std::vector<Box> rects;           // this frame's parts to draw (when drawn in part)
			bool partial = false;
			std::vector<uint8_t> tiles;
			std::vector<uint16_t> lcs;

			bool vsync = true;
			double interval = 1.0 / 60.0;
			Clock::time_point nextFrame;
			Clock::time_point frameStart;
			bool frameStarted = false;

			Script script;
			bool scriptLoaded = false;
			std::string pendingDump; // the file the next drawn frame's commands go to
			std::string pendingCapture; // the file the next drawn frame is captured to (SoftCapture.h)
			bool statsNow = false;    // the test script's "stats"

			int statsEvery = 600;
			bool bench = false; // ES_SOFT_BENCH: every frame drawn, even the same as the last
			Stats stats;
		};

		SoftState& S()
		{
			static SoftState state;
			return state;
		}

		inline uint32_t toArgb(unsigned int col)
		{
			// a Vertex's colour is R, G, B, A in memory (Renderer::convertColor)
			return (col & 0xFF00FF00) | ((col & 0xFF) << 16) | ((col >> 16) & 0xFF);
		}

		void updateTransform()
		{
			SoftState& s = S();
			const Transform4x4f combined = s.projection * s.modelview;
			const float* t = (const float*)&combined;
			const float hw = s.viewport.w * 0.5f, hh = s.viewport.h * 0.5f;
			s.m[0] = hw * t[0];
			s.m[1] = hw * t[4];
			s.m[2] = s.viewport.x + hw * (1.0f + t[12]);
			s.m[3] = -hh * t[1];
			s.m[4] = -hh * t[5];
			s.m[5] = s.viewport.y + hh * (1.0f - t[13]);
		}

		bool openOutput()
		{
			SoftState& s = S();
			if (s.open)
				return true;
			if (!s.output)
				s.output = createOutput();

			std::string error;
			int width = 0, height = 0;
			if (!s.output->open(width, height, error))
			{
				LOG(LogError) << "SoftRenderer: " << error << "; drawing in memory only";
				// (asked for by name: setenv here raced with the threads main() has started, which read the
				// environment)
				s.output = createOutput("null");
				if (!s.output->open(width, height, error))
					return false;
			}

			s.width = width;
			s.height = height;
			s.open = true;
			LOG(LogInfo) << "SoftRenderer: output " << s.output->describe();
			return true;
		}

		void record(const Vertex* vertices, unsigned int count, Topology topology, Blend::Factor src, Blend::Factor dst, const std::shared_ptr<Soft::Texture>& texture, bool stencil = false)
		{
			if (count == 0 || vertices == nullptr)
				return;

			SoftState& s = S();
			Command command;
			command.stencil = stencil;
			command.topology = topology;
			command.first = (uint32_t)s.vertices.size();
			command.count = count;
			command.state.srcBlend = (uint8_t)src;
			command.state.dstBlend = (uint8_t)dst;
			command.state.stencil = stencil ? StencilTest::Off : s.stencil;
			command.state.scissor = s.scissor;
			if (s.scissor)
			{
				command.state.sx0 = s.scissorRect.x;
				command.state.sy0 = s.scissorRect.y;
				command.state.sx1 = s.scissorRect.x + s.scissorRect.w;
				command.state.sy1 = s.scissorRect.y + s.scissorRect.h;
			}
			// a picture's saturation (a line of text sets it a glyph at a time, for the shaders: not here)
			command.state.saturation = count == 4 ? vertices[0].saturation : 1.0f;
			command.texture = stencil ? nullptr : texture;

			const float* m = s.m;
			for (unsigned int i = 0; i < count; i++)
			{
				const Vertex& v = vertices[i];
				SVertex sv;
				sv.x = m[0] * v.pos.x() + m[1] * v.pos.y() + m[2];
				sv.y = m[3] * v.pos.x() + m[4] * v.pos.y() + m[5];
				sv.u = v.tex.x();
				sv.v = v.tex.y();
				sv.color = toArgb(v.col);
				s.vertices.push_back(sv);
			}

			uint64_t h = kHashSeed;
			h = hashValue(h, ((uint64_t)stencil << 40) | ((uint64_t)topology << 32) | count);
			h = hashValue(h, ((uint64_t)command.state.srcBlend << 16) | ((uint64_t)command.state.dstBlend << 8) | (uint64_t)command.state.stencil);
			if (command.state.scissor)
			{
				h = hashValue(h, ((uint64_t)(uint32_t)command.state.sx0 << 32) | (uint32_t)command.state.sy0);
				h = hashValue(h, ((uint64_t)(uint32_t)command.state.sx1 << 32) | (uint32_t)command.state.sy1);
			}
			uint32_t saturation;
			memcpy(&saturation, &command.state.saturation, 4);
			h = hashValue(h, saturation);
			if (command.texture)
				h = hashValue(hashValue(h, command.texture->id), command.texture->generation);
			h = hashWords(h, &s.vertices[command.first], sizeof(SVertex) * count);
			// a draw tested against the stencil draws what the shape lets through: the shape is part of it
			if (stencil)
				s.stencilSig = h;
			else if (command.state.stencil != StencilTest::Off)
				h = hashValue(h, s.stencilSig);
			command.sig = h;
			s.hash = hashValue(s.hash, h);

			// where it may draw (a stencil shape: where it may mark): its vertices' box, in the scissor and the frame
			{
				const Soft::Bounds b = Soft::drawBounds(&s.vertices[command.first], (int)count, topology, command.state, s.width, s.height);
				command.box = { b.x0, b.y0, b.x1, b.y1 };
			}

			s.commands.push_back(std::move(command));
		}

		// one part of a band, cleared, then drawn by the commands that reach it, clipped to it (a pixel is the
		// same however its draw is clipped)
		void executePart(Raster& raster, const Box& part)
		{
			SoftState& s = S();
			raster.clearBox(part.x0, part.y0, part.x1, part.y1, 0);
			for (const Command& command : s.commands)
			{
				if (!command.box.meets(part))
				{
					// a stencil shape that marks nothing here still ends the last one: its marks cleared (it was
					// set in every part, every strip of 8 rows: the grids' rounded corners, one a tile)
					if (command.stencil)
						raster.resetStencil();
					continue;
				}
				DrawState state = command.state;
				if (state.scissor)
				{
					state.sx0 = std::max(state.sx0, part.x0); state.sy0 = std::max(state.sy0, part.y0);
					state.sx1 = std::min(state.sx1, part.x1); state.sy1 = std::min(state.sy1, part.y1);
				}
				else
				{
					state.scissor = true;
					state.sx0 = part.x0; state.sy0 = part.y0; state.sx1 = part.x1; state.sy1 = part.y1;
				}
				const SVertex* vertices = s.vertices.data() + command.first;
				if (command.stencil)
					raster.setStencil(vertices, (int)command.count, state);
				else
				{
					state.texture = command.texture.get();
					raster.draw(vertices, (int)command.count, command.topology, state);
				}
			}
		}

		// The frame's commands, in one band: all of it, or (drawn in part) each part. A few rows at a time
		// (ES_SOFT_STRIP, default by the width: 24 at 1080p; 0: the whole band or part at once), every command over them before the
		// next rows: the rows stay in the processor's cache between the layers (a background, another over it
		// in a fade, a shade...), and go to memory once instead of once a layer. On the MiSTer the memory is
		// what a frame waits for (the FPGA reads the screen from the same DDR3): a 1080p frame took as long
		// with two cores as with one.
		void executeBand(Raster& raster)
		{
			SoftState& s = S();
			const int top = raster.bandTop(), bottom = raster.bandBottom();
			auto inStrips = [&](const Box& part)
			{
				if (s.stripRows <= 0)
				{
					executePart(raster, part);
					return;
				}
				for (int y = part.y0; y < part.y1; y += s.stripRows)
					executePart(raster, Box{ part.x0, y, part.x1, std::min(part.y1, y + s.stripRows) });
			};

			if (!s.partial)
			{
				if (s.stripRows > 0)
				{
					inStrips(Box{ 0, top, s.raster.width(), bottom });
					return;
				}
				raster.clear(0);
				for (const Command& command : s.commands)
				{
					const SVertex* vertices = s.vertices.data() + command.first;
					if (command.stencil)
					{
						raster.setStencil(vertices, (int)command.count, command.state);
						continue;
					}
					DrawState state = command.state;
					state.texture = command.texture.get();
					raster.draw(vertices, (int)command.count, command.topology, state);
				}
				return;
			}

			for (const Box& whole : s.rects)
			{
				Box part = whole;
				part.y0 = std::max(part.y0, top);
				part.y1 = std::min(part.y1, bottom);
				if (!part.empty())
					inStrips(part);
			}
		}

		// what a band shows again: all its rows, or (drawn in part) its parts (which do not overlap); the
		// pixels copied
		int64_t presentBand(Raster& raster)
		{
			SoftState& s = S();
			const int top = raster.bandTop(), bottom = raster.bandBottom();
			const int w = s.raster.width(), h = s.raster.height();
			if (!s.partial)
				return s.output->presentRect(s.raster.pixels(), w, h, 0, top, w, bottom);

			int64_t copied = 0;
			for (const Box& r : s.rects)
			{
				const int y0 = std::max(r.y0, top), y1 = std::min(r.y1, bottom);
				if (y0 < y1)
					copied += s.output->presentRect(s.raster.pixels(), w, h, r.x0, y0, r.x1, y1);
			}
			return copied;
		}

		void timedBand(int index)
		{
			SoftState& s = S();
			Bands& b = s.bands;
			Raster& raster = *b.rasters[index];
			const Clock::time_point start = Clock::now();
			raster.stats() = RasterStats();
			executeBand(raster);
			const Clock::time_point drawn = Clock::now();
			b.ms[index] = std::chrono::duration<double, std::milli>(drawn - start).count();

			// its rows to the screen at once, while the other bands may still draw theirs
			b.copied[index] = 0;
			b.copyMs[index] = 0;
			if (b.present)
			{
				b.copied[index] = presentBand(raster);
				b.copyMs[index] = std::chrono::duration<double, std::milli>(Clock::now() - drawn).count();
			}
		}

		void bandWorker(int index)
		{
			Bands& b = S().bands;
			uint64_t done = 0;
			for (;;)
			{
				{
					std::unique_lock<std::mutex> guard(b.lock);
					b.wake.wait(guard, [&] { return b.quit || b.job != done; });
					if (b.quit)
						return;
					done = b.job;
				}
				timedBand(index);
				{
					std::lock_guard<std::mutex> guard(b.lock);
					if (--b.pending == 0)
						b.finished.notify_one();
				}
			}
		}

		// ES_SOFT_THREADS bands, by default one a processor up to two (the DE10-Nano's two)
		void startBands()
		{
			SoftState& s = S();
			Bands& b = s.bands;
			int count = std::min(2, (int)std::max(1u, std::thread::hardware_concurrency()));
			if (const char* value = getenv("ES_SOFT_THREADS"))
				count = std::max(1, std::min(8, atoi(value)));
			b.count = count;
			b.rasters.clear();
			for (int i = 0; i < count; i++)
				b.rasters.push_back(std::unique_ptr<Raster>(new Raster()));
			b.ms.assign(count, 0.0);
			b.copyMs.assign(count, 0.0);
			b.copied.assign(count, 0);
			b.split = 0;
			b.attached = false;
			for (int i = 1; i < count; i++)
				b.workers.push_back(std::thread(bandWorker, i));
			LOG(LogInfo) << "SoftRenderer: drawing in " << count << (count == 1 ? " thread" : " threads");
		}

		void stopBands()
		{
			Bands& b = S().bands;
			{
				std::lock_guard<std::mutex> guard(b.lock);
				b.quit = true;
			}
			b.wake.notify_all();
			for (auto& worker : b.workers)
				worker.join();
			b.workers.clear();
			b.quit = false;
		}

		// the bands' rows. A whole frame: equal, then the line between the first two moved a few rows a frame
		// toward balance (balanceBands). A frame drawn in part: the line where half its parts' pixels are.
		void placeBands()
		{
			SoftState& s = S();
			Bands& b = s.bands;
			const int height = s.raster.height();
			if (b.fullSplit <= 0 || b.fullSplit >= height)
				b.fullSplit = height / b.count;
			int split = b.fullSplit;
			if (s.partial && b.count == 2)
			{
				std::vector<int64_t> change(height + 1, 0);
				int64_t total = 0;
				for (const Box& r : s.rects)
				{
					change[r.y0] += r.x1 - r.x0;
					change[r.y1] -= r.x1 - r.x0;
					total += (int64_t)(r.x1 - r.x0) * (r.y1 - r.y0);
				}
				int64_t width = 0, sum = 0;
				for (int y = 0; y < height; y++)
				{
					width += change[y];
					sum += width;
					if (sum * 2 >= total)
					{
						split = y + 1;
						break;
					}
				}
				split = std::max(1, std::min(height - 1, split));
			}
			if (b.attached && split == b.split)
				return;

			// a band's stencil rows may become another's: each clears what it marked first
			for (auto& raster : b.rasters)
				raster->resetStencil();
			for (int i = 0; i < b.count; i++)
			{
				const int y0 = i == 0 ? 0 : (b.count == 2 ? split : height * i / b.count);
				const int y1 = i == b.count - 1 ? height : (i == 0 && b.count == 2 ? split : height * (i + 1) / b.count);
				b.rasters[i]->attach(s.raster, y0, y1);
			}
			b.split = split;
			b.attached = true;
		}

		// after a whole frame: its line toward the band that took less
		void balanceBands()
		{
			SoftState& s = S();
			Bands& b = s.bands;
			if (b.count != 2 || s.partial)
				return;
			const int height = s.raster.height();
			const double a = b.ms[0], c = b.ms[1];
			const int step = std::max(2, height / 90);
			int split = b.split;
			if (a > c * 1.15 && a - c > 0.5)
				split -= step;
			else if (c > a * 1.15 && c - a > 0.5)
				split += step;
			b.fullSplit = std::max(height / 8, std::min(height - height / 8, split));
		}

		// Which parts of the frame to draw again: the draws of this frame and of the last one drawn are matched
		// in order (the longest common run of their signatures, after the same first and last ones), and the
		// boxes of those not matched, the last frame's and this one's, mark tiles of 32x32 pixels; the marked
		// tiles, in rectangles, are the parts. A pixel no unmatched draw reaches is drawn by the same draws, in
		// the same order, as in the last frame: it is the same. False: draw the frame whole.
		bool findParts()
		{
			SoftState& s = S();
			s.rects.clear();
			std::vector<const Command*> now;
			now.reserve(s.commands.size());
			for (const Command& command : s.commands)
				if (!command.stencil)
					now.push_back(&command);
			const std::vector<Drawn>& old = s.lastDrawn;

			const int tile = 32;
			const int tilesX = (s.width + tile - 1) / tile, tilesY = (s.height + tile - 1) / tile;
			s.tiles.assign((size_t)tilesX * tilesY, 0);
			int marked = 0;
			auto mark = [&](const Box& box)
			{
				if (box.empty())
					return;
				const int tx1 = (box.x1 - 1) / tile, ty1 = (box.y1 - 1) / tile;
				for (int ty = box.y0 / tile; ty <= ty1; ty++)
					for (int tx = box.x0 / tile; tx <= tx1; tx++)
					{
						uint8_t& t = s.tiles[(size_t)ty * tilesX + tx];
						marked += t == 0;
						t = 1;
					}
			};

			const size_t n = old.size(), m = now.size();
			size_t first = 0, last = 0;
			while (first < n && first < m && old[first].sig == now[first]->sig)
				first++;
			while (last < n - first && last < m - first && old[n - 1 - last].sig == now[m - 1 - last]->sig)
				last++;
			const size_t a = n - first - last, b = m - first - last;
			std::vector<char> keptOld(a, 0), keptNow(b, 0);
			if (a > 0 && b > 0 && a * b <= (1u << 20) && a < 65535 && b < 65535)
			{
				// the longest common subsequence, table from the end
				std::vector<uint16_t>& t = s.lcs;
				t.assign((a + 1) * (b + 1), 0);
				for (size_t i = a; i-- > 0;)
					for (size_t j = b; j-- > 0;)
						t[i * (b + 1) + j] = old[first + i].sig == now[first + j]->sig ? t[(i + 1) * (b + 1) + j + 1] + 1 :
							std::max(t[(i + 1) * (b + 1) + j], t[i * (b + 1) + j + 1]);
				for (size_t i = 0, j = 0; i < a && j < b;)
				{
					if (old[first + i].sig == now[first + j]->sig)
					{
						keptOld[i++] = 1;
						keptNow[j++] = 1;
					}
					else if (t[(i + 1) * (b + 1) + j] >= t[i * (b + 1) + j + 1])
						i++;
					else
						j++;
				}
			}
			for (size_t i = 0; i < a; i++)
				if (!keptOld[i])
					mark(old[first + i].box);
			for (size_t j = 0; j < b; j++)
				if (!keptNow[j])
					mark(now[first + j]->box);

			// most of it changed: drawn whole, with less to do a pixel
			if (marked * 10 > tilesX * tilesY * 7)
				return false;

			// runs of marked tiles in each row of tiles, joined to the same run of the row above
			std::vector<Box> open;
			for (int ty = 0; ty <= tilesY; ty++)
			{
				std::vector<Box> next;
				for (int tx = 0; ty < tilesY && tx < tilesX;)
				{
					if (!s.tiles[(size_t)ty * tilesX + tx])
					{
						tx++;
						continue;
					}
					int end = tx;
					while (end < tilesX && s.tiles[(size_t)ty * tilesX + end])
						end++;
					Box run = { tx * tile, ty * tile, std::min(s.width, end * tile), std::min(s.height, (ty + 1) * tile) };
					for (auto it = open.begin(); it != open.end(); ++it)
						if (it->x0 == run.x0 && it->x1 == run.x1)
						{
							run.y0 = it->y0;
							open.erase(it);
							break;
						}
					next.push_back(run);
					tx = end;
				}
				for (const Box& done : open)
					s.rects.push_back(done);
				open.swap(next);
			}
			return true;
		}

		// ES_SOFT_DIRTY_CHECK: the frame drawn in part against the same drawn whole, on this thread
		void checkParts()
		{
			SoftState& s = S();
			Raster whole;
			whole.resize(s.raster.width(), s.raster.height());
			whole.clear(0);
			for (const Command& command : s.commands)
			{
				const SVertex* vertices = s.vertices.data() + command.first;
				if (command.stencil)
				{
					whole.setStencil(vertices, (int)command.count, command.state);
					continue;
				}
				DrawState state = command.state;
				state.texture = command.texture.get();
				whole.draw(vertices, (int)command.count, command.topology, state);
			}
			const int w = s.raster.width(), h = s.raster.height();
			const uint32_t* drawn = s.raster.pixels();
			const uint32_t* right = whole.pixels();
			int count = 0;
			Box at = { w, h, 0, 0 };
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
					if (drawn[(size_t)y * w + x] != right[(size_t)y * w + x])
					{
						count++;
						at.x0 = std::min(at.x0, x); at.x1 = std::max(at.x1, x + 1);
						at.y0 = std::min(at.y0, y); at.y1 = std::max(at.y1, y + 1);
					}
			s.stats.checked++;
			if (count == 0)
				return;
			s.stats.wrong++;
			LOG(LogError) << "SoftRenderer: drawn in parts or strips, " << count << " pixels differ from the frame drawn whole, in "
				<< at.x0 << "," << at.y0 << " " << (at.x1 - at.x0) << "x" << (at.y1 - at.y0) << " (" << s.rects.size() << " parts)";
			// the next frames checked on their own
			memcpy(s.raster.pixels(), right, (size_t)w * h * 4);
		}

		// ES_SOFT_DUMP=FILE: the commands of the drawn frame ES_SOFT_DUMP_FRAME (default 100), each with what
		// it drew and its time, drawn again alone on one thread, for tuning
		// (or the test script's "dump NAME")
		void dumpCommands(const std::string& requested, const std::string& header = "")
		{
			static int drawnFrames = 0;
			std::string path = requested;
			if (path.empty())
			{
				const char* env = getenv("ES_SOFT_DUMP");
				const char* at = getenv("ES_SOFT_DUMP_FRAME");
				if (env == nullptr || *env == 0 || ++drawnFrames != (at ? atoi(at) : 100))
					return;
				path = env;
			}

			SoftState& s = S();
			FILE* out = fopen(path.c_str(), "w");
			if (out == nullptr)
				return;
			if (!header.empty())
				fprintf(out, "%s\n", header.c_str());
			Raster raster;
			raster.resize(s.raster.width(), s.raster.height());
			raster.clear(0);
			const char* kindNames[Soft::PX_KINDS] = { "fill", "copy", "colour", "1:1", "nearest", "bilinear", "triangle" };
			double totalMs = 0;
			int index = 0;
			for (const Command& command : s.commands)
			{
				const SVertex* v = s.vertices.data() + command.first;
				float x0 = v[0].x, y0 = v[0].y, x1 = v[0].x, y1 = v[0].y;
				for (uint32_t i = 1; i < command.count; i++)
				{
					x0 = std::min(x0, v[i].x); x1 = std::max(x1, v[i].x);
					y0 = std::min(y0, v[i].y); y1 = std::max(y1, v[i].y);
				}
				raster.stats() = RasterStats();
				const Clock::time_point start = Clock::now();
				if (command.stencil)
					raster.setStencil(v, (int)command.count, command.state);
				else
				{
					DrawState state = command.state;
					state.texture = command.texture.get();
					raster.draw(v, (int)command.count, command.topology, state);
				}
				const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
				totalMs += ms;
				std::string kinds;
				for (int k = 0; k < Soft::PX_KINDS; k++)
					if (raster.stats().kinds[k])
						kinds += std::string(kinds.empty() ? "" : " ") + kindNames[k] + " " + std::to_string(raster.stats().kinds[k] / 1000) + "k";
				const Soft::Texture* t = command.texture.get();
				fprintf(out, "%4d %7.3f ms %s %3u vertices, box %.0f,%.0f %.0fx%.0f, texture %s, blend %d/%d%s%s, colour %08x: %s\n",
					index++, ms, command.stencil ? "stencil" : (command.topology == Topology::Strip ? "strip" : (command.topology == Topology::Fan ? "fan" : "lines")),
					command.count, x0, y0, x1 - x0, y1 - y0,
					t ? (std::to_string(t->width) + "x" + std::to_string(t->height) + (t->opaque ? " opaque" : "") + (t->linear ? "" : " nearest")).c_str() : "-",
					command.state.srcBlend, command.state.dstBlend, command.state.scissor ? " scissor" : "",
					command.state.stencil != StencilTest::Off ? " stencil-tested" : "", v[0].color, kinds.empty() ? "nothing" : kinds.c_str());
			}
			fprintf(out, "total %.3f ms on one thread, %d commands\n", totalMs, index);
			fclose(out);
		}

		// The frame's commands, vertices and textures in a file (SoftCapture.h), for es-mister-replay
		void captureFrame(const std::string& path)
		{
			SoftState& s = S();
			Soft::Capture capture;
			capture.width = s.raster.width();
			capture.height = s.raster.height();
			std::map<const Soft::Texture*, int> index;
			for (const Command& command : s.commands)
			{
				Soft::CapturedCommand c;
				c.stencil = command.stencil;
				c.topology = command.topology;
				c.state = command.state;
				c.state.texture = nullptr;
				if (command.texture)
				{
					auto found = index.find(command.texture.get());
					if (found == index.end())
					{
						found = index.emplace(command.texture.get(), (int)capture.textures.size()).first;
						capture.textures.push_back(command.texture);
					}
					c.texture = found->second;
				}
				c.vertices.assign(s.vertices.begin() + command.first, s.vertices.begin() + command.first + command.count);
				capture.commands.push_back(std::move(c));
			}
			if (!Soft::saveCapture(path, capture))
				LOG(LogError) << "SoftRenderer: cannot write the frame capture " << path;
		}

		void execute()
		{
			SoftState& s = S();
			Bands& b = s.bands;
			if (b.rasters.empty())
				startBands();
			placeBands();

			b.present = s.output != nullptr;
			if (b.count > 1)
			{
				std::lock_guard<std::mutex> guard(b.lock);
				b.pending = b.count - 1;
				b.job++;
			}
			b.wake.notify_all();
			timedBand(0);
			if (b.count > 1)
			{
				std::unique_lock<std::mutex> guard(b.lock);
				b.finished.wait(guard, [&] { return b.pending == 0; });
			}

			if (b.present)
			{
				int64_t copied = 0;
				for (int64_t pixels : b.copied)
					copied += pixels;
				s.output->finishFrame(copied);
			}

			balanceBands();

			RasterStats& total = s.raster.stats();
			total = RasterStats();
			for (auto& raster : b.rasters)
			{
				total.pixels += raster->stats().pixels;
				total.quads += raster->stats().quads;
				total.triangles += raster->stats().triangles;
				for (int k = 0; k < Soft::PX_KINDS; k++)
					total.kinds[k] += raster->stats().kinds[k];
			}
		}

		double msSince(Clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		void logStats()
		{
			SoftState& s = S();
			Stats& st = s.stats;
			if (st.frames == 0)
				return;
			const int drawn = std::max(1, st.drawn);
			char bands[96] = "";
			if (s.bands.count > 1)
				snprintf(bands, sizeof(bands), ", %d threads (first two %.1f / %.1f ms, line at row %d)",
					s.bands.count, st.bandMs[0] / drawn, st.bandMs[1] / drawn, s.bands.split);
			auto kpx = [&](int kind) { return (double)st.kinds[kind] / drawn / 1000.0; };
			char parts[160] = "";
			if (s.dirty)
				snprintf(parts, sizeof(parts), ", %d drawn in part, %.0f%% of the screen drawn again a drawn frame%s",
					st.partial, 100.0 * st.redrawn / drawn / std::max(1, s.width * s.height),
					st.checked ? (", " + std::to_string(st.wrong) + " of " + std::to_string(st.checked) + " checked wrong").c_str() : "");
			// presses answered: the median, the 99th percentile and the longest
			char answers[200] = "";
			if (!st.answers.empty())
			{
				std::vector<float> sorted = st.answers;
				std::sort(sorted.begin(), sorted.end());
				const size_t n = sorted.size();
				snprintf(answers, sizeof(answers), "; %zu presses answered in %.0f ms (median), %.0f ms (99%%), %.0f ms (longest)",
					n, sorted[n / 2], sorted[std::min(n - 1, (size_t)std::ceil(n * 0.99) - 1)], sorted[n - 1]);
			}
			char budget[96];
			snprintf(budget, sizeof(budget), "; %d of %d drawn frames over 16.7 ms", st.overBudget, st.drawn);
			char line[1400];
			snprintf(line, sizeof(line),
				"SoftRenderer: %d frames, %d drawn; frame %.1f ms (max %.1f), ES before a drawn frame %.1f ms (max %.1f), drawing %.1f ms (max %.1f)%s, copy %.2f ms, %.0f k pixels copied, "
				"%.0f vertices, %.0f k pixels, %u quads, %u triangles a drawn frame%s; k pixels: fill %.0f, copy %.0f, colour %.0f, "
				"picture 1:1 %.0f, nearest %.0f, bilinear %.0f, triangles %.0f%s%s",
				st.frames, st.drawn, st.frameMs / st.frames, st.frameMax, st.esMs / drawn, st.esMax, st.rasterMs / drawn, st.rasterMax, bands, st.copyMs / drawn,
				(double)st.copied / drawn / 1000.0, (double)st.vertices / drawn, (double)st.pixels / drawn / 1000.0, st.quads / drawn, st.triangles / drawn, parts,
				kpx(Soft::PX_FILL), kpx(Soft::PX_COPY), kpx(Soft::PX_COLOR), kpx(Soft::PX_TEX_1TO1), kpx(Soft::PX_TEX_NEAREST), kpx(Soft::PX_TEX_LINEAR), kpx(Soft::PX_TRIANGLE),
				budget, answers);
			LOG(LogInfo) << line;

			// also where it can be read without ES's debug log (ES_SOFT_STATS_FILE), with the time
			const char* file = getenv("ES_SOFT_STATS_FILE");
			if (file != nullptr && *file)
			{
				FILE* out = fopen(file, "a");
				if (out != nullptr)
				{
					time_t now = time(nullptr);
					char stamp[32];
					strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&now));
					fprintf(out, "%s %s\n", stamp, line);
					fclose(out);
				}
			}
			st = Stats();
		}
	}

	bool getSoftwareOutputSize(int& width, int& height)
	{
		if (!openOutput())
			return false;
		width = S().width;
		height = S().height;
		return true;
	}

	std::string SoftRenderer::getDriverName()
	{
		return "SOFTWARE";
	}

	std::vector<std::pair<std::string, std::string>> SoftRenderer::getDriverInformation()
	{
		std::vector<std::pair<std::string, std::string>> info;
		info.push_back(std::pair<std::string, std::string>("GRAPHICS API", "SOFTWARE"));
		if (S().output)
			info.push_back(std::pair<std::string, std::string>("OUTPUT", S().output->describe()));
		return info;
	}

	unsigned int SoftRenderer::getWindowFlags()
	{
		return 0;
	}

	void SoftRenderer::setupWindow()
	{
	}

	void SoftRenderer::createContext()
	{
		SoftState& s = S();
		if (!openOutput())
		{
			LOG(LogError) << "SoftRenderer: no output";
			return;
		}

		// the bands draw into the raster's memory: they start again with it
		stopBands();
		s.bands.rasters.clear();
		s.raster.resize(s.width, s.height);
		s.forceRedraw = true;
		s.lastValid = false;
		s.frameStarted = false;
		const char* dirty = getenv("ES_SOFT_DIRTY");
		s.dirty = dirty == nullptr || strcmp(dirty, "0") != 0;
		const char* check = getenv("ES_SOFT_DIRTY_CHECK");
		s.dirtyCheck = check != nullptr && strcmp(check, "0") != 0;
		// rows a strip: ES_SOFT_STRIP, or by itself ("auto", the default) as many as make 180 KB of a row's
		// pixels, two bands' strips staying in the Cortex-A9's 512 KB L2 cache: 24 rows at 1920 pixels a row.
		// Measured on the DE10-Nano with real frames (es-mister-replay, 11 frames of the carousel and the
		// grids at 1080p): 24 rows 1766 ms, 8 rows 1904-1984, 16 rows 1912, 32 rows 1838, a band at once 1818.
		const char* strip = getenv("ES_SOFT_STRIP");
		if (strip != nullptr && *strip != 0 && strcmp(strip, "auto") != 0)
			s.stripRows = std::max(0, atoi(strip));
		else
			s.stripRows = std::max(8, std::min(64, 46080 / std::max(1, s.width)));

		const char* stats = getenv("ES_SOFT_STATS");
		if (stats != nullptr)
			s.statsEvery = atoi(stats);
		s.bench = getenv("ES_SOFT_BENCH") != nullptr;

		if (!s.scriptLoaded)
		{
			s.scriptLoaded = true;
			const char* script = getenv("ES_SOFT_SCRIPT");
			if (script != nullptr && !s.script.load(script))
				LOG(LogError) << "SoftRenderer: cannot read the test script " << script;
		}

		LOG(LogInfo) << "SoftRenderer: drawing " << s.width << "x" << s.height << " in software";
	}

	void SoftRenderer::destroyContext()
	{
		SoftState& s = S();
		logStats();
		stopBands();
		s.bands.rasters.clear();
		if (s.output)
			s.output->close();
		s.open = false;
		s.commands.clear();
		s.vertices.clear();
		s.hash = kHashSeed;
		s.stencilSig = 0;
		s.lastValid = false;
	}

	void SoftRenderer::resetCache()
	{
	}

	unsigned int SoftRenderer::createTexture(const Texture::Type _type, const bool _linear, const bool _repeat, const unsigned int _width, const unsigned int _height, void* _data)
	{
		std::shared_ptr<Soft::Texture> texture = std::make_shared<Soft::Texture>();
		texture->linear = _linear;
		texture->repeat = _repeat;

		SoftState& s = S();
		{
			std::lock_guard<std::mutex> lock(s.textureLock);
			texture->id = s.nextTexture++;
			s.textures[texture->id] = texture;
		}

		updateTexture(texture->id, _type, (unsigned int)-1, (unsigned int)-1, _width, _height, _data);
		return texture->id;
	}

	void SoftRenderer::destroyTexture(const unsigned int _texture)
	{
		SoftState& s = S();
		std::lock_guard<std::mutex> lock(s.textureLock);
		auto it = s.textures.find(_texture);
		if (it == s.textures.end())
			return;
		if (s.bound == it->second)
			s.bound.reset();
		s.textures.erase(it); // a draw recorded this frame keeps its own reference
	}

	void SoftRenderer::updateTexture(const unsigned int _texture, const Texture::Type _type, const unsigned int _x, const unsigned _y, const unsigned int _width, const unsigned int _height, void* _data)
	{
		SoftState& s = S();
		std::shared_ptr<Soft::Texture> texture;
		{
			std::lock_guard<std::mutex> lock(s.textureLock);
			auto it = s.textures.find(_texture);
			if (it == s.textures.end())
				return;
			texture = it->second;
		}

		const bool whole = _x == (unsigned int)-1 && _y == (unsigned int)-1;

		// replaced while the frame being recorded draws it: the frame keeps the old one, as it was drawn
		// (a part updated in place is a font's new glyphs, in a place nothing drew yet)
		const long owners = 2 + (s.bound == texture ? 1 : 0); // the table, this function, the bound texture
		if (whole && texture.use_count() > owners)
		{
			std::shared_ptr<Soft::Texture> copy = std::make_shared<Soft::Texture>(*texture);
			std::lock_guard<std::mutex> lock(s.textureLock);
			s.textures[_texture] = copy;
			if (s.bound == texture)
				s.bound = copy;
			texture = copy;
		}

		unsigned int x = _x, y = _y;
		if (whole)
		{
			texture->width = (int)_width;
			texture->height = (int)_height;
			texture->pixels.assign((size_t)_width * _height, 0);
			x = 0;
			y = 0;
		}

		bool opaque = whole ? true : texture->opaque;
		if (_data == nullptr)
			opaque = false;

		// a part that starts past the texture's right edge (GL refuses it): nothing to write (the count below
		// would wrap around and write past the row)
		if (_data != nullptr && texture->width > 0 && x < (unsigned int)texture->width)
		{
			const uint8_t* src = (const uint8_t*)_data;
			for (unsigned int row = 0; row < _height; row++)
			{
				if (y + row >= (unsigned int)texture->height)
					break;
				uint32_t* dst = texture->pixels.data() + (size_t)(y + row) * texture->width + x;
				const unsigned int count = std::min(_width, (unsigned int)texture->width - x);
				if (_type == Texture::ALPHA)
				{
					const uint8_t* line = src + (size_t)row * _width;
					for (unsigned int i = 0; i < count; i++)
					{
						dst[i] = ((uint32_t)line[i] << 24) | 0x00FFFFFF;
						opaque = opaque && line[i] == 255;
					}
				}
				else
				{
					const uint8_t* line = src + (size_t)row * _width * 4;
					for (unsigned int i = 0; i < count; i++)
					{
						const uint8_t* p = line + i * 4;
						dst[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
						opaque = opaque && p[3] == 255;
					}
				}
			}
		}

		texture->opaque = opaque;
		texture->generation++;
		// its clear runs, for a whole picture (a part updated: a font's glyphs, none)
		if (whole)
			Soft::findClearRuns(*texture);
		else
		{
			texture->clearAt.clear();
			texture->clear.clear();
		}
	}

	void SoftRenderer::bindTexture(const unsigned int _texture)
	{
		SoftState& s = S();
		if (_texture == 0)
		{
			s.bound.reset();
			return;
		}
		std::lock_guard<std::mutex> lock(s.textureLock);
		auto it = s.textures.find(_texture);
		s.bound = it != s.textures.end() ? it->second : nullptr;
	}

	void SoftRenderer::drawLines(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		record(_vertices, _numVertices, Topology::Lines, _srcBlendFactor, _dstBlendFactor, nullptr);
	}

	void SoftRenderer::drawTriangleStrips(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor, bool verticesChanged)
	{
		record(_vertices, _numVertices, Topology::Strip, _srcBlendFactor, _dstBlendFactor, S().bound);
	}

	void SoftRenderer::drawTriangleFan(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		record(_vertices, _numVertices, Topology::Fan, _srcBlendFactor, _dstBlendFactor, S().bound);
	}

	void SoftRenderer::drawSolidRectangle(const float _x, const float _y, const float _w, const float _h, const unsigned int _fillColor, const unsigned int _borderColor, float borderWidth, float cornerRadius)
	{
		if (cornerRadius == 0.0f)
		{
			if (_fillColor != 0)
				drawRect(_x + borderWidth, _y + borderWidth, _w - borderWidth - borderWidth, _h - borderWidth - borderWidth, _fillColor);

			if (_borderColor != 0 && borderWidth > 0)
			{
				drawRect(_x, _y, _w, borderWidth, _borderColor);
				drawRect(_x + _w - borderWidth, _y + borderWidth, borderWidth, _h - borderWidth, _borderColor);
				drawRect(_x, _y + _h - borderWidth, _w - borderWidth, borderWidth, _borderColor);
				drawRect(_x, _y + borderWidth, borderWidth, _h - borderWidth - borderWidth, _borderColor);
			}
			return;
		}

		bindTexture(0);
		std::vector<Vertex> inner = createRoundRect(_x + borderWidth, _y + borderWidth, _w - borderWidth - borderWidth, _h - borderWidth - borderWidth, cornerRadius);

		if (_fillColor & 0xFF)
		{
			const unsigned int fill = convertColor(_fillColor);
			for (Vertex& v : inner)
				v.col = fill;
			record(inner.data(), (unsigned int)inner.size(), Topology::Fan, Blend::SRC_ALPHA, Blend::ONE_MINUS_SRC_ALPHA, nullptr);
		}

		if ((_borderColor & 0xFF) && borderWidth > 0)
		{
			std::vector<Vertex> outer = createRoundRect(_x, _y, _w, _h, cornerRadius);
			const unsigned int border = convertColor(_borderColor);
			for (Vertex& v : outer)
				v.col = border;

			record(inner.data(), (unsigned int)inner.size(), Topology::Fan, Blend::SRC_ALPHA, Blend::ONE_MINUS_SRC_ALPHA, nullptr, true);
			S().stencil = StencilTest::Outside;
			record(outer.data(), (unsigned int)outer.size(), Topology::Fan, Blend::SRC_ALPHA, Blend::ONE_MINUS_SRC_ALPHA, nullptr);
			S().stencil = StencilTest::Off;
		}
	}

	void SoftRenderer::setProjection(const Transform4x4f& _projection)
	{
		S().projection = _projection;
		updateTransform();
	}

	void SoftRenderer::setMatrix(const Transform4x4f& _matrix)
	{
		S().modelview = _matrix;
		updateTransform();
	}

	void SoftRenderer::setViewport(const Rect& _viewport)
	{
		S().viewport = _viewport;
		updateTransform();
	}

	void SoftRenderer::setScissor(const Rect& _scissor)
	{
		SoftState& s = S();
		s.scissor = !(_scissor.x == 0 && _scissor.y == 0 && _scissor.w == 0 && _scissor.h == 0);
		s.scissorRect = _scissor;
	}

	void SoftRenderer::setStencil(const Vertex* _vertices, const unsigned int _numVertices)
	{
		record(_vertices, _numVertices, Topology::Fan, Blend::SRC_ALPHA, Blend::ONE_MINUS_SRC_ALPHA, nullptr, true);
		S().stencil = StencilTest::Inside;
	}

	void SoftRenderer::disableStencil()
	{
		S().stencil = StencilTest::Off;
	}

	void SoftRenderer::setSwapInterval()
	{
		SoftState& s = S();
		s.vsync = Settings::getInstance()->getBool("VSync");
		double fps = 60.0;
		const char* value = getenv("ES_SOFT_FPS");
		if (value != nullptr)
			fps = atof(value);
		s.interval = fps > 0 ? 1.0 / fps : 0.0;
	}

	void SoftRenderer::swapBuffers()
	{
		SoftState& s = S();
		StartupTrace::frameShown();

		// a frame with nothing drawn (ES swaps while it loads) leaves the last one on screen
		const bool recorded = !s.commands.empty();
		const bool redraw = recorded && (s.forceRedraw || s.bench || s.hash != s.lastHash);
		if (redraw && s.open)
		{
			const double esMs = s.frameStarted ? msSince(s.frameStart) : 0;
			s.stats.esMs += esMs;
			s.stats.esMax = std::max(s.stats.esMax, esMs);
			// what changed since the last frame drawn, when it can be drawn in part (a benchmark draws all)
			s.partial = s.dirty && !s.bench && s.lastValid && !s.forceRedraw && findParts();
			// drawn and copied to the screen, each band by its thread; the times are the slowest band's
			if (!s.partial || !s.rects.empty() || s.bands.rasters.empty())
				execute();
			else
			{
				// the same pixels as the last frame (its draws in another order, apart): nothing to do
				std::fill(s.bands.ms.begin(), s.bands.ms.end(), 0.0);
				std::fill(s.bands.copyMs.begin(), s.bands.copyMs.end(), 0.0);
				if (s.output)
					s.output->finishFrame(0);
			}
			if ((s.partial || s.stripRows > 0) && s.dirtyCheck)
				checkParts();
			uint64_t redrawn = 0;
			if (!s.partial)
				redrawn = (uint64_t)s.width * s.height;
			else
				for (const Box& r : s.rects)
					redrawn += (uint64_t)(r.x1 - r.x0) * (r.y1 - r.y0);
			s.stats.redrawn += redrawn;
			s.stats.partial += s.partial ? 1 : 0;
			// the raster now holds this frame: its draws, for the next one
			s.lastDrawn.clear();
			for (const Command& command : s.commands)
				if (!command.stencil)
					s.lastDrawn.push_back({ command.sig, command.box });
			s.lastValid = true;
			Soft::scaledFrame();
			const double rasterMs = *std::max_element(s.bands.ms.begin(), s.bands.ms.end());
			const double copyMs = *std::max_element(s.bands.copyMs.begin(), s.bands.copyMs.end());

			Stats& st = s.stats;
			st.drawn++;
			dumpCommands(s.pendingDump);
			s.pendingDump.clear();
			if (!s.pendingCapture.empty())
			{
				captureFrame(s.pendingCapture);
				s.pendingCapture.clear();
			}

			// ES_SOFT_SLOW_DUMP=PREFIX: the first frames that took long to draw (ES_SOFT_SLOW_MS, default 120 ms;
			// ES_SOFT_SLOW_DUMPS of them, default 4), each draw with what it drew and its time on one thread,
			// in PREFIX1.txt, PREFIX2.txt...: what to make faster, seen on the machine itself
			static const char* slowPrefix = getenv("ES_SOFT_SLOW_DUMP");
			if (slowPrefix != nullptr && *slowPrefix != 0)
			{
				static const double slowMs = getenv("ES_SOFT_SLOW_MS") ? atof(getenv("ES_SOFT_SLOW_MS")) : 120.0;
				static const int most = getenv("ES_SOFT_SLOW_DUMPS") ? atoi(getenv("ES_SOFT_SLOW_DUMPS")) : 4;
				static int written = 0;
				if (rasterMs >= slowMs && written < most)
				{
					char header[256];
					snprintf(header, sizeof(header), "frame drawn in %.1f ms by %d threads (%s %.0f%% of the screen, %zu parts), "
						"copy %.1f ms, ES %.1f ms before it, %dx%d", rasterMs, s.bands.count, s.partial ? "in part," : "whole,",
						100.0 * redrawn / std::max(1, s.width * s.height), s.partial ? s.rects.size() : (size_t)1,
						copyMs, esMs, s.width, s.height);
					dumpCommands(std::string(slowPrefix) + std::to_string(++written) + ".txt", header);
					// ES_SOFT_SLOW_CAPTURE=1: the frame itself too, to draw again with es-mister-replay
					static const bool slowCapture = getenv("ES_SOFT_SLOW_CAPTURE") != nullptr && strcmp(getenv("ES_SOFT_SLOW_CAPTURE"), "0") != 0;
					if (slowCapture)
						captureFrame(std::string(slowPrefix) + std::to_string(written) + ".escf");
				}
			}
			// the first frame on the screen (the splash), in the startup trace
			static bool shownOnce = false;
			if (!shownOnce && s.output != nullptr)
			{
				shownOnce = true;
				StartupTrace::mark("first frame shown");
			}

			// the time from a press to the frame that shows its effect, and the frames over a 60 Hz frame's time
			double answeredMs;
			if (StartupTrace::inputAnswered(answeredMs))
			{
				st.answers.push_back((float)answeredMs);
				static bool first = true;
				if (first && StartupTrace::enabled())
				{
					first = false;
					StartupTrace::mark("first press answered", std::to_string((int)answeredMs) + " ms");
				}
			}
			if (esMs + rasterMs + copyMs > 1000.0 / 60.0)
				st.overBudget++;

			st.rasterMs += rasterMs;
			st.rasterMax = std::max(st.rasterMax, rasterMs);
			st.copyMs += copyMs;
			st.copied += s.output ? (uint64_t)s.output->lastPixelsCopied() : 0;
			st.bandMs[0] += s.bands.ms.size() > 0 ? s.bands.ms[0] : 0;
			st.bandMs[1] += s.bands.ms.size() > 1 ? s.bands.ms[1] : 0;
			st.vertices += s.vertices.size();
			st.pixels += s.raster.stats().pixels;
			st.quads += s.raster.stats().quads;
			st.triangles += s.raster.stats().triangles;
			for (int k = 0; k < Soft::PX_KINDS; k++)
				st.kinds[k] += s.raster.stats().kinds[k];

			// a frame that kept the screen still, in the startup trace (with what ES did meanwhile there)
			if (StartupTrace::enabled() && (esMs >= 150 || rasterMs >= 150))
			{
				char detail[96];
				snprintf(detail, sizeof(detail), "ES %.0f ms before it, drawing %.0f ms (%.0f%% of the screen)", esMs, rasterMs,
					100.0 * redrawn / std::max(1, s.width * s.height));
				StartupTrace::mark("slow frame", detail);
			}
		}
		else if (StartupTrace::inputWaitingMs() > 250)
		{
			// a press that changed nothing on the screen (frames went on, the same): not counted
			double ignored;
			StartupTrace::inputAnswered(ignored);
		}

		if (s.scriptLoaded && s.script.active())
		{
			s.script.frame(s.raster.pixels(), s.raster.width(), s.raster.height(), redraw);
			const std::string dump = s.script.takeDump();
			if (!dump.empty())
				s.pendingDump = dump;
			const std::string capture = s.script.takeCapture();
			if (!capture.empty())
				s.pendingCapture = capture;
			if (s.script.takeStats())
				s.statsNow = true;
		}

		if (recorded)
		{
			s.lastHash = s.hash;
			s.forceRedraw = false;
		}
		s.hash = kHashSeed;
		s.stencilSig = 0;
		s.commands.clear();
		s.vertices.clear();

		// the frame's time, from the end of the last one's wait to the end of this one's drawing
		Stats& st = s.stats;
		if (s.frameStarted && recorded)
		{
			const double frameMs = msSince(s.frameStart);
			st.frames++;
			st.frameMs += frameMs;
			st.frameMax = std::max(st.frameMax, frameMs);
			if ((s.statsEvery > 0 && st.frames >= s.statsEvery) || s.statsNow)
				logStats();
			s.statsNow = false;
		}

		// a steady rate in place of the display's vertical sync
		if (s.vsync && s.interval > 0)
		{
			const Clock::time_point now = Clock::now();
			const Clock::duration interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(s.interval));
			if (s.nextFrame < now - interval)
				s.nextFrame = now;
			s.nextFrame += interval;
			if (s.nextFrame > now)
				std::this_thread::sleep_until(s.nextFrame);
		}

		s.frameStart = Clock::now();
		s.frameStarted = true;
	}

	size_t SoftRenderer::getTotalMemUsage()
	{
		SoftState& s = S();
		std::lock_guard<std::mutex> lock(s.textureLock);
		size_t total = 0;
		for (auto& texture : s.textures)
			total += texture.second->pixels.size() * 4;
		return total + Soft::scaledBytes();
	}

#ifdef MISTER
	void releaseScreen()
	{
		SoftState& s = S();
		if (s.output)
			s.output->close();
		s.open = false;
	}

	void reclaimScreen()
	{
		SoftState& s = S();
		if (!s.output || s.open)
			return;
		std::string error;
		int width = 0, height = 0;
		if (!s.output->open(width, height, error))
		{
			LOG(LogError) << "SoftRenderer: the screen after a game: " << error;
			return;
		}
		if (width != s.width || height != s.height)
			LOG(LogWarning) << "SoftRenderer: the screen is " << width << "x" << height << " after a game, ES draws at " << s.width << "x" << s.height;
		s.open = true;
		s.forceRedraw = true;
		s.lastValid = false;
		LOG(LogInfo) << "SoftRenderer: output " << s.output->describe() << " again";
	}
#endif
}

#endif // RENDERER_SOFTWARE
