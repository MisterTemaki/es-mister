#include "Renderer_Soft.h"

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
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
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

		struct Command
		{
			bool stencil = false;  // the stencil shape, not a draw
			Topology topology = Topology::Strip;
			uint32_t first = 0, count = 0;
			DrawState state;
			std::shared_ptr<Soft::Texture> texture;
		};

		struct Stats
		{
			int frames = 0, drawn = 0;
			double frameMs = 0, frameMax = 0;
			double rasterMs = 0, rasterMax = 0;
			double copyMs = 0;
			uint64_t rows = 0, vertices = 0, pixels = 0;
			uint32_t quads = 0, triangles = 0;
		};

		struct SoftState
		{
			std::unique_ptr<Output> output;
			bool open = false;
			int width = 0, height = 0;

			Raster raster;

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

			bool vsync = true;
			double interval = 1.0 / 60.0;
			Clock::time_point nextFrame;
			Clock::time_point frameStart;
			bool frameStarted = false;

			Script script;
			bool scriptLoaded = false;

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

			uint64_t h = s.hash;
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
			s.hash = h;

			s.commands.push_back(std::move(command));
		}

		void execute()
		{
			SoftState& s = S();
			s.raster.clear(0);
			for (const Command& command : s.commands)
			{
				const SVertex* vertices = s.vertices.data() + command.first;
				if (command.stencil)
				{
					s.raster.setStencil(vertices, (int)command.count, command.state);
					continue;
				}
				DrawState state = command.state;
				state.texture = command.texture.get();
				s.raster.draw(vertices, (int)command.count, command.topology, state);
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
			char line[512];
			snprintf(line, sizeof(line),
				"SoftRenderer: %d frames, %d drawn; frame %.1f ms (max %.1f), drawing %.1f ms (max %.1f), copy %.2f ms, %.0f rows copied, "
				"%.0f vertices, %.0f k pixels, %u quads, %u triangles a drawn frame",
				st.frames, st.drawn, st.frameMs / st.frames, st.frameMax, st.rasterMs / drawn, st.rasterMax, st.copyMs / drawn,
				(double)st.rows / drawn, (double)st.vertices / drawn, (double)st.pixels / drawn / 1000.0, st.quads / drawn, st.triangles / drawn);
			LOG(LogInfo) << line;
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

		s.raster.resize(s.width, s.height);
		s.forceRedraw = true;
		s.frameStarted = false;

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
		if (s.output)
			s.output->close();
		s.open = false;
		s.commands.clear();
		s.vertices.clear();
		s.hash = kHashSeed;
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
		const Clock::time_point swapStart = Clock::now();

		// a frame with nothing drawn (ES swaps while it loads) leaves the last one on screen
		const bool recorded = !s.commands.empty();
		const bool redraw = recorded && (s.forceRedraw || s.bench || s.hash != s.lastHash);
		if (redraw && s.open)
		{
			s.raster.stats() = RasterStats();
			execute();
			const double rasterMs = msSince(swapStart);

			const Clock::time_point copyStart = Clock::now();
			if (s.output)
				s.output->present(s.raster.pixels(), s.raster.width(), s.raster.height());

			Stats& st = s.stats;
			st.drawn++;
			st.rasterMs += rasterMs;
			st.rasterMax = std::max(st.rasterMax, rasterMs);
			st.copyMs += msSince(copyStart);
			st.rows += s.output ? s.output->lastRowsCopied() : 0;
			st.vertices += s.vertices.size();
			st.pixels += s.raster.stats().pixels;
			st.quads += s.raster.stats().quads;
			st.triangles += s.raster.stats().triangles;
		}

		if (s.scriptLoaded && s.script.active())
			s.script.frame(s.raster.pixels(), s.raster.width(), s.raster.height(), redraw);

		if (recorded)
		{
			s.lastHash = s.hash;
			s.forceRedraw = false;
		}
		s.hash = kHashSeed;
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
			if (s.statsEvery > 0 && st.frames >= s.statsEvery)
				logStats();
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
		return total;
	}
}

#endif // RENDERER_SOFTWARE
