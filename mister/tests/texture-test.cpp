// Soft::convertRgba: the texels of an upload (NEON on the MiSTer), the same as one at a time, and whether
// every alpha was 255.
#include "TestMain.h"
#include "renderers/soft/SoftRaster.h"

#include <random>

TEST(convert_rgba_as_one_texel_at_a_time)
{
	std::mt19937 random(7);
	for (unsigned int count = 0; count < 100; count++)
		for (int kind = 0; kind < 4; kind++)
		{
			// kind 0: random alphas; 1: all 255; 2 and 3: all 255 but one (early, late)
			std::vector<uint8_t> rgba(count * 4);
			for (auto& b : rgba)
				b = (uint8_t)random();
			if (kind > 0)
				for (unsigned int i = 0; i < count; i++)
					rgba[i * 4 + 3] = 255;
			if (kind == 2 && count > 0)
				rgba[3] = 254;
			if (kind == 3 && count > 0)
				rgba[(count - 1) * 4 + 3] = 0;

			std::vector<uint32_t> expected(count), got(count + 1, 0xDEADBEEF);
			bool expectedOpaque = true;
			for (unsigned int i = 0; i < count; i++)
			{
				const uint8_t* p = rgba.data() + i * 4;
				expected[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
				expectedOpaque = expectedOpaque && p[3] == 255;
			}
			const bool opaque = Renderer::Soft::convertRgba(rgba.data(), got.data(), count);
			CHECK_EQ(opaque, expectedOpaque);
			CHECK(std::equal(expected.begin(), expected.end(), got.begin()));
			CHECK_EQ(got[count], 0xDEADBEEFu); // nothing written past the end
		}
}
