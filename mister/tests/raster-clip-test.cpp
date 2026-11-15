// The software renderer's invariant that drawing only what changed rests on (Renderer_Soft's findParts,
// bands and strips): a draw clipped to a part of the frame leaves, inside that part, the same pixels as
// the draw unclipped. Random scenes (pictures 1:1, scaled, stretched big enough for the scaled-picture
// cache, repeated, tinted, desaturated; text-like strips; triangles, fans, lines; the stencil; every
// blend; scissors) are drawn whole, then again a part at a time into band rasters, as executePart does
// (each part cleared, every command clipped to it), and compared pixel by pixel. With the sanitizers it
// also checks that no draw touches memory outside the frame or the textures, whatever the coordinates.
#include "TestMain.h"
#include "renderers/soft/SoftRaster.h"

#include <algorithm>
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
		const int kind = r.range(0, 3); // opaque, alpha, clear runs (an overlay), font-like (white, alpha)
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
		const uint32_t a = r.color(), b = r.color();
		for (size_t i = 0; i < v.size(); i++)
		{
			switch (mode)
			{
			case 0: v[i].color = a; break;
			case 1: v[i].color = 0xFFFFFFFF; break;
			case 2: v[i].color = (i % 2 == 0) ? a : b; break; // v0, v2 top; v1, v3 bottom
			default: v[i].color = r.color(); break;
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

	// the scene drawn a part at a time, as Renderer_Soft's executePart: the part cleared, then every command
	// with its scissor narrowed to the part
	void drawPart(Raster& band, const Scene& scene, int x0, int y0, int x1, int y1)
	{
		band.clearBox(x0, y0, x1, y1, 0);
		for (const Command& c : scene.commands)
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
			run(band, scene, c, state);
		}
	}
}

TEST(clipped_draws_leave_the_same_pixels)
{
	const int scenes = getenv("RASTER_SCENES") ? atoi(getenv("RASTER_SCENES")) : 3000;
	int differing = 0;
	for (int i = 0; i < scenes; i++)
	{
		Random r(0x5eed0000u + (uint32_t)i);
		const Scene scene = makeScene(r);

		Raster whole;
		whole.resize(scene.width, scene.height);
		whole.clear(0);
		for (const Command& c : scene.commands)
			run(whole, scene, c, c.state);

		// two bands with a random line between them; each split into random parts (strips of rows and
		// columns), drawn in a random order
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
		for (int y = 0; y < scene.height; y++)
			for (int x = 0; x < scene.width; x++)
				if (a[(size_t)y * scene.width + x] != b[(size_t)y * scene.width + x])
				{
					if (differing < 5)
						fprintf(stderr, "  scene %d (%dx%d, %zu commands): pixel %d,%d is %08x drawn whole, %08x in parts\n", i, scene.width, scene.height,
							scene.commands.size(), x, y, a[(size_t)y * scene.width + x], b[(size_t)y * scene.width + x]);
					differing++;
					y = scene.height;
					break;
				}
		scaledFrame();
	}
	CHECK_EQ(differing, 0);
}
