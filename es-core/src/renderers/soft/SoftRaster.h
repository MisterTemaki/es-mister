#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_RASTER_H
#define ES_CORE_RENDERERS_SOFT_RASTER_H

// The software renderer's rasterizer (Renderer_Soft): triangles, quads and lines drawn into a 32-bit
// target in memory, as OpenGL ES 1.0 would draw them with the state the renderer uses. Pixels are
// 0xAARRGGBB. A texture is sampled as GL_MODULATE does it (its texel times the vertex colour), nearest
// or bilinear, clamped to its edge or repeated; blending takes the factors of Renderer::Blend. Pixel
// centres are at +0.5, and a pixel is covered when its centre is inside the shape (left and top edges
// in, right and bottom edges out), so shapes sharing an edge do not blend twice along it.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace Renderer
{
	namespace Soft
	{
		struct Texture
		{
			unsigned int id = 0;
			int width = 0, height = 0;
			bool linear = true;   // magnification filter (minification is always bilinear, as GL_LINEAR)
			bool repeat = false;  // GL_REPEAT, else GL_CLAMP_TO_EDGE
			bool opaque = false;  // every texel's alpha is 255: drawn 1:1 with no colour, its rows are copied
			uint64_t generation = 0; // bumped at each change, for the frame's hash
			std::vector<uint32_t> pixels; // 0xAARRGGBB; row 0 is v = 0, as uploaded
			// its rows' runs of clear texels (alpha 0, 32 or more), which a picture drawn 1:1 over the frame
			// leaves as they are: row y's are clear[clearAt[y]] to clear[clearAt[y + 1]], first and last + 1
			// texel by turns (none: no runs)
			std::vector<uint32_t> clearAt, clear;
		};

		// A texture's clear runs (Texture::clear), after a whole upload
		void findClearRuns(Texture& texture);

		// A part of the frame, in pixels, half-open
		struct Bounds
		{
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			bool empty() const { return x0 >= x1 || y0 >= y1; }
			bool meets(const Bounds& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
		};

		// A vertex in target pixels (top left origin) with its texture coordinates and colour (0xAARRGGBB)
		struct SVertex
		{
			float x, y;
			float u, v;
			uint32_t color;
		};

		enum class Topology : uint8_t { Strip, Fan, Lines };

		enum class StencilTest : uint8_t
		{
			Off,
			Inside,  // drawn where the stencil shape is (GL_EQUAL 1)
			Outside, // drawn where it is not (GL_NOTEQUAL 1)
		};

		struct DrawState
		{
			const Texture* texture = nullptr; // none: the vertex colour alone
			uint8_t srcBlend = 4;             // Blend::Factor values
			uint8_t dstBlend = 5;
			StencilTest stencil = StencilTest::Off;
			bool scissor = false;
			int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0; // half-open
			float saturation = 1.0f;
		};

		// what a frame's pixels were, to see where the drawing time goes
		enum PixelKind { PX_FILL, PX_COPY, PX_COLOR, PX_TEX_1TO1, PX_TEX_NEAREST, PX_TEX_LINEAR, PX_TRIANGLE, PX_KINDS };

		struct RasterStats
		{
			uint64_t pixels = 0;   // pixels written
			uint32_t quads = 0;    // drawn on the axis-aligned path
			uint32_t triangles = 0;
			uint32_t together = 0; // draws drawn with others (Raster::drawLayers)
			uint64_t kinds[PX_KINDS] = {}; // pixels by kind: opaque fills, opaque copies, blended colour, textures 1:1, nearest, bilinear, triangles
		};

		// One draw of a run drawn together (Raster::drawLayers)
		struct LayerDraw
		{
			const SVertex* vertices = nullptr;
			int count = 0;
			Topology topology = Topology::Strip;
			DrawState state;
		};

		class Raster
		{
		public:
			void resize(int width, int height);
			int width() const { return mWidth; }
			int height() const { return mHeight; }
			uint32_t* pixels() { return mPx; }
			const uint32_t* pixels() const { return mPx; }

			// Draws into another raster's pixels and stencil, in its rows y0 to y1 only: a band of the frame,
			// drawn while other threads draw the other bands. What it marked in the stencil is cleared first.
			void attach(Raster& owner, int y0, int y1);
			int bandTop() const { return mBandY0; }
			int bandBottom() const { return mBandY1; }

			// Clears the stencil this raster marked
			void resetStencil();

			// Clears its rows (all of them, or its band's)
			void clear(uint32_t color);
			// Clears a box of its rows (x1, y1 not in it)
			void clearBox(int x0, int y0, int x1, int y1, uint32_t color);

			void draw(const SVertex* vertices, int count, Topology topology, const DrawState& state);

			// Draws a run of draws together, when each is a rectangle whose rows are read as they are (a picture
			// 1:1, a big picture's scaled rows, a colour), blended normally, not tested against the stencil nor
			// desaturated: a row at a time, each pixel read and written once for all of them, its value through
			// each draw in turn as drawing them one after another gives (the same pixels). The layers of a
			// carousel's move (two backgrounds crossing, the frame over them) went to memory once each, and
			// memory (the DDR3 the FPGA reads the screen from) is what those frames wait for. Returns how many
			// of the draws, from the first, it drew: 0 when fewer than two would be drawn together (then draw
			// them one by one). uncleared: a part of its rows not cleared yet (nothing drawn there yet): cleared
			// (to 0) by it, the layers drawn from the clear colour without reading it (in a carousel's move the
			// part's clearing and reading it back were two of the five passes over memory).
			int drawLayers(const LayerDraw* draws, int count, const Bounds* uncleared = nullptr);

			// The stencil shape: cleared, then the fan's coverage marked (inside the scissor)
			void setStencil(const SVertex* vertices, int count, const DrawState& state);

			RasterStats& stats() { return mStats; }

		private:
			struct Clip { int x0, y0, x1, y1; };
			Clip clipOf(const DrawState& state) const;

			void drawQuad(const SVertex* v, const DrawState& state, const Clip& clip);

			// a draw as a layer of drawLayers: drawQuad describes it here (in mLayer) instead of drawing it
			struct Layer;
			bool layerOf(const LayerDraw& draw, Layer& out);
			Layer* mLayer = nullptr;
			void drawTriangle(const SVertex& a, const SVertex& b, const SVertex& c, const DrawState& state, const Clip& clip);
			void drawLine(const SVertex& a, const SVertex& b, const DrawState& state, const Clip& clip);

			int mWidth = 0, mHeight = 0;
			std::vector<uint32_t> mPixels; // its own, unless attached to another's
			std::vector<uint8_t> mStencil;
			uint32_t* mPx = nullptr;       // the pixels and stencil drawn into
			uint8_t* mSt = nullptr;
			int mBandY0 = 0, mBandY1 = 0;  // the rows it draws
			Clip mStencilBox = { 0, 0, 0, 0 }; // where the mask may be set, to clear only that
			RasterStats mStats;

			// a picture's rows sampled once (drawQuad): two texture rows, the frame row, and the columns'
			// texels and weights
			std::vector<uint32_t> mRowA, mRowB, mRowOut;
			std::vector<int32_t> mColX0, mColX1;
			std::vector<uint16_t> mColFx;
			// nearest and made bigger: each 8 pixels' first texel, and their bytes' indices in the 8 texels from it
			std::vector<int32_t> mNearAt;
			std::vector<uint8_t> mNearIdx;
		};

		// Where a draw or a stencil shape of a width x height frame may reach (empty: nowhere): its vertices'
		// box, a pixel more each side for the rounding (two for lines), in its scissor and the frame. A part
		// of the frame it does not meet is left as it was by a draw, and marked nowhere by a stencil shape.
		Bounds drawBounds(const SVertex* vertices, int count, Topology topology, const DrawState& state, int width, int height);

		// Whether a draw (its state in the part: its scissor narrowed to it) writes every pixel of the part with
		// alpha 255, whatever was there: a rectangle with alpha 255 at every vertex, opaque texels if textured
		// (1:1, or made bigger and not smoothed), blended normally, not stencil-tested, covering the part. The
		// part's clearing before it would be written over: left out (the grids' background).
		bool coversOpaque(const SVertex* vertices, int count, Topology topology, const DrawState& state, const Bounds& part);

		// Draws worth drawing together (Raster::drawLayers), in a part of the frame: a rectangle alone, not a
		// stencil shape nor tested against one, whose box (b) covers half of the part or more. ES_SOFT_LAYERS=0:
		// none, each drawn on its own (to compare).
		bool layersOn();
		inline bool layerCandidate(bool stencilShape, const DrawState& state, Topology topology, int count, const Bounds& b, const Bounds& part)
		{
			if (stencilShape || state.stencil != StencilTest::Off || topology != Topology::Strip || count != 4)
				return false;
			const int64_t w = std::max(0, std::min(b.x1, part.x1) - std::max(b.x0, part.x0));
			const int64_t h = std::max(0, std::min(b.y1, part.y1) - std::max(b.y0, part.y0));
			return w * h * 2 >= (int64_t)(part.x1 - part.x0) * (part.y1 - part.y0);
		}

		// Big pictures drawn scaled keep their scaled pixels (ES_SOFT_SCALED_MB, default 40; 0: none): after
		// each drawn frame, those not drawn for a while are let go of
		void scaledFrame();
		size_t scaledBytes();
	}
}

#endif
