#pragma once
#ifndef ES_CORE_RENDERERS_SOFT_RASTER_H
#define ES_CORE_RENDERERS_SOFT_RASTER_H

// The software renderer's rasterizer (Renderer_Soft): triangles, quads and lines drawn into a 32-bit
// target in memory, as OpenGL ES 1.0 would draw them with the state the renderer uses. Pixels are
// 0xAARRGGBB. A texture is sampled as GL_MODULATE does it (its texel times the vertex colour), nearest
// or bilinear, clamped to its edge or repeated; blending takes the factors of Renderer::Blend. Pixel
// centres are at +0.5, and a pixel is covered when its centre is inside the shape (left and top edges
// in, right and bottom edges out), so shapes sharing an edge do not blend twice along it.

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
			uint64_t kinds[PX_KINDS] = {}; // pixels by kind: opaque fills, opaque copies, blended colour, textures 1:1, nearest, bilinear, triangles
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

			// The stencil shape: cleared, then the fan's coverage marked (inside the scissor)
			void setStencil(const SVertex* vertices, int count, const DrawState& state);

			RasterStats& stats() { return mStats; }

		private:
			struct Clip { int x0, y0, x1, y1; };
			Clip clipOf(const DrawState& state) const;

			void drawQuad(const SVertex* v, const DrawState& state, const Clip& clip);
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
		};

		// Big pictures drawn scaled keep their scaled pixels (ES_SOFT_SCALED_MB, default 40; 0: none): after
		// each drawn frame, those not drawn for a while are let go of
		void scaledFrame();
		size_t scaledBytes();
	}
}

#endif
