#include "Renderer_Soft.h"
#include "utils/StartupTrace.h"

#ifdef RENDERER_SOFTWARE

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
			Box box;               // where it may draw (none for a stencil shape)
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
			uint64_t rows = 0, vertices = 0, pixels = 0;
			uint32_t quads = 0, triangles = 0;
			double bandMs[2] = { 0, 0 }; // the first two bands' drawing, to see the balance
			uint64_t kinds[Soft::PX_KINDS] = {};
			int partial = 0;             // frames drawn in part (what changed)
			uint64_t redrawn = 0;        // pixels drawn again
			int checked = 0, wrong = 0;  // ES_SOFT_DIRTY_CHECK: frames drawn in part checked against whole ones
		};

		// The frame drawn in horizontal bands, one a thread: the first by the thread that swaps, the others
		// by workers. The line between the first two moves toward the one that took longer.
		struct Bands
		{
			int count = 1;
			std::vector<std::unique_ptr<Raster>> rasters;
			std::vector<double> ms;           // each band's drawing, the last frame
			std::vector<double> copyMs;       // and its copy to the output
			std::vector<int> copied;          // the rows it copied
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
				s.output.reset(nullptr);
				const char* old = getenv("ES_SOFT_OUTPUT");
				setenv("ES_SOFT_OUTPUT", "null", 1);
				s.output = createOutput();
				if (old != nullptr)
					setenv("ES_SOFT_OUTPUT", old, 1);
				else
					unsetenv("ES_SOFT_OUTPUT");
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

			// where it may draw: its vertices' box (a pixel's centre is inside the shape: a pixel more each
			// side for the rounding, two for lines), in the scissor and the frame
			if (!stencil)
			{
				Box box = { 0, 0, s.width, s.height };
				const SVertex* v = &s.vertices[command.first];
				float x0 = v[0].x, y0 = v[0].y, x1 = v[0].x, y1 = v[0].y;
				bool finite = true;
				for (unsigned int i = 0; i < count; i++)
				{
					finite = finite && std::isfinite(v[i].x) && std::isfinite(v[i].y);
					x0 = std::min(x0, v[i].x); x1 = std::max(x1, v[i].x);
					y0 = std::min(y0, v[i].y); y1 = std::max(y1, v[i].y);
				}
				if (finite)
				{
					const int margin = topology == Topology::Lines ? 2 : 1;
					auto limit = [](float value, int size) { return std::max(-8.0f, std::min((float)size + 8.0f, value)); };
					box.x0 = std::max(box.x0, (int)std::floor(limit(x0, s.width)) - margin);
					box.y0 = std::max(box.y0, (int)std::floor(limit(y0, s.height)) - margin);
					box.x1 = std::min(box.x1, (int)std::ceil(limit(x1, s.width)) + margin);
					box.y1 = std::min(box.y1, (int)std::ceil(limit(y1, s.height)) + margin);
				}
				if (command.state.scissor)
				{
					box.x0 = std::max(box.x0, command.state.sx0);
					box.y0 = std::max(box.y0, command.state.sy0);
					box.x1 = std::min(box.x1, command.state.sx1);
					box.y1 = std::min(box.y1, command.state.sy1);
				}
				command.box = box.empty() ? Box() : box;
			}

			s.commands.push_back(std::move(command));
		}

		// the frame's commands, in one band: all of it, or (drawn in part) each part, cleared then drawn by
		// the commands that reach it, clipped to it
		void executeBand(Raster& raster)
		{
			SoftState& s = S();
			if (!s.partial)
			{
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
				part.y0 = std::max(part.y0, raster.bandTop());
				part.y1 = std::min(part.y1, raster.bandBottom());
				if (part.empty())
					continue;
				raster.clearBox(part.x0, part.y0, part.x1, part.y1, 0);
				for (const Command& command : s.commands)
				{
					// a stencil shape is always set: it also clears the last one
					if (!command.stencil && !command.box.meets(part))
						continue;
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
		}

		// the rows a band shows again: all of them, or (drawn in part) its parts' rows
		int presentBand(Raster& raster)
		{
			SoftState& s = S();
			const int top = raster.bandTop(), bottom = raster.bandBottom();
			if (!s.partial)
				return s.output->presentRows(s.raster.pixels(), s.raster.width(), s.raster.height(), top, bottom);

			std::vector<std::pair<int, int>> spans;
			for (const Box& r : s.rects)
			{
				const int y0 = std::max(r.y0, top), y1 = std::min(r.y1, bottom);
				if (y0 < y1)
					spans.push_back({ y0, y1 });
			}
			std::sort(spans.begin(), spans.end());
			int copied = 0;
			for (size_t i = 0; i < spans.size();)
			{
				int y0 = spans[i].first, y1 = spans[i].second;
				for (i++; i < spans.size() && spans[i].first <= y1; i++)
					y1 = std::max(y1, spans[i].second);
				copied += s.output->presentRows(s.raster.pixels(), s.raster.width(), s.raster.height(), y0, y1);
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
			LOG(LogError) << "SoftRenderer: drawn in part, " << count << " pixels differ from the whole frame, in "
				<< at.x0 << "," << at.y0 << " " << (at.x1 - at.x0) << "x" << (at.y1 - at.y0) << " (" << s.rects.size() << " parts)";
			// the next frames checked on their own
			memcpy(s.raster.pixels(), right, (size_t)w * h * 4);
		}

		// ES_SOFT_DUMP=FILE: the commands of the drawn frame ES_SOFT_DUMP_FRAME (default 100), each with what
		// it drew and its time, drawn again alone on one thread, for tuning
		// (or the test script's "dump NAME")
		void dumpCommands(const std::string& requested)
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
				int copied = 0;
				for (int rows : b.copied)
					copied += rows;
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
			char line[1024];
			snprintf(line, sizeof(line),
				"SoftRenderer: %d frames, %d drawn; frame %.1f ms (max %.1f), ES before a drawn frame %.1f ms (max %.1f), drawing %.1f ms (max %.1f)%s, copy %.2f ms, %.0f rows copied, "
				"%.0f vertices, %.0f k pixels, %u quads, %u triangles a drawn frame%s; k pixels: fill %.0f, copy %.0f, colour %.0f, "
				"picture 1:1 %.0f, nearest %.0f, bilinear %.0f, triangles %.0f",
				st.frames, st.drawn, st.frameMs / st.frames, st.frameMax, st.esMs / drawn, st.esMax, st.rasterMs / drawn, st.rasterMax, bands, st.copyMs / drawn,
				(double)st.rows / drawn, (double)st.vertices / drawn, (double)st.pixels / drawn / 1000.0, st.quads / drawn, st.triangles / drawn, parts,
				kpx(Soft::PX_FILL), kpx(Soft::PX_COPY), kpx(Soft::PX_COLOR), kpx(Soft::PX_TEX_1TO1), kpx(Soft::PX_TEX_NEAREST), kpx(Soft::PX_TEX_LINEAR), kpx(Soft::PX_TRIANGLE));
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

		if (_data != nullptr && texture->width > 0)
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
			if (s.partial && s.dirtyCheck)
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
			st.rasterMs += rasterMs;
			st.rasterMax = std::max(st.rasterMax, rasterMs);
			st.copyMs += copyMs;
			st.rows += s.output ? s.output->lastRowsCopied() : 0;
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

		if (s.scriptLoaded && s.script.active())
		{
			s.script.frame(s.raster.pixels(), s.raster.width(), s.raster.height(), redraw);
			const std::string dump = s.script.takeDump();
			if (!dump.empty())
				s.pendingDump = dump;
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
