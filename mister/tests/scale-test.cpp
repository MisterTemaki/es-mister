// The frames enlarged on their way to the screen (SoftScale): each kind's pixels against a reference
// written apart from it, and the invariant the renderer rests on — the parts that changed, presented
// alone (in any order, in bands of the frame's rows at rowStep), leave the screen as the whole frame
// presented again would. Run on the ARM too (qemu or the MiSTer), where the NEON paths are the ones taken.
#include "TestMain.h"
#include "renderers/soft/SoftScale.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace Renderer::Soft;

namespace
{
	const uint32_t UNTOUCHED = 0x12345678;

	void copyRow(uint8_t* to, const uint8_t* from, size_t bytes) { memcpy(to, from, bytes); }

	// each byte (x + y + 1) / 2
	uint32_t mean(uint32_t a, uint32_t b)
	{
		uint32_t out = 0;
		for (int shift = 0; shift < 32; shift += 8)
			out |= ((((a >> shift) & 0xFF) + ((b >> shift) & 0xFF) + 1) >> 1) << shift;
		return out;
	}

	struct Screen
	{
		int width, height;
		size_t pitch; // a few pixels more than a row, as a framebuffer's line_length may be
		std::vector<uint32_t> pixels;
		Screen(int w, int h) : width(w), height(h), pitch((size_t)(w + 3) * 4), pixels((w + 3) * (size_t)h, UNTOUCHED) { }
		uint8_t* bytes() { return (uint8_t*)pixels.data(); }
		uint32_t at(int x, int y) const { return pixels[(size_t)y * (width + 3) + x]; }
	};

	std::vector<uint32_t> randomFrame(int w, int h, std::mt19937& random)
	{
		std::vector<uint32_t> frame((size_t)w * h);
		for (uint32_t& p : frame)
			p = random();
		return frame;
	}

	// what the screen's pixel (x, y) shows of the frame, worked out from the definition
	uint32_t expectedThreeHalves(const std::vector<uint32_t>& frame, int w, int x, int y)
	{
		const int gx = x / 3, px = x % 3, gy = y / 3, py = y % 3;
		auto at = [&](int fx, int fy) { return frame[(size_t)fy * w + fx]; };
		const uint32_t a0 = at(2 * gx, 2 * gy), a1 = at(2 * gx + 1, 2 * gy);
		const uint32_t b0 = at(2 * gx, 2 * gy + 1), b1 = at(2 * gx + 1, 2 * gy + 1);
		auto pick = [&](uint32_t left, uint32_t right, uint32_t middle) { return px == 0 ? left : px == 2 ? right : middle; };
		if (py == 0)
			return pick(a0, a1, mean(a0, a1));
		if (py == 2)
			return pick(b0, b1, mean(b0, b1));
		const uint32_t left = mean(a0, b0), right = mean(a1, b1);
		return pick(left, right, mean(left, right));
	}

	// the frame changed in a few random parts, which are presented alone; then compared with the whole of
	// it presented on a screen of its own
	void checkParts(const Scale& scale, int w, int h, int sw, int sh, std::mt19937& random)
	{
		std::vector<uint32_t> frame = randomFrame(w, h, random);
		Screen screen(sw, sh);
		scale.present(frame.data(), 0, 0, w, h, screen.bytes(), screen.pitch, copyRow);

		for (int round = 0; round < 20; round++)
		{
			// the frame cut in two bands at a row on the output's step, as the renderer's placeBands does
			const int step = scale.rowStep();
			int split = (int)(random() % (unsigned)h);
			split -= split % step;
			struct Part { int x0, y0, x1, y1; };
			std::vector<Part> parts;
			const int count = 1 + (int)(random() % 6);
			for (int i = 0; i < count; i++)
			{
				const int x0 = (int)(random() % (unsigned)w), y0 = (int)(random() % (unsigned)h);
				const int x1 = std::min(w, x0 + 1 + (int)(random() % 40)), y1 = std::min(h, y0 + 1 + (int)(random() % 40));
				for (int y = y0; y < y1; y++)
					for (int x = x0; x < x1; x++)
						frame[(size_t)y * w + x] = random();
				// each band presents its rows of the part
				if (y0 < split)
					parts.push_back({ x0, y0, x1, std::min(y1, split) });
				if (y1 > split)
					parts.push_back({ x0, std::max(y0, split), x1, y1 });
			}
			std::shuffle(parts.begin(), parts.end(), random);
			for (const Part& p : parts)
				scale.present(frame.data(), p.x0, p.y0, p.x1, p.y1, screen.bytes(), screen.pitch, copyRow);

			Screen whole(sw, sh);
			scale.present(frame.data(), 0, 0, w, h, whole.bytes(), whole.pitch, copyRow);
			int wrong = 0;
			for (int y = 0; y < sh; y++)
				for (int x = 0; x < sw; x++)
					wrong += screen.at(x, y) != whole.at(x, y);
			CHECK_EQ(wrong, 0);
		}
	}
}

TEST(scale_kinds)
{
	Scale scale;
	scale.setup(1280, 720, 1920, 1080);
	CHECK(scale.kind() == Scale::Kind::ThreeHalves);
	CHECK_EQ(scale.rowStep(), 2);
	scale.setup(1280, 720, 1280, 720);
	CHECK(scale.kind() == Scale::Kind::Copy);
	CHECK_EQ(scale.rowStep(), 1);
	scale.setup(960, 540, 1920, 1080);
	CHECK(scale.kind() == Scale::Kind::Nearest);
	CHECK_EQ(scale.width(), 1920);
	CHECK_EQ(scale.height(), 1080);
	// a 16:9 frame on a 5:4 screen: as wide as it, in the middle
	scale.setup(1280, 720, 1280, 1024);
	CHECK(scale.kind() == Scale::Kind::Nearest);
	CHECK_EQ(scale.width(), 1280);
	CHECK_EQ(scale.height(), 720);
	CHECK_EQ(scale.top(), 152);
	CHECK_EQ(scale.left(), 0);
	// and on a wider screen: as high as it
	scale.setup(1280, 720, 2560, 1080);
	CHECK_EQ(scale.width(), 1920);
	CHECK_EQ(scale.left(), 320);
}

TEST(scale_three_halves_pixels)
{
	std::mt19937 random(7);
	// widths with and without a tail after the 8-pixel steps
	const int sizes[][2] = { { 1280, 720 }, { 18, 12 }, { 22, 6 }, { 2, 2 }, { 46, 10 } };
	for (const auto& size : sizes)
	{
		const int w = size[0], h = size[1], sw = w * 3 / 2, sh = h * 3 / 2;
		Scale scale;
		scale.setup(w, h, sw, sh);
		CHECK(scale.kind() == Scale::Kind::ThreeHalves);
		const std::vector<uint32_t> frame = randomFrame(w, h, random);
		Screen screen(sw, sh);
		CHECK_EQ(scale.present(frame.data(), 0, 0, w, h, screen.bytes(), screen.pitch, copyRow), (int64_t)sw * sh);
		int wrong = 0;
		for (int y = 0; y < sh; y++)
			for (int x = 0; x < sw; x++)
				wrong += screen.at(x, y) != expectedThreeHalves(frame, w, x, y);
		CHECK_EQ(wrong, 0);
		// nothing past a row's end (the pitch's extra pixels)
		int past = 0;
		for (int y = 0; y < sh; y++)
			for (int x = sw; x < sw + 3; x++)
				past += screen.pixels[(size_t)y * (sw + 3) + x] != UNTOUCHED;
		CHECK_EQ(past, 0);
	}
}

TEST(scale_three_halves_parts)
{
	std::mt19937 random(11);
	Scale scale;
	scale.setup(1280, 720, 1920, 1080);
	checkParts(scale, 1280, 720, 1920, 1080, random);
	scale.setup(62, 40, 93, 60);
	checkParts(scale, 62, 40, 93, 60, random);
}

TEST(scale_nearest)
{
	std::mt19937 random(13);
	const int sizes[][4] = { { 960, 540, 1920, 1080 }, { 1280, 720, 1280, 1024 }, { 1280, 720, 2560, 1080 }, { 100, 60, 37, 50 }, { 64, 48, 200, 90 } };
	for (const auto& size : sizes)
	{
		const int w = size[0], h = size[1], sw = size[2], sh = size[3];
		Scale scale;
		scale.setup(w, h, sw, sh);
		CHECK(scale.kind() == Scale::Kind::Nearest);
		const std::vector<uint32_t> frame = randomFrame(w, h, random);
		Screen screen(sw, sh);
		scale.clearAround(screen.bytes(), screen.pitch, copyRow);
		scale.present(frame.data(), 0, 0, w, h, screen.bytes(), screen.pitch, copyRow);
		int wrong = 0, border = 0;
		for (int y = 0; y < sh; y++)
			for (int x = 0; x < sw; x++)
			{
				const int px = x - scale.left(), py = y - scale.top();
				if (px < 0 || py < 0 || px >= scale.width() || py >= scale.height())
				{
					border += screen.at(x, y) != 0;
					continue;
				}
				// the frame's pixel under the middle of the screen's
				const int fx = std::min(w - 1, (int)((2LL * px + 1) * w / (2LL * scale.width())));
				const int fy = std::min(h - 1, (int)((2LL * py + 1) * h / (2LL * scale.height())));
				wrong += screen.at(x, y) != frame[(size_t)fy * w + fx];
			}
		CHECK_EQ(wrong, 0);
		CHECK_EQ(border, 0);
		checkParts(scale, w, h, sw, sh, random);
	}
}

TEST(scale_copy)
{
	std::mt19937 random(17);
	Scale scale;
	scale.setup(50, 30, 50, 30);
	const std::vector<uint32_t> frame = randomFrame(50, 30, random);
	Screen screen(50, 30);
	scale.present(frame.data(), 0, 0, 50, 30, screen.bytes(), screen.pitch, copyRow);
	int wrong = 0;
	for (int y = 0; y < 30; y++)
		for (int x = 0; x < 50; x++)
			wrong += screen.at(x, y) != frame[(size_t)y * 50 + x];
	CHECK_EQ(wrong, 0);
	checkParts(scale, 50, 30, 50, 30, random);
}
