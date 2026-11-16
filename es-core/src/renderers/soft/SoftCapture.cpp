#include "SoftCapture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>

namespace Renderer
{
	namespace Soft
	{
		namespace
		{
			// "ESCF", then the version: little-endian fields, as the machines that write and read them are
			const char kMagic[4] = { 'E', 'S', 'C', 'F' };
			const uint32_t kVersion = 1;

			class Writer
			{
			public:
				explicit Writer(FILE* f) : mFile(f) { }
				template<typename T> void put(const T& value) { mOk = mOk && fwrite(&value, sizeof(T), 1, mFile) == 1; }
				template<typename T> void putVector(const std::vector<T>& values)
				{
					put((uint32_t)values.size());
					if (!values.empty())
						mOk = mOk && fwrite(values.data(), sizeof(T), values.size(), mFile) == values.size();
				}
				bool ok() const { return mOk; }
			private:
				FILE* mFile;
				bool mOk = true;
			};

			class Reader
			{
			public:
				explicit Reader(FILE* f) : mFile(f) { }
				template<typename T> T get()
				{
					T value{};
					mOk = mOk && fread(&value, sizeof(T), 1, mFile) == 1;
					return value;
				}
				template<typename T> void getVector(std::vector<T>& values, size_t most)
				{
					const uint32_t count = get<uint32_t>();
					if (!mOk || count > most)
					{
						mOk = false;
						return;
					}
					values.resize(count);
					if (count > 0)
						mOk = mOk && fread(values.data(), sizeof(T), count, mFile) == count;
				}
				bool ok() const { return mOk; }
			private:
				FILE* mFile;
				bool mOk = true;
			};

			void putState(Writer& w, const DrawState& s)
			{
				w.put(s.srcBlend);
				w.put(s.dstBlend);
				w.put((uint8_t)s.stencil);
				w.put((uint8_t)s.scissor);
				w.put((int32_t)s.sx0); w.put((int32_t)s.sy0); w.put((int32_t)s.sx1); w.put((int32_t)s.sy1);
				w.put(s.saturation);
			}

			void getState(Reader& r, DrawState& s)
			{
				s.srcBlend = r.get<uint8_t>();
				s.dstBlend = r.get<uint8_t>();
				s.stencil = (StencilTest)r.get<uint8_t>();
				s.scissor = r.get<uint8_t>() != 0;
				s.sx0 = r.get<int32_t>(); s.sy0 = r.get<int32_t>(); s.sx1 = r.get<int32_t>(); s.sy1 = r.get<int32_t>();
				s.saturation = r.get<float>();
				s.texture = nullptr;
			}
		}

		bool saveCapture(const std::string& path, const Capture& capture)
		{
			FILE* f = fopen(path.c_str(), "wb");
			if (f == nullptr)
				return false;
			Writer w(f);
			for (char c : kMagic)
				w.put(c);
			w.put(kVersion);
			w.put((int32_t)capture.width);
			w.put((int32_t)capture.height);

			w.put((uint32_t)capture.textures.size());
			for (const auto& t : capture.textures)
			{
				w.put((uint32_t)t->id);
				w.put((int32_t)t->width);
				w.put((int32_t)t->height);
				w.put((uint8_t)((t->linear ? 1 : 0) | (t->repeat ? 2 : 0) | (t->opaque ? 4 : 0)));
				w.put((uint64_t)t->generation);
				w.putVector(t->pixels);
				w.putVector(t->clearAt);
				w.putVector(t->clear);
			}

			w.put((uint32_t)capture.commands.size());
			for (const CapturedCommand& c : capture.commands)
			{
				w.put((uint8_t)c.stencil);
				w.put((uint8_t)c.topology);
				w.put((int32_t)c.texture);
				putState(w, c.state);
				w.putVector(c.vertices);
			}
			const bool ok = w.ok();
			return fclose(f) == 0 && ok;
		}

		bool loadCapture(const std::string& path, Capture& capture, std::string& error)
		{
			capture = Capture();
			FILE* f = fopen(path.c_str(), "rb");
			if (f == nullptr)
			{
				error = "cannot open " + path;
				return false;
			}
			Reader r(f);
			char magic[4];
			for (char& c : magic)
				c = r.get<char>();
			const uint32_t version = r.get<uint32_t>();
			if (!r.ok() || memcmp(magic, kMagic, 4) != 0 || version != kVersion)
			{
				fclose(f);
				error = path + ": not a frame capture (or another version)";
				return false;
			}
			capture.width = r.get<int32_t>();
			capture.height = r.get<int32_t>();
			const uint32_t textures = r.get<uint32_t>();
			const bool sane = capture.width > 0 && capture.height > 0 && capture.width <= 8192 && capture.height <= 8192 && textures < 100000;
			for (uint32_t i = 0; sane && r.ok() && i < textures; i++)
			{
				auto t = std::make_shared<Texture>();
				t->id = r.get<uint32_t>();
				t->width = r.get<int32_t>();
				t->height = r.get<int32_t>();
				const uint8_t flags = r.get<uint8_t>();
				t->linear = (flags & 1) != 0;
				t->repeat = (flags & 2) != 0;
				t->opaque = (flags & 4) != 0;
				t->generation = r.get<uint64_t>();
				const size_t texels = t->width > 0 && t->height > 0 && t->width <= 16384 && t->height <= 16384 ? (size_t)t->width * t->height : 0;
				r.getVector(t->pixels, texels);
				r.getVector(t->clearAt, (size_t)std::max(0, t->height) + 1);
				r.getVector(t->clear, texels + 2);
				if (t->pixels.size() != texels)
					break;
				capture.textures.push_back(t);
			}
			const uint32_t commands = r.get<uint32_t>();
			for (uint32_t i = 0; sane && r.ok() && i < commands && i < 1000000; i++)
			{
				CapturedCommand c;
				c.stencil = r.get<uint8_t>() != 0;
				c.topology = (Topology)r.get<uint8_t>();
				c.texture = r.get<int32_t>();
				getState(r, c.state);
				r.getVector(c.vertices, 1 << 24);
				if (c.texture >= (int)capture.textures.size() || (uint8_t)c.topology > (uint8_t)Topology::Lines)
					break;
				capture.commands.push_back(std::move(c));
			}
			const bool ok = sane && r.ok() && capture.textures.size() == textures && capture.commands.size() == commands;
			fclose(f);
			if (!ok)
				error = path + ": cut short or damaged";
			return ok;
		}

		namespace
		{
			// one part of a band: cleared, then the commands that reach it clipped to it (Renderer_Soft's
			// executePart: a stencil shape out of it only ends the last one)
			void drawPart(const Capture& capture, const std::vector<Bounds>& bounds, bool oldStencil, Raster& raster, int x0, int y0, int x1, int y1)
			{
				const Bounds part = { x0, y0, x1, y1 };
				// cleared before the first command that reaches it is drawn (Renderer_Soft's executePart)
				bool cleared = false;
				auto clearPart = [&]
				{
					if (!cleared)
						raster.clearBox(x0, y0, x1, y1, 0);
					cleared = true;
				};
				auto stateIn = [&](const CapturedCommand& c)
				{
					DrawState state = c.state;
					if (state.scissor)
					{
						state.sx0 = std::max(state.sx0, x0); state.sy0 = std::max(state.sy0, y0);
						state.sx1 = std::min(state.sx1, x1); state.sy1 = std::min(state.sy1, y1);
					}
					else
					{
						state.scissor = true;
						state.sx0 = x0; state.sy0 = y0; state.sx1 = x1; state.sy1 = y1;
					}
					return state;
				};
				auto candidate = [&](size_t i)
				{
					const CapturedCommand& c = capture.commands[i];
					return layerCandidate(c.stencil, c.state, c.topology, (int)c.vertices.size(), bounds[i], part);
				};
				const bool layers = layersOn() && !oldStencil;
				const size_t total = capture.commands.size();
				for (size_t i = 0; i < total; i++)
				{
					const CapturedCommand& c = capture.commands[i];
					if (!bounds[i].meets(part) && !(c.stencil && oldStencil))
					{
						if (c.stencil)
							raster.resetStencil();
						continue;
					}
					// big rectangles one over the other drawn together (Renderer_Soft's executePart)
					if (layers && candidate(i))
					{
						LayerDraw run[8];
						size_t at[8];
						int m = 0;
						for (size_t j = i; j < total && m < 8; j++)
						{
							const CapturedCommand& d = capture.commands[j];
							if (!bounds[j].meets(part) && !d.stencil)
								continue;
							if (!candidate(j))
								break;
							run[m].vertices = d.vertices.data();
							run[m].count = (int)d.vertices.size();
							run[m].topology = d.topology;
							run[m].state = stateIn(d);
							run[m].state.texture = d.texture >= 0 ? capture.textures[d.texture].get() : nullptr;
							at[m++] = j;
						}
						const int done = m >= 2 ? raster.drawLayers(run, m, cleared ? nullptr : &part) : 0;
						if (done >= 2)
						{
							cleared = true;
							i = at[done - 1];
							continue;
						}
					}
					DrawState state = stateIn(c);
					// the first draw writing every pixel of the part: no clearing before it
					if (!cleared && !c.stencil)
					{
						DrawState drawn = state;
						drawn.texture = c.texture >= 0 ? capture.textures[c.texture].get() : nullptr;
						cleared = coversOpaque(c.vertices.data(), (int)c.vertices.size(), c.topology, drawn, part);
					}
					clearPart();
					if (c.stencil)
						raster.setStencil(c.vertices.data(), (int)c.vertices.size(), state);
					else
					{
						state.texture = c.texture >= 0 ? capture.textures[c.texture].get() : nullptr;
						raster.draw(c.vertices.data(), (int)c.vertices.size(), c.topology, state);
					}
				}
				clearPart();
			}

			void drawBand(const Capture& capture, const std::vector<Bounds>& bounds, bool oldStencil, Raster& band, int strip)
			{
				const int top = band.bandTop(), bottom = band.bandBottom();
				const int step = strip > 0 ? strip : std::max(1, bottom - top);
				for (int y = top; y < bottom; y += step)
					drawPart(capture, bounds, oldStencil, band, 0, y, capture.width, std::min(bottom, y + step));
			}
		}

		void drawCapture(const Capture& capture, Raster& owner, int threads, int strip, bool oldStencil)
		{
			if (owner.width() != capture.width || owner.height() != capture.height)
				owner.resize(capture.width, capture.height);
			threads = std::max(1, std::min(8, threads));
			std::vector<Bounds> bounds;
			bounds.reserve(capture.commands.size());
			for (const CapturedCommand& c : capture.commands)
				bounds.push_back(drawBounds(c.vertices.data(), (int)c.vertices.size(), c.topology, c.state, capture.width, capture.height));
			std::vector<Raster> bands(threads);
			for (int i = 0; i < threads; i++)
				bands[i].attach(owner, capture.height * i / threads, capture.height * (i + 1) / threads);
			std::vector<std::thread> workers;
			for (int i = 1; i < threads; i++)
				workers.emplace_back([&, i] { drawBand(capture, bounds, oldStencil, bands[i], strip); });
			drawBand(capture, bounds, oldStencil, bands[0], strip);
			for (auto& worker : workers)
				worker.join();
			// the bands marked the owner's stencil: cleared, for the next frame
			for (auto& band : bands)
				band.resetStencil();
			owner.stats() = RasterStats();
			for (auto& band : bands)
			{
				owner.stats().pixels += band.stats().pixels;
				owner.stats().quads += band.stats().quads;
				owner.stats().triangles += band.stats().triangles;
				owner.stats().together += band.stats().together;
				for (int k = 0; k < PX_KINDS; k++)
					owner.stats().kinds[k] += band.stats().kinds[k];
			}
			scaledFrame();
		}
	}
}
