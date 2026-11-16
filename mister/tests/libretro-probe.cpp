// What the "Libretro-thumbnails" scraper picks for each game of a folder, without ES and without writing anything
// beside the games: es-mister-libretro-probe SYSTEM FOLDER CACHE TABLES [MAX_CRC [DOWNLOADS OUTDIR]]
//   SYSTEM  ES's system name (snes, megadrive...)        FOLDER  the games (read only)
//   CACHE   where the art lists are kept                  TABLES  resources/scrapers/libretro
//   MAX_CRC how many games' ROMs are read for their CRC32 (default all; the rest by name)
//   DOWNLOADS, OUTDIR: that many box arts downloaded there, to look at
#include "HttpReq.h"
#include "scrapers/LibretroArt.h"
#include "utils/FileSystemUtil.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <thread>
#include <vector>

using namespace LibretroArt;

static double secondsSince(std::chrono::steady_clock::time_point t)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		fprintf(stderr, "usage: %s SYSTEM FOLDER CACHE TABLES [MAX_CRC [DOWNLOADS OUTDIR]]\n", argv[0]);
		return 2;
	}
	const std::string folder = argv[2];
	endpoints().cache = argv[3];
	endpoints().tables = argv[4];
	const long maxCrc = argc > 5 ? atol(argv[5]) : -1;
	const int downloads = argc > 7 ? atoi(argv[6]) : 0;
	const std::string out = argc > 7 ? argv[7] : "";

	std::vector<std::string> files;
	if (DIR* d = opendir(folder.c_str()))
	{
		while (dirent* e = readdir(d))
			if (e->d_name[0] != '.' && e->d_type == DT_REG)
				files.push_back(e->d_name);
		closedir(d);
	}
	std::sort(files.begin(), files.end());

	const System* system = findSystem({ argv[1] }, files.empty() ? "" : Utils::FileSystem::getExtension(files[0]));
	if (system == nullptr)
	{
		fprintf(stderr, "%s: not a Libretro-thumbnails system\n", argv[1]);
		return 1;
	}

	auto start = std::chrono::steady_clock::now();
	Source* source = Source::get(*system);
	while (!source->poll())
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	const Art& art = source->art();
	printf("# %s: loaded in %.1f s: %s, %zu box arts, %zu screenshots, %zu titles, %zu CRCs, %zu database covers\n",
		system->repo.c_str(), secondsSince(start), art.indexed ? "listed" : "NOT listed", art.folders[BOXART].size(),
		art.folders[SNAP].size(), art.folders[TITLE].size(), art.byCrc.size(), art.db.size());

	int byCrc = 0, box = 0, db = 0, none = 0, read = 0, fetched = 0;
	double crcTime = 0, chooseTime = 0;
	for (const std::string& file : files)
	{
		RomCrc crc;
		if (system->dat && (maxCrc < 0 || read < maxCrc))
		{
			auto t = std::chrono::steady_clock::now();
			crc = romCrc(folder + "/" + file);
			crcTime += secondsSince(t);
			read++;
		}
		auto t = std::chrono::steady_clock::now();
		const Choice c = choose(art, Utils::FileSystem::getStem(file), crc, "");
		chooseTime += secondsSince(t);

		const char* how = !c.noIntro.empty() ? "crc" : !c.names[BOXART].empty() ? "name" : !c.db.empty() ? "db" : !c.guesses.empty() ? "guess" : "-";
		byCrc += !c.noIntro.empty();
		box += !c.names[BOXART].empty();
		db += c.names[BOXART].empty() && !c.db.empty();
		none += c.names[BOXART].empty() && c.db.empty() && c.guesses.empty();
		printf("%-5s %s -> %s\n", how, file.c_str(), !c.names[BOXART].empty() ? c.names[BOXART].c_str() : !c.db.empty() ? c.db[0].c_str() : "");

		if (fetched < downloads && (!c.names[BOXART].empty() || !c.db.empty()))
		{
			const std::string url = !c.names[BOXART].empty() ? artUrl(art.repo, BOXART, c.names[BOXART]) : c.db[0];
			HttpReq req(url, out + "/" + Utils::FileSystem::getStem(file) + "-box.img");
			req.wait();
			printf("      downloaded %s: %d\n", url.c_str(), (int)req.status());
			fetched++;
		}
	}
	printf("# %zu games: %d known by CRC32 (%d read, %.1f s), %d with a box art, %d from a database, %d without; choosing %.2f s\n",
		files.size(), byCrc, read, crcTime, box, db, none, chooseTime);
	return 0;
}
