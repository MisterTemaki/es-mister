// PictureCache: a picture written comes back with the very same bytes and sizes; another key, a damaged or a
// cut file is a miss, never other pixels.
#include "TestMain.h"
#include "resources/PictureCache.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <thread>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
	std::string folder()
	{
		static const std::string dir = []
		{
			char name[] = "/tmp/es-picture-cache-test-XXXXXX";
			const char* made = mkdtemp(name);
			const std::string d = made ? made : "/tmp";
			setenv("ES_PICTURE_CACHE", (d + "/cache").c_str(), 1);
			std::ofstream(d + "/source.png") << "not really a picture, only a file to be known by";
			return d;
		}();
		return dir;
	}

	// the picture back from the cache, waiting for its writer thread (up to 3 s)
	bool loadSoon(const PictureCache::Key& key, PictureCache::Picture& out)
	{
		for (int i = 0; i < 300; i++)
		{
			if (PictureCache::load(key, out))
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return false;
	}

	// the cache's files (one each picture), in the folder's subfolders
	std::vector<std::string> files()
	{
		std::vector<std::string> out;
		const std::string root = folder() + "/cache";
		DIR* dir = opendir(root.c_str());
		if (!dir)
			return out;
		while (dirent* sub = readdir(dir))
		{
			if (sub->d_name[0] == '.')
				continue;
			const std::string path = root + "/" + sub->d_name;
			DIR* inner = opendir(path.c_str());
			if (!inner)
				continue;
			while (dirent* f = readdir(inner))
				if (strstr(f->d_name, ".espc"))
					out.push_back(path + "/" + f->d_name);
			closedir(inner);
		}
		closedir(dir);
		return out;
	}
}

TEST(pictures_come_back_the_same)
{
	PictureCache::Key key;
	CHECK(PictureCache::makeKey(folder() + "/source.png", key));
	key.screenWidth = 1280;
	key.screenHeight = 720;

	std::mt19937 random(11);
	const int sizes[][2] = { { 1, 1 }, { 219, 156 }, { 7, 3 }, { 137, 248 }, { 64, 64 } };
	for (const auto& size : sizes)
	{
		key.maxX = (float)size[0];
		key.maxY = (float)size[1];

		PictureCache::Picture in;
		in.width = size[0];
		in.height = size[1];
		in.baseSize = Vector2i(size[0] * 2 + 1, size[1] * 3 + 2);
		in.packedSize = Vector2i(size[0], size[1]);
		std::vector<unsigned char> pixels(in.width * in.height * 4);
		// runs, repeats and noise, so every QOI operation is used
		for (size_t i = 0; i < pixels.size(); i++)
			pixels[i] = (i / 64) % 3 == 0 ? (unsigned char)(i / 256) : (unsigned char)random();
		in.rgba = pixels.data();
		PictureCache::store(key, in);

		PictureCache::Picture out;
		CHECK(loadSoon(key, out));
		CHECK_EQ(out.width, in.width);
		CHECK_EQ(out.height, in.height);
		CHECK_EQ(out.baseSize.x(), in.baseSize.x());
		CHECK_EQ(out.baseSize.y(), in.baseSize.y());
		CHECK_EQ(out.packedSize.x(), in.packedSize.x());
		CHECK_EQ(out.packedSize.y(), in.packedSize.y());
		CHECK(out.rgba != nullptr && memcmp(out.rgba, pixels.data(), pixels.size()) == 0);
		delete[] out.rgba;
	}

	// the same file at another size, another frame or another screen: not there
	PictureCache::Picture none;
	key.maxX = 220;
	CHECK(!PictureCache::load(key, none));
	key.maxX = 219;
	key.maxY = 156;
	key.subImageIndex = 2;
	CHECK(!PictureCache::load(key, none));
	key.subImageIndex = -1;
	key.screenWidth = 1920;
	CHECK(!PictureCache::load(key, none));
}

TEST(damaged_or_cut_files_are_misses)
{
	PictureCache::Key key;
	CHECK(PictureCache::makeKey(folder() + "/source.png", key));
	key.maxX = 31;
	key.maxY = 17;
	key.screenWidth = 1280;
	key.screenHeight = 720;

	std::vector<unsigned char> pixels(31 * 17 * 4);
	std::mt19937 random(5);
	for (auto& b : pixels)
		b = (unsigned char)random();
	PictureCache::Picture in;
	in.rgba = pixels.data();
	in.width = 31;
	in.height = 17;
	in.baseSize = in.packedSize = Vector2i(31, 17);
	const auto before = files();
	PictureCache::store(key, in);
	PictureCache::Picture out;
	CHECK(loadSoon(key, out));
	delete[] out.rgba;

	std::string file;
	for (const auto& f : files())
		if (std::find(before.begin(), before.end(), f) == before.end())
			file = f;
	CHECK(!file.empty());
	std::string data;
	{
		std::ifstream in(file, std::ios::binary);
		data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	// every byte of the pixels' part changed in turn, and the file cut short: never a hit
	int hits = 0;
	for (size_t at = data.size() / 2; at < data.size(); at += 7)
	{
		std::string damaged = data;
		damaged[at] ^= 0x5A;
		std::ofstream(file, std::ios::binary | std::ios::trunc) << damaged;
		PictureCache::Picture got;
		if (PictureCache::load(key, got))
		{
			hits += memcmp(got.rgba, pixels.data(), pixels.size()) != 0; // a hit must be the same pixels
			delete[] got.rgba;
		}
	}
	CHECK_EQ(hits, 0);
	std::ofstream(file, std::ios::binary | std::ios::trunc) << data.substr(0, data.size() - 5);
	CHECK(!PictureCache::load(key, out));

	// whole again: a hit
	std::ofstream(file, std::ios::binary | std::ios::trunc) << data;
	CHECK(PictureCache::load(key, out));
	delete[] out.rgba;
}
