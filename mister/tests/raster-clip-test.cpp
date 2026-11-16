// The software renderer's invariant that drawing only what changed rests on (Renderer_Soft's findParts,
// bands and strips): a draw clipped to a part of the frame leaves, inside that part, the same pixels as
// the draw unclipped. Random scenes (pictures 1:1, scaled, stretched big enough for the scaled-picture
// cache, repeated, tinted, desaturated; text-like strips; triangles, fans, lines; the stencil; every
// blend; scissors) are drawn whole, then again a part at a time into band rasters, as executePart does
// (each part cleared, every command that meets it clipped to it), and compared pixel by pixel. With the sanitizers it
// also checks that no draw touches memory outside the frame or the textures, whatever the coordinates.
#include "TestMain.h"
#include "renderers/soft/SoftCapture.h"
#include "renderers/soft/SoftRaster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

using namespace Renderer::Soft;

namespace
{
	struct Command
	{
		bool stencil = false;
		Topology topology = Topology::Strip;
		std::vector<SVertex> vertices;
		DrawState state;
		int texture = -1;
	};

	struct Scene
	{
		int width = 0, height = 0;
		std::vector<std::unique_ptr<Texture>> textures;
		std::vector<Command> commands;
	};

	unsigned int nextTextureId = 1000;

	class Random
	{
	public:
		explicit Random(uint32_t seed) : mEngine(seed) { }
		int range(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(mEngine); }
		float real(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(mEngine); }
		bool chance(int percent) { return range(0, 99) < percent; }
		// a coordinate: on whole pixels, half pixels or anywhere
		float coord(float lo, float hi)
		{
			const float v = real(lo, hi);
			switch (range(0, 2))
			{
			case 0: return (float)(int)v;
			case 1: return (float)(int)v + 0.5f;
			default: return v;
			}
		}
		uint32_t color()
		{
			const uint32_t alpha = chance(40) ? 255 : (chance(20) ? 0 : (uint32_t)range(1, 254));
			return (alpha << 24) | ((uint32_t)range(0, 255) << 16) | ((uint32_t)range(0, 255) << 8) | (uint32_t)range(0, 255);
		}
	private:
		std::mt19937 mEngine;
	};

	std::unique_ptr<Texture> makeTexture(Random& r)
	{
		std::unique_ptr<Texture> t(new Texture());
		t->id = nextTextureId++;
		t->generation = 1;
		t->width = r.chance(30) ? r.range(64, 200) : r.range(1, 40);
		t->height = r.range(1, 120);
		t->linear = r.chance(70);
		t->repeat = r.chance(25);
		t->pixels.resize((size_t)t->width * t->height);
		// opaque, alpha, clear runs (an overlay), font-like (white, alpha), a nine-patch's (a border that
		// varies around a flat middle, a shadow's or a frame's), flat with a few odd texels
		const int kind = r.range(0, 5);
		const uint32_t flat = r.chance(50) ? r.color() : (r.chance(50) ? 0xFFFFFFFFu : 0xFF000000u | (uint32_t)r.range(0, 0xFFFFFF));
		const int border = r.range(1, std::max(1, std::min(t->width, t->height) / 3));
		bool opaque = true;
		for (int y = 0; y < t->height; y++)
			for (int x = 0; x < t->width; x++)
			{
				uint32_t c = r.color();
				if (kind == 0)
					c |= 0xFF000000;
				else if (kind == 2 && (x / 37 + y / 11) % 2 == 0)
					c &= 0x00FFFFFF; // long clear runs
				else if (kind == 3)
					c = (c & 0xFF000000) | 0x00FFFFFF;
				else if (kind == 4 && x >= border && y >= border && x < t->width - border && y < t->height - border)
					c = flat;
				else if (kind == 5 && !r.chance(2))
					c = flat;
				opaque = opaque && (c >> 24) == 255;
				t->pixels[(size_t)y * t->width + x] = c;
			}
		t->opaque = opaque;
		findClearRuns(*t);
		return t;
	}

	void setColors(Random& r, std::vector<SVertex>& v)
	{
		const int mode = r.range(0, 3); // one colour, white, a vertical gradient, all different
		uint32_t a = r.color(), b = r.color();
		const bool black = r.chance(15); // a black tint (a shade): its alpha alone
		if (black)
		{
			a &= 0xFF000000;
			b &= 0xFF000000;
		}
		for (size_t i = 0; i < v.size(); i++)
		{
			switch (mode)
			{
			case 0: v[i].color = a; break;
			case 1: v[i].color = 0xFFFFFFFF; break;
			case 2: v[i].color = (i % 2 == 0) ? a : b; break; // v0, v2 top; v1, v3 bottom
			default: v[i].color = black ? r.color() & 0xFF000000 : r.color(); break;
			}
		}
	}

	// an axis-aligned quad as ES sends it: v0 top left, v1 bottom left, v2 top right, v3 bottom right
	std::vector<SVertex> quad(Random& r, const Scene& scene, const Texture* t)
	{
		float x0, y0, x1, y1;
		const int size = r.range(0, 9);
		if (size == 0)
		{
			// big: the scaled-picture cache (a quarter of the frame or more)
			x0 = r.coord(-20, 20); y0 = r.coord(-20, 20);
			x1 = x0 + r.coord(scene.width * 0.6f, scene.width * 1.3f); y1 = y0 + r.coord(scene.height * 0.6f, scene.height * 1.3f);
		}
		else if (size == 1 && t != nullptr)
		{
			// 1:1 with the texture (the direct and copy paths)
			x0 = r.coord(-30, (float)scene.width); y0 = r.coord(-30, (float)scene.height);
			x1 = x0 + t->width; y1 = y0 + t->height;
		}
		else if (size == 2)
		{
			// far outside or huge
			x0 = r.coord(-3000, 3000); y0 = r.coord(-3000, 3000);
			x1 = x0 + r.coord(-4000, 4000); y1 = y0 + r.coord(-4000, 4000);
		}
		else
		{
			x0 = r.coord(-40, (float)scene.width + 10); y0 = r.coord(-40, (float)scene.height + 10);
			x1 = x0 + r.coord(0, 120); y1 = y0 + r.coord(0, 90);
		}
		if (r.chance(10)) std::swap(x0, x1); // either axis may be reversed
		if (r.chance(10)) std::swap(y0, y1);

		float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
		if (t != nullptr)
		{
			const int map = r.range(0, 3);
			if (map == 1 && size == 1) { u0 = 0; u1 = 1; v0 = 1; v1 = 0; }           // uploaded bottom up, as ES does
			else if (map == 2) { u0 = r.real(-1, 1); u1 = r.real(-1, 3); v0 = r.real(-1, 1); v1 = r.real(-1, 3); } // repeat, partial
			else if (map == 3) { u0 = r.real(0.4f, 0.5f); u1 = u0; v0 = 0.5f; v1 = 0.5f; } // one texel stretched (a flat part)
			if (r.chance(30))
			{
				// a nine-patch's middle or edge: a part of the texture, stretched or made a little smaller
				u0 = r.real(0.0f, 0.45f); u1 = r.real(0.55f, 1.0f); v0 = r.real(0.0f, 0.45f); v1 = r.real(0.55f, 1.0f);
				if (r.chance(20)) std::swap(v0, v1);
			}
		}
		std::vector<SVertex> v(4);
		v[0] = { x0, y0, u0, v0, 0 };
		v[1] = { x0, y1, u0, v1, 0 };
		v[2] = { x1, y0, u1, v0, 0 };
		v[3] = { x1, y1, u1, v1, 0 };
		setColors(r, v);
		return v;
	}

	Scene makeScene(Random& r)
	{
		Scene scene;
		scene.width = r.range(40, 260);
		scene.height = r.range(30, 200);
		const int textures = r.range(1, 5);
		for (int i = 0; i < textures; i++)
			scene.textures.push_back(makeTexture(r));

		const int count = r.range(1, 28);
		bool stencilSet = false;
		for (int i = 0; i < count; i++)
		{
			Command c;
			const int kind = r.range(0, 99);
			c.texture = r.chance(75) ? r.range(0, textures - 1) : -1;
			const Texture* t = c.texture >= 0 ? scene.textures[c.texture].get() : nullptr;

			if (kind < 55)
			{
				c.vertices = quad(r, scene, t);
				// several quads joined by degenerate triangles (a line of text)
				if (r.chance(20))
				{
					const int glyphs = r.range(2, 6);
					std::vector<SVertex> line;
					for (int g = 0; g < glyphs; g++)
					{
						std::vector<SVertex> q = quad(r, scene, t);
						if (g > 0)
							line.push_back(q[0]);
						line.insert(line.end(), q.begin(), q.end());
						if (g + 1 < glyphs)
							line.push_back(q[3]);
					}
					c.vertices = line;
				}
			}
			else if (kind < 85)
			{
				// triangles: a strip or a fan of a few vertices anywhere
				c.topology = r.chance(50) ? Topology::Strip : Topology::Fan;
				const int n = r.range(3, 8);
				for (int k = 0; k < n; k++)
					c.vertices.push_back({ r.coord(-60, scene.width + 60.0f), r.coord(-60, scene.height + 60.0f), r.real(-0.5f, 1.5f), r.real(-0.5f, 1.5f), 0 });
				setColors(r, c.vertices);
			}
			else if (kind < 92)
			{
				c.topology = Topology::Lines;
				c.texture = -1;
				const int n = 2 * r.range(1, 4);
				for (int k = 0; k < n; k++)
					c.vertices.push_back({ r.coord(-30, scene.width + 30.0f), r.coord(-30, scene.height + 30.0f), 0, 0, 0 });
				setColors(r, c.vertices);
			}
			else
			{
				// a stencil shape (a rounded rectangle's fan, here any fan); the next draws test against it
				c.stencil = true;
				c.topology = Topology::Fan;
				c.texture = -1;
				const float cx = r.coord(0, (float)scene.width), cy = r.coord(0, (float)scene.height), rad = r.real(5, 80);
				const int n = r.range(3, 12);
				for (int k = 0; k < n; k++)
				{
					const float a = 6.2831853f * k / n;
					c.vertices.push_back({ cx + rad * std::cos(a), cy + rad * std::sin(a), 0, 0, 0xFFFFFFFF });
				}
				stencilSet = true;
			}

			if (!c.stencil)
			{
				static const uint8_t blends[][2] = { { 4, 5 }, { 4, 5 }, { 4, 5 }, { 1, 1 }, { 1, 5 }, { 0, 2 }, { 6, 0 }, { 4, 1 }, { 9, 8 }, { 2, 3 } };
				const int b = r.range(0, 9);
				c.state.srcBlend = blends[b][0];
				c.state.dstBlend = blends[b][1];
				if (stencilSet && r.chance(40))
					c.state.stencil = r.chance(50) ? StencilTest::Inside : StencilTest::Outside;
				c.state.saturation = r.chance(15) ? r.real(0, 1) : 1.0f;
			}
			if (r.chance(25))
			{
				c.state.scissor = true;
				c.state.sx0 = r.range(-20, scene.width);
				c.state.sy0 = r.range(-20, scene.height);
				c.state.sx1 = c.state.sx0 + r.range(0, scene.width);
				c.state.sy1 = c.state.sy0 + r.range(0, scene.height);
			}
			scene.commands.push_back(c);
		}
		return scene;
	}

	void run(Raster& raster, const Scene& scene, const Command& c, DrawState state)
	{
		if (c.stencil)
			raster.setStencil(c.vertices.data(), (int)c.vertices.size(), state);
		else
		{
			state.texture = c.texture >= 0 ? scene.textures[c.texture].get() : nullptr;
			raster.draw(c.vertices.data(), (int)c.vertices.size(), c.topology, state);
		}
	}

	int partsLeftUncleared = 0;

	// the scene drawn a part at a time, as Renderer_Soft's executePart: the part cleared before its first
	// command (not when that one writes every pixel of it: coversOpaque), then every command with its scissor
	// narrowed to the part
	void drawPart(Raster& band, const Scene& scene, int x0, int y0, int x1, int y1)
	{
		const Bounds part = { x0, y0, x1, y1 };
		bool cleared = false;
		for (const Command& c : scene.commands)
		{
			// as executePart: a command that does not meet the part is left out (a stencil shape only ends the
			// last one)
			if (!drawBounds(c.vertices.data(), (int)c.vertices.size(), c.topology, c.state, scene.width, scene.height).meets(part))
			{
				if (c.stencil)
					band.resetStencil();
				continue;
			}
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
			if (!cleared)
			{
				DrawState drawn = state;
				drawn.texture = c.texture >= 0 ? scene.textures[c.texture].get() : nullptr;
				if (!c.stencil && coversOpaque(c.vertices.data(), (int)c.vertices.size(), c.topology, drawn, part))
					partsLeftUncleared++;
				else
					band.clearBox(x0, y0, x1, y1, 0);
				cleared = true;
			}
			run(band, scene, c, state);
		}
		if (!cleared)
			band.clearBox(x0, y0, x1, y1, 0);
	}

	// The scene drawn whole, then in parts of two bands (a random line between them; random strips of rows
	// and columns, in a random order), as the renderer draws them; the pixels compared. Returns whether they
	// are the same (the first differing pixel reported while fewer than 5 scenes were), and adds the whole
	// frame's pixels to the hash.
	bool sameInParts(const Scene& scene, Random& r, int i, int differing, uint64_t& allPixels)
	{
		Raster whole;
		whole.resize(scene.width, scene.height);
		whole.clear(0);
		for (const Command& c : scene.commands)
			run(whole, scene, c, c.state);

		Raster owner, top, bottom;
		owner.resize(scene.width, scene.height);
		std::fill(owner.pixels(), owner.pixels() + (size_t)scene.width * scene.height, 0xDEADBEEFu);
		const int split = r.range(1, std::max(1, scene.height - 1));
		top.attach(owner, 0, split);
		bottom.attach(owner, split, scene.height);
		struct Part { Raster* band; int x0, y0, x1, y1; };
		std::vector<Part> parts;
		for (Raster* band : { &top, &bottom })
		{
			const int rows = r.chance(50) ? r.range(1, 20) : band->bandBottom() - band->bandTop();
			const int cols = r.chance(50) ? r.range(1, scene.width) : scene.width;
			for (int y = band->bandTop(); y < band->bandBottom(); y += rows)
				for (int x = 0; x < scene.width; x += cols)
					parts.push_back({ band, x, y, std::min(scene.width, x + cols), std::min(band->bandBottom(), y + rows) });
		}
		std::shuffle(parts.begin(), parts.end(), std::mt19937(i));
		// a band's parts go in any order, but the stencil a band marks is its own: the parts of one band
		// draw one after another, as in the renderer
		std::stable_sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) { return a.band < b.band; });
		for (const Part& p : parts)
			drawPart(*p.band, scene, p.x0, p.y0, p.x1, p.y1);

		const uint32_t* a = whole.pixels();
		const uint32_t* b = owner.pixels();
		for (size_t k = 0; k < (size_t)scene.width * scene.height; k++)
			allPixels = (allPixels ^ a[k]) * 1099511628211ULL;
		bool same = true;
		for (int y = 0; y < scene.height && same; y++)
			for (int x = 0; x < scene.width; x++)
				if (a[(size_t)y * scene.width + x] != b[(size_t)y * scene.width + x])
				{
					if (differing < 5)
						fprintf(stderr, "  scene %d (%dx%d, %zu commands): pixel %d,%d is %08x drawn whole, %08x in parts\n", i, scene.width, scene.height,
							scene.commands.size(), x, y, a[(size_t)y * scene.width + x], b[(size_t)y * scene.width + x]);
					same = false;
					break;
				}
		scaledFrame();
		return same;
	}
}

TEST(clipped_draws_leave_the_same_pixels)
{
	const int scenes = getenv("RASTER_SCENES") ? atoi(getenv("RASTER_SCENES")) : 3000;
	int differing = 0;
	// a hash of every scene drawn whole: the same before and after a change to the rasterizer means the
	// change left every pixel of every scene as it was (RASTER_HASH=1 prints it)
	uint64_t allPixels = 1469598103934665603ULL;
	partsLeftUncleared = 0;
	for (int i = 0; i < scenes; i++)
	{
		Random r(0x5eed0000u + (uint32_t)i);
		const Scene scene = makeScene(r);
		if (!sameInParts(scene, r, i, differing, allPixels))
			differing++;
	}
	if (getenv("RASTER_HASH") != nullptr)
		printf("  pixels of the %d scenes: %016llx (%d parts not cleared)\n", scenes, (unsigned long long)allPixels, partsLeftUncleared);
	CHECK_EQ(differing, 0);
}

// A part's clearing left out under a first draw that writes every pixel of it (coversOpaque): scenes with a
// background first, now and then: an opaque colour, an opaque picture 1:1 or made bigger (nearest or smoothed),
// covering the frame or nearly (edges on fractions of a pixel), with an alpha of 254 at a corner now and then;
// drawn whole and in parts, as above.
TEST(parts_under_an_opaque_draw_left_uncleared)
{
	const int scenes = getenv("RASTER_SCENES") ? atoi(getenv("RASTER_SCENES")) : 3000;
	int differing = 0;
	uint64_t allPixels = 1469598103934665603ULL;
	partsLeftUncleared = 0;
	for (int i = 0; i < scenes; i++)
	{
		Random r(0xc1ea0000u + (uint32_t)i);
		Scene scene = makeScene(r);
		if (r.chance(60))
		{
			Command bg;
			const float W = (float)scene.width, H = (float)scene.height;
			const float x0 = r.chance(60) ? 0.0f : r.coord(-1.5f, 1.5f), y0 = r.chance(60) ? 0.0f : r.coord(-1.5f, 1.5f);
			const float x1 = r.chance(60) ? W : r.coord(W - 1.5f, W + 1.5f), y1 = r.chance(60) ? H : r.coord(H - 1.5f, H + 1.5f);
			uint32_t c = 0xFF000000u | (uint32_t)r.range(0, 0xFFFFFF);
			if (r.chance(50))
				c = 0xFFFFFFFFu;
			bg.vertices = { { x0, y0, 0, 0, c }, { x0, y1, 0, 1, c }, { x1, y0, 1, 0, c }, { x1, y1, 1, 1, c } };
			if (r.chance(10))
				bg.vertices[r.range(0, 3)].color = (c & 0x00FFFFFF) | 0xFE000000u; // not opaque at a corner
			const int kind = r.range(0, 3);
			if (kind > 0)
			{
				std::unique_ptr<Texture> t = makeTexture(r);
				if (kind == 1)
				{
					// 1:1: the frame's size
					t->width = scene.width;
					t->height = scene.height;
					t->pixels.resize((size_t)t->width * t->height);
					for (uint32_t& p : t->pixels)
						p = r.color();
					bg.vertices = { { x0, y0, 0, 0, c }, { x0, y0 + H, 0, 1, c }, { x0 + W, y0, 1, 0, c }, { x0 + W, y0 + H, 1, 1, c } };
				}
				t->linear = kind == 3;
				bool opaque = r.chance(85);
				for (uint32_t& p : t->pixels)
					p = opaque ? (p | 0xFF000000u) : p;
				opaque = true;
				for (uint32_t p : t->pixels)
					opaque = opaque && (p >> 24) == 255;
				t->opaque = opaque;
				findClearRuns(*t);
				bg.texture = (int)scene.textures.size();
				scene.textures.push_back(std::move(t));
			}
			scene.commands.insert(scene.commands.begin(), bg);
		}
		if (!sameInParts(scene, r, i, differing, allPixels))
			differing++;
	}
	if (getenv("RASTER_HASH") != nullptr)
		printf("  %d parts not cleared in %d scenes\n", partsLeftUncleared, scenes);
	CHECK_EQ(differing, 0);
	CHECK(partsLeftUncleared > scenes); // the rule was used, many times
}

// Raster::drawLayers (draws drawn together, a row at a time, each pixel read and written once) against the
// same draws one after another: random runs of colours, pictures 1:1 (opaque, with alpha, with clear runs),
// big pictures scaled (the scaled-picture cache), tinted white, faded (white with an alpha: spanNeonFade's
// case) or by any colour, at whole or fractional positions, with draws it does not take (a gradient, a
// stencil test, desaturated, added) among them; into a part not cleared yet (it clears it) or after other
// draws. The pixels must be the same in the part, and none written outside it.
TEST(layers_drawn_together_leave_the_same_pixels)
{
	const int scenes = getenv("RASTER_SCENES") ? atoi(getenv("RASTER_SCENES")) : 3000;
	int differing = 0, outside = 0, together = 0;
	// a hash of the pixels drawn one by one: the same with ES_SOFT_NEON=0 means the NEON spans (spanNeonFade's
	// faded pictures among them) gave spanT's pixels (RASTER_HASH=1 prints it)
	uint64_t allPixels = 1469598103934665603ULL;
	for (int i = 0; i < scenes; i++)
	{
		Random r(0x1a7e0000u + (uint32_t)i);
		const int W = r.range(24, 320), H = r.range(8, 200);
		std::vector<std::unique_ptr<Texture>> textures;
		struct Draw { std::vector<SVertex> v; DrawState state; };
		std::vector<Draw> draws;
		auto quad = [](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c)
		{
			return std::vector<SVertex>{ { x0, y0, u0, v0, c }, { x0, y1, u0, v1, c }, { x1, y0, u1, v0, c }, { x1, y1, u1, v1, c } };
		};
		auto tint = [&r]()
		{
			switch (r.range(0, 3))
			{
			case 0: return 0xFFFFFFFFu;
			case 1: return ((uint32_t)(r.chance(20) ? (r.chance(50) ? 0 : 255) : r.range(1, 254)) << 24) | 0x00FFFFFFu; // faded
			default: return r.color();
			}
		};
		const int before = r.chance(30) ? r.range(1, 2) : 0; // draws before the run: the part already drawn
		const int count = before + r.range(2, 6) + r.range(0, 2);
		for (int k = 0; k < count; k++)
		{
			Draw d;
			const int kind = r.range(0, 9);
			if (kind <= 1)
			{
				// a colour over much of the frame
				const float x0 = r.coord(-10, W / 2.0f), y0 = r.coord(-10, H / 2.0f);
				d.v = quad(x0, y0, r.coord(W / 2.0f, W + 10.0f), r.coord(H / 2.0f, H + 10.0f), 0, 0, 0, 0, r.color());
			}
			else
			{
				textures.push_back(makeTexture(r));
				Texture* t = textures.back().get();
				d.state.texture = t;
				if (kind <= 5)
				{
					// 1:1
					const float x0 = r.coord(-(float)t->width / 2, (float)W), y0 = r.coord(-(float)t->height / 2, (float)H);
					d.v = quad(x0, y0, x0 + t->width, y0 + t->height, 0, 0, 1, 1, tint());
				}
				else
				{
					// big and scaled (a quarter of the frame or more): the scaled-picture cache when smoothed
					t->linear = r.chance(80);
					const float w = r.real(W * 0.5f, W * 1.3f), h = r.real(H * 0.5f, H * 1.3f);
					const float x0 = r.coord(-w / 4, W - w / 2), y0 = r.coord(-h / 4, H - h / 2);
					d.v = quad(x0, y0, x0 + w, y0 + h, 0, 0, 1, 1, tint());
				}
			}
			// now and then one drawLayers does not take
			switch (r.range(0, 19))
			{
			case 0: d.v[1].color = d.v[3].color = r.color(); break;  // a gradient
			case 1: d.state.stencil = StencilTest::Inside; break;
			case 2: d.state.saturation = 0.5f; break;
			case 3: d.state.srcBlend = 1; d.state.dstBlend = 1; break; // added
			default: break;
			}
			if (r.chance(15))
			{
				d.state.scissor = true;
				d.state.sx0 = r.range(0, W / 2); d.state.sy0 = r.range(0, H / 2);
				d.state.sx1 = r.range(W / 2, W); d.state.sy1 = r.range(H / 2, H);
			}
			draws.push_back(d);
		}

		// a part of a band, as the renderer draws one: the commands' scissors narrowed to it
		Raster owner, band;
		owner.resize(W, H);
		const int bandTop = r.chance(50) ? 0 : r.range(0, H - 1);
		band.attach(owner, bandTop, H);
		const int py0 = r.range(bandTop, H - 1), py1 = r.range(py0 + 1, H);
		const int px0 = r.chance(70) ? 0 : r.range(0, W - 1), px1 = r.chance(70) ? W : r.range(px0 + 1, W);
		const Bounds part = { px0, py0, px1, py1 };
		auto stateIn = [&](const DrawState& s0)
		{
			DrawState s = s0;
			if (s.scissor)
			{
				s.sx0 = std::max(s.sx0, px0); s.sy0 = std::max(s.sy0, py0);
				s.sx1 = std::min(s.sx1, px1); s.sy1 = std::min(s.sy1, py1);
			}
			else
			{
				s.scissor = true;
				s.sx0 = px0; s.sy0 = py0; s.sx1 = px1; s.sy1 = py1;
			}
			return s;
		};

		// one after another
		std::vector<uint32_t> expected;
		{
			std::fill(owner.pixels(), owner.pixels() + (size_t)W * H, 0xDEADBEEFu);
			band.clearBox(px0, py0, px1, py1, 0);
			for (const Draw& d : draws)
				band.draw(d.v.data(), 4, Topology::Strip, stateIn(d.state));
			expected.assign(owner.pixels(), owner.pixels() + (size_t)W * H);
			for (uint32_t p : expected)
				allPixels = (allPixels ^ p) * 1099511628211ULL;
			scaledFrame();
		}
		// the draws before one by one, then the run together (from the first that is), the rest one by one
		{
			std::fill(owner.pixels(), owner.pixels() + (size_t)W * H, 0xDEADBEEFu);
			bool cleared = false;
			size_t k = 0;
			if (before > 0)
			{
				band.clearBox(px0, py0, px1, py1, 0);
				cleared = true;
				for (; k < (size_t)before; k++)
					band.draw(draws[k].v.data(), 4, Topology::Strip, stateIn(draws[k].state));
			}
			while (k < draws.size())
			{
				LayerDraw run[8];
				int m = 0;
				for (size_t j = k; j < draws.size() && m < 8; j++, m++)
				{
					run[m].vertices = draws[j].v.data();
					run[m].count = 4;
					run[m].topology = Topology::Strip;
					run[m].state = stateIn(draws[j].state);
				}
				const int done = band.drawLayers(run, m, cleared ? nullptr : &part);
				if (done >= 2)
				{
					together++;
					cleared = true;
					k += (size_t)done;
					continue;
				}
				if (!cleared)
					band.clearBox(px0, py0, px1, py1, 0);
				cleared = true;
				band.draw(draws[k].v.data(), 4, Topology::Strip, stateIn(draws[k].state));
				k++;
			}
			if (!cleared)
				band.clearBox(px0, py0, px1, py1, 0);
			scaledFrame();
		}

		const uint32_t* got = owner.pixels();
		bool reported = false;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				const size_t at = (size_t)y * W + x;
				const bool in = x >= px0 && x < px1 && y >= py0 && y < py1;
				if (!in && got[at] != 0xDEADBEEFu)
					outside++;
				if (in && got[at] != expected[at] && !reported)
				{
					if (differing < 5)
						fprintf(stderr, "  scene %d (%dx%d, %zu draws): pixel %d,%d is %08x one by one, %08x together\n", i, W, H, draws.size(), x, y, expected[at], got[at]);
					differing++;
					reported = true;
				}
			}
	}
	if (getenv("RASTER_HASH") != nullptr)
		printf("  %d of %d scenes drew a run together; pixels: %016llx\n", together, scenes, (unsigned long long)allPixels);
	CHECK_EQ(differing, 0);
	CHECK_EQ(outside, 0);
	CHECK(together * 4 >= scenes); // the runs were drawn together in many scenes, not left to draw
}

// Big pictures scaled and kept the new ways (made bigger and not smoothed, on whole pixels; a constant colour
// of alpha 255 and the desaturation kept in their pixels) give the pixels they gave before: those sampled each
// time (a nearest one) or kept smoothed and tinted when drawn (SCALED_SMOOTHED). Drawn the old way, then the
// new twice (the second from what was kept), one by one and together with a colour under them (drawLayers),
// at whole or fractional positions, tinted any way, desaturated, blended any way, scissored.
TEST(scaled_pictures_kept_leave_the_pixels_sampled)
{
	const int scenes = getenv("RASTER_SCENES") ? atoi(getenv("RASTER_SCENES")) : 3000;
	int differing = 0, kept = 0;
	for (int i = 0; i < scenes; i++)
	{
		Random r(0x5ca1e000u + (uint32_t)i);
		const int W = r.range(24, 320), H = r.range(16, 200);
		std::unique_ptr<Texture> texture = makeTexture(r);
		Texture* t = texture.get();
		t->linear = r.chance(50);

		auto quad = [](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c)
		{
			return std::vector<SVertex>{ { x0, y0, u0, v0, c }, { x0, y1, u0, v1, c }, { x1, y0, u1, v0, c }, { x1, y1, u1, v1, c } };
		};
		const bool whole = r.chance(70);
		float w = r.real(W * 0.5f, W * 1.3f), h = r.real(H * 0.5f, H * 1.3f);
		float x0 = r.coord(-w / 4, W - w / 2), y0 = r.coord(-h / 4, H - h / 2);
		if (whole)
		{
			x0 = std::floor(x0); y0 = std::floor(y0);
			if (r.chance(60)) { w = std::floor(w); h = std::floor(h); }
		}
		float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
		switch (r.range(0, 4))
		{
		case 1: v0 = 1; v1 = 0; break;                                            // uploaded bottom up
		case 2: u0 = r.real(0.0f, 0.4f); u1 = r.real(0.6f, 1.0f); v0 = r.real(0.0f, 0.4f); v1 = r.real(0.6f, 1.0f); break; // a part
		case 3: u0 = r.real(-1, 1); u1 = r.real(-1, 3); v0 = r.real(-1, 1); v1 = r.real(-1, 3); break; // repeated
		default: break;
		}
		uint32_t c;
		switch (r.range(0, 4))
		{
		case 0: c = 0xFFFFFFFFu; break;
		case 1: c = 0xFF000000u | (uint32_t)r.range(0, 0xFFFFFF); break; // a colour of alpha 255 (kept in the pixels)
		case 2: c = 0xFF000000u | (uint32_t)r.range(0, 0x3F3F3F); break; // dark, as Carbon's grid background
		case 3: c = ((uint32_t)r.range(1, 254) << 24) | 0x00FFFFFFu; break; // faded
		default: c = r.color(); break;
		}
		std::vector<SVertex> picture = quad(x0, y0, x0 + w, y0 + h, u0, v0, u1, v1, c);
		if (r.chance(10))
			picture[1].color = picture[3].color = r.color(); // a gradient
		DrawState state;
		state.texture = t;
		if (r.chance(20))
			state.saturation = r.chance(50) ? 0.0f : r.real(0, 1);
		if (r.chance(15))
		{
			state.srcBlend = 1; state.dstBlend = 5;
		}
		if (r.chance(20))
		{
			state.scissor = true;
			state.sx0 = r.range(0, W / 2); state.sy0 = r.range(0, H / 2);
			state.sx1 = r.range(W / 2, W); state.sy1 = r.range(H / 2, H);
		}
		const std::vector<SVertex> under = quad(-5, -5, W + 5.0f, H + 5.0f, 0, 0, 0, 0, 0xFF000000u | (uint32_t)r.range(0, 0xFFFFFF));
		DrawState underState;

		Raster owner, band;
		owner.resize(W, H);
		band.attach(owner, 0, H);
		auto drawn = [&](int mode, bool together)
		{
			setScaledMode(mode);
			std::fill(owner.pixels(), owner.pixels() + (size_t)W * H, 0xDEADBEEFu);
			const Bounds part = { 0, 0, W, H };
			LayerDraw run[2];
			run[0].vertices = under.data(); run[0].count = 4; run[0].state = underState;
			run[1].vertices = picture.data(); run[1].count = 4; run[1].state = state;
			if (!together || band.drawLayers(run, 2, &part) < 2)
			{
				band.clearBox(0, 0, W, H, 0);
				band.draw(under.data(), 4, Topology::Strip, underState);
				band.draw(picture.data(), 4, Topology::Strip, state);
			}
			scaledFrame();
			return std::vector<uint32_t>(owner.pixels(), owner.pixels() + (size_t)W * H);
		};

		const std::vector<uint32_t> expected = drawn(SCALED_SMOOTHED, false);
		const size_t before = scaledBytes();
		const std::vector<uint32_t> got[4] = { drawn(SCALED_ALL, false), drawn(SCALED_ALL, false), drawn(SCALED_ALL, true), drawn(SCALED_ALL, true) };
		if (scaledBytes() > before)
			kept++;
		for (int g = 0; g < 4; g++)
			if (got[g] != expected)
			{
				size_t at = 0;
				while (got[g][at] == expected[at])
					at++;
				if (differing < (getenv("RASTER_SHOW") ? atoi(getenv("RASTER_SHOW")) : 5))
					fprintf(stderr, "  scene %d (%dx%d, %s, quad %g,%g %gx%g, uv %g,%g-%g,%g, sat %g, colour %08x, way %d): pixel %d,%d is %08x before, %08x now\n", i, W, H,
						t->linear ? "smoothed" : "nearest", x0, y0, w, h, u0, v0, u1, v1, state.saturation, c, g, (int)(at % W), (int)(at / W), expected[at], got[g][at]);
				differing++;
				break;
			}
	}
	setScaledMode(SCALED_ALL);
	if (getenv("RASTER_HASH") != nullptr)
		printf("  %d of %d scenes kept a scaled picture the new way\n", kept, scenes);
	CHECK_EQ(differing, 0);
	CHECK(kept * 5 >= scenes); // many scenes used the cache
}

// Draws written over by later ones are left out there (findHidden, drawCapture's parts as the renderer's):
// grid-like scenes (a background; tiles of a shadow, a stencil shape, a picture tested inside it or not, a
// label; anything else between them; scissors) drawn whole, one draw after another, and as the renderer
// draws them, in bands and strips, without what is written over. The pixels must be the same.
TEST(draws_written_over_are_left_out)
{
	const int scenes = getenv("RASTER_SCENES") ? atoi(getenv("RASTER_SCENES")) : 3000;
	int differing = 0, cut = 0;
	for (int i = 0; i < scenes; i++)
	{
		Random r(0x0cc10000u + (uint32_t)i);
		Capture capture;
		capture.width = r.range(40, 300);
		capture.height = r.range(30, 220);
		const int W = capture.width, H = capture.height;
		auto texture = [&](bool opaque)
		{
			std::unique_ptr<Texture> t = makeTexture(r);
			if (opaque)
			{
				for (uint32_t& p : t->pixels)
					p |= 0xFF000000u;
				t->opaque = true;
				findClearRuns(*t);
			}
			capture.textures.push_back(std::shared_ptr<Texture>(t.release()));
			return (int)capture.textures.size() - 1;
		};
		auto quad = [](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c)
		{
			return std::vector<SVertex>{ { x0, y0, u0, v0, c }, { x0, y1, u0, v1, c }, { x1, y0, u1, v0, c }, { x1, y1, u1, v1, c } };
		};
		auto coord = [&](float a, float b) { return r.chance(60) ? std::floor(r.real(a, b)) : r.real(a, b); };
		auto scissor = [&](CapturedCommand& c)
		{
			if (!r.chance(20))
				return;
			c.state.scissor = true;
			c.state.sx0 = r.range(-10, W / 2); c.state.sy0 = r.range(-10, H / 2);
			c.state.sx1 = r.range(W / 2, W + 10); c.state.sy1 = r.range(H / 2, H + 10);
		};
		auto colour = [&]()
		{
			switch (r.range(0, 4))
			{
			case 0: return 0xFFFFFFFFu;
			case 1: return 0xFF000000u | (uint32_t)r.range(0, 0xFFFFFF);
			case 2: return ((uint32_t)r.range(200, 254) << 24) | (uint32_t)r.range(0, 0xFFFFFF); // almost opaque
			default: return r.chance(50) ? 0xFFB0B0B0u : r.color();
			}
		};

		// a background
		{
			CapturedCommand c;
			if (r.chance(70))
			{
				c.texture = texture(r.chance(80));
				capture.textures[c.texture]->linear = r.chance(30);
			}
			c.vertices = quad(0, 0, (float)W, (float)H, 0, 0, 1, 1, r.chance(70) ? (0xFF000000u | (uint32_t)r.range(0, 0xFFFFFF)) : colour());
			scissor(c);
			capture.commands.push_back(c);
		}
		const int tiles = r.range(1, 8);
		for (int k = 0; k < tiles; k++)
		{
			const float w = r.real(10, W * 0.6f), h = r.real(10, H * 0.6f);
			const float x0 = coord(-w / 3, W - w / 2), y0 = coord(-h / 3, H - h / 2);
			// a shadow, bigger, any texture
			{
				CapturedCommand c;
				c.texture = texture(false);
				const float m = r.real(0, 12);
				c.vertices = quad(x0 - m, y0 - m, x0 + w + m, y0 + h + m, 0, 0, 1, 1, r.chance(60) ? 0xB0000000u : r.color());
				scissor(c);
				capture.commands.push_back(c);
			}
			// a stencil shape: a rounded rectangle (or any fan)
			const int stencil = r.range(0, 9);
			if (stencil < 7)
			{
				CapturedCommand c;
				c.stencil = true;
				c.topology = Topology::Fan;
				if (stencil < 5)
				{
					const float rad = r.real(0, std::min(w, h) / 2);
					const float cx = x0 + w / 2, cy = y0 + h / 2;
					c.vertices.push_back({ cx, cy, 0, 0, 0xFFFFFFFF });
					const int steps = r.range(1, 5);
					const float corners[4][2] = { { x0 + w - rad, y0 + rad }, { x0 + w - rad, y0 + h - rad }, { x0 + rad, y0 + h - rad }, { x0 + rad, y0 + rad } };
					for (int q = 0; q < 4; q++)
						for (int st = 0; st <= steps; st++)
						{
							const float a = 1.5707963f * (q - 1 + (float)st / steps);
							c.vertices.push_back({ corners[q][0] + rad * std::cos(a), corners[q][1] + rad * std::sin(a), 0, 0, 0xFFFFFFFF });
						}
					c.vertices.push_back(c.vertices[1]);
				}
				else
				{
					const int n = r.range(3, 9);
					for (int q = 0; q < n; q++)
						c.vertices.push_back({ r.coord(x0 - 10, x0 + w + 10), r.coord(y0 - 10, y0 + h + 10), 0, 0, 0xFFFFFFFF });
				}
				if (r.chance(15))
					scissor(c);
				capture.commands.push_back(c);
			}
			// something between them now and then
			if (r.chance(20))
			{
				CapturedCommand c;
				c.texture = r.chance(50) ? texture(r.chance(50)) : -1;
				const float bx = r.coord(x0 - 20, x0 + w), by = r.coord(y0 - 20, y0 + h);
				c.vertices = quad(bx, by, bx + r.real(2, 40), by + r.real(2, 30), 0, 0, 1, 1, colour());
				capture.commands.push_back(c);
			}
			// the tile's picture: opaque or not, tested inside the shape or not
			{
				CapturedCommand c;
				c.texture = r.chance(85) ? texture(r.chance(85)) : -1;
				if (c.texture >= 0)
					capture.textures[c.texture]->linear = r.chance(25);
				const float tw = c.texture >= 0 && r.chance(30) ? (float)capture.textures[c.texture]->width : w;
				c.vertices = quad(x0, y0, x0 + tw, y0 + h, 0, 0, 1, 1, colour());
				if (r.chance(15))
					c.vertices[1].color = c.vertices[3].color = r.color() | 0xFF000000u; // a gradient, still opaque
				if (stencil < 7 && r.chance(85))
					c.state.stencil = r.chance(90) ? StencilTest::Inside : StencilTest::Outside;
				if (r.chance(10))
					c.state.saturation = r.real(0, 1);
				if (r.chance(5))
				{
					c.state.srcBlend = 1; c.state.dstBlend = 1; // added: reads what is under it
				}
				scissor(c);
				capture.commands.push_back(c);
			}
			// a label
			if (r.chance(50))
			{
				CapturedCommand c;
				c.texture = texture(false);
				const float lx = r.coord(x0, x0 + w), ly = r.coord(y0 + h - 10, y0 + h + 10);
				c.vertices = quad(lx, ly, lx + r.real(5, 60), ly + r.real(3, 15), 0, 0, 1, 1, colour());
				capture.commands.push_back(c);
			}
		}
		// over it all now and then: a bar, a shade
		if (r.chance(30))
		{
			CapturedCommand c;
			c.vertices = quad(0, coord(0, (float)H), (float)W, coord(0, (float)H) + r.real(1, 40), 0, 0, 0, 0, colour());
			capture.commands.push_back(c);
		}

		// one draw after another over the whole frame
		Raster whole;
		whole.resize(W, H);
		whole.clear(0);
		for (const CapturedCommand& c : capture.commands)
		{
			DrawState state = c.state;
			if (c.stencil)
				whole.setStencil(c.vertices.data(), (int)c.vertices.size(), state);
			else
			{
				state.texture = c.texture >= 0 ? capture.textures[c.texture].get() : nullptr;
				whole.draw(c.vertices.data(), (int)c.vertices.size(), c.topology, state);
			}
		}
		whole.resetStencil();
		scaledFrame();

		// what is written over
		{
			std::vector<FrameDraw> draws(capture.commands.size());
			for (size_t k = 0; k < draws.size(); k++)
			{
				const CapturedCommand& c = capture.commands[k];
				draws[k].vertices = c.vertices.data();
				draws[k].count = (int)c.vertices.size();
				draws[k].topology = c.topology;
				draws[k].state = c.state;
				draws[k].state.texture = c.texture >= 0 ? capture.textures[c.texture].get() : nullptr;
				draws[k].stencil = c.stencil;
				draws[k].box = drawBounds(c.vertices.data(), (int)c.vertices.size(), c.topology, c.state, W, H);
			}
			Hidden hidden;
			findHidden(draws.data(), draws.size(), W, H, hidden);
			if (!hidden.rects.empty())
				cut++;
		}

		const int threads = r.range(1, 3), strip = r.chance(50) ? 0 : r.range(1, 40);
		Raster owner;
		drawCapture(capture, owner, threads, strip);
		const uint32_t* a = whole.pixels();
		const uint32_t* b = owner.pixels();
		for (int p = 0; p < W * H; p++)
			if (a[p] != b[p])
			{
				if (differing < 5)
					fprintf(stderr, "  scene %d (%dx%d, %zu commands, %d threads, strips %d): pixel %d,%d is %08x whole, %08x left out\n", i, W, H,
						capture.commands.size(), threads, strip, p % W, p / W, a[p], b[p]);
				differing++;
				break;
			}
	}
	if (getenv("RASTER_HASH") != nullptr)
		printf("  %d of %d scenes had draws written over\n", cut, scenes);
	CHECK_EQ(differing, 0);
	CHECK(cut * 2 >= scenes);
}

// What findHidden takes a picture tested inside a stencil shape to write: every pixel of it, by the shape's
// marks. Convex fans (rounded rectangles, polygons; from any of their vertices; on whole pixels, pixel middles
// and a hair off them, very flat and very steep edges), an opaque rectangle over them tested inside: the
// rectangles written over that a draw before them is given are all written by it.
TEST(stencil_shapes_written_over_are_marked)
{
	const int shapes = getenv("RASTER_SCENES") ? atoi(getenv("RASTER_SCENES")) * 8 : 24000;
	int wrong = 0, found = 0;
	for (int i = 0; i < shapes; i++)
	{
		Random r(0x57e40000u + (uint32_t)i);
		// now and then a wide frame, the shape far right (bigger coordinates: coarser floats)
		const bool wide = r.chance(30);
		const int W = wide ? r.range(1000, 2000) : r.range(20, 400), H = wide ? r.range(20, 160) : r.range(20, 300);
		// a coordinate: anywhere, on a whole pixel, a pixel's middle, or a hair off one
		auto at = [&](float a, float b)
		{
			const float f = r.real(a, b);
			switch (r.range(0, 5))
			{
			case 0: return std::floor(f);
			case 1: return std::floor(f) + 0.5f;
			case 2: return std::nextafter(std::floor(f) + 0.5f, r.chance(50) ? 1e9f : -1e9f);
			case 3: return std::floor(f) + 0.5f + (r.chance(50) ? 1 : -1) * r.real(0, 0.002f);
			default: return f;
			}
		};
		std::vector<SVertex> fan;
		const float x0 = wide ? at(W * 0.5f, W * 0.8f) : at(-30, W * 0.7f), y0 = at(-30, H * 0.7f);
		float w = wide ? r.real(2, W * 0.2f) : r.real(2, W * 0.8f);
		const float h = r.real(2, H * 0.8f);
		int start = -1; // the rounded rectangle's first vertex as ES has it: its left side's top
		if (r.chance(60))
		{
			// a rounded rectangle, as ES draws one: its corners' ends on its straight sides exactly (the sides'
			// ends as high, or as far left, as each other)
			const float rad = r.real(0, std::min(w, h) / 2);
			if (r.chance(40) && h - 2 * rad > 4)
			{
				// as wide as puts the line from the left side's top to the right side's bottom through a pixel's
				// middle (two triangles work it out differently: the pixel may be left out by both)
				const float cy = std::floor(r.real(y0 + rad + 2, y0 + h - rad)) + 0.5f;
				const float cx = std::floor(r.real(x0 + 2, x0 + w)) + 0.5f;
				w = (cx - x0) * (h - 2 * rad) / (cy - y0 - rad);
			}
			const int steps = r.range(1, 6);
			const float corners[4][2] = { { x0 + w - rad, y0 + rad }, { x0 + w - rad, y0 + h - rad }, { x0 + rad, y0 + h - rad }, { x0 + rad, y0 + rad } };
			for (int q = 0; q < 4; q++)
				for (int st = 0; st <= steps; st++)
				{
					const float a = 1.5707963f * (q - 1 + (float)st / steps);
					float cs = std::cos(a), sn = std::sin(a);
					if (st == 0 || st == steps)
					{
						cs = std::round(cs);
						sn = std::round(sn);
					}
					fan.push_back({ corners[q][0] + rad * cs, corners[q][1] + rad * sn, 0, 0, 0xFFFFFFFF });
				}
			if (r.chance(60))
				start = 3 * (steps + 1);
		}
		else
		{
			// a convex polygon: points on an ellipse, some moved to pixel middles
			const int n = r.range(3, 14);
			std::vector<float> angles;
			for (int k = 0; k < n; k++)
				angles.push_back(r.real(0, 6.2831853f));
			std::sort(angles.begin(), angles.end());
			for (float a : angles)
				fan.push_back({ x0 + w / 2 + w / 2 * std::cos(a), y0 + h / 2 + h / 2 * std::sin(a), 0, 0, 0xFFFFFFFF });
			for (SVertex& p : fan)
				if (r.chance(30))
				{
					p.x = std::floor(p.x) + 0.5f;
					p.y = std::floor(p.y) + 0.5f;
				}
		}
		// from any vertex (or ES's first); now and then the centre first, as a fan around it
		std::rotate(fan.begin(), fan.begin() + (start >= 0 ? start : r.range(0, (int)fan.size() - 1)), fan.end());
		if (r.chance(20))
		{
			fan.insert(fan.begin(), SVertex{ x0 + w / 2, y0 + h / 2, 0, 0, 0xFFFFFFFF });
			fan.push_back(fan[1]);
		}

		std::vector<SVertex> under = { { -1, -1, 0, 0, 0xFF000000u }, { -1, (float)H + 1, 0, 0, 0xFF000000u }, { (float)W + 1, -1, 0, 0, 0xFF000000u }, { (float)W + 1, (float)H + 1, 0, 0, 0xFF000000u } };
		const float qx0 = at(x0 - 10, x0 + w / 2), qy0 = at(y0 - 10, y0 + h / 2);
		const float qx1 = at(x0 + w / 2, x0 + w + 10), qy1 = at(y0 + h / 2, y0 + h + 10);
		std::vector<SVertex> cover = { { qx0, qy0, 0, 0, 0xFFFFFFFFu }, { qx0, qy1, 0, 0, 0xFFFFFFFFu }, { qx1, qy0, 0, 0, 0xFFFFFFFFu }, { qx1, qy1, 0, 0, 0xFFFFFFFFu } };
		DrawState shapeState, coverState, underState;
		coverState.stencil = StencilTest::Inside;
		if (r.chance(20))
		{
			shapeState.scissor = true;
			shapeState.sx0 = r.range(-5, W / 2); shapeState.sy0 = r.range(-5, H / 2);
			shapeState.sx1 = r.range(W / 2, W + 5); shapeState.sy1 = r.range(H / 2, H + 5);
		}

		FrameDraw draws[3];
		draws[0].vertices = under.data(); draws[0].count = 4; draws[0].state = underState;
		draws[1].vertices = fan.data(); draws[1].count = (int)fan.size(); draws[1].topology = Topology::Fan; draws[1].state = shapeState; draws[1].stencil = true;
		draws[2].vertices = cover.data(); draws[2].count = 4; draws[2].state = coverState;
		for (FrameDraw& d : draws)
			d.box = drawBounds(d.vertices, d.count, d.topology, d.state, W, H);
		Hidden hidden;
		findHidden(draws, 3, W, H, hidden);

		Raster raster;
		raster.resize(W, H);
		raster.clear(0);
		raster.setStencil(fan.data(), (int)fan.size(), shapeState);
		raster.draw(cover.data(), 4, Topology::Strip, coverState);
		raster.resetStencil();
		const uint32_t* px = raster.pixels();
		bool bad = false;
		for (int h0 = 0; h0 < hidden.count(0) && !bad; h0++)
		{
			const Bounds& b = hidden.of(0)[h0];
			found++;
			for (int y = b.y0; y < b.y1 && !bad; y++)
				for (int x = b.x0; x < b.x1; x++)
					if (x < 0 || y < 0 || x >= W || y >= H || px[(size_t)y * W + x] != 0xFFFFFFFFu)
					{
						if (wrong < 5)
							fprintf(stderr, "  shape %d (%zu vertices, %dx%d): pixel %d,%d in %d,%d-%d,%d not written by the picture\n", i, fan.size(), W, H, x, y, b.x0, b.y0, b.x1, b.y1);
						wrong++;
						bad = true;
						break;
					}
		}
	}
	if (getenv("RASTER_HASH") != nullptr)
		printf("  %d rectangles written over in %d shapes\n", found, shapes);
	CHECK_EQ(wrong, 0);
	CHECK(found >= shapes / 2);
}
