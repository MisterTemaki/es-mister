// The "Libretro-thumbnails" scraper without the network: names matched to art names (the MiSTer packs' "(JP)" and
// ROM hacks' "- Fastrom" included), ROMs' CRC32 as No-Intro has them, the DAT, GitHub's trees and the database
// tables read, the art chosen, and a system's art loaded from the cache (or, with no network, worked out from names).
#include "TestMain.h"
#include "scrapers/LibretroArt.h"
#include "scrapers/LibretroArtMatch.h"
#include "utils/FileSystemUtil.h"
#include "utils/ZipFile.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <chrono>
#include <unistd.h>

using namespace LibretroArt;

namespace
{
	uint32_t crcOf(const std::vector<unsigned char>& data, size_t from = 0)
	{
		return Utils::Zip::ZipFile::computeCRC(0, data.data() + from, data.size() - from);
	}

	std::vector<unsigned char> bytes(size_t n, unsigned seed)
	{
		std::vector<unsigned char> v(n);
		for (size_t i = 0; i < n; i++)
			v[i] = (unsigned char)((i * 131 + seed * 7 + (i >> 8)) & 0xff);
		return v;
	}

	void write(const std::string& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}

	std::string tempFolder()
	{
		char folder[] = "/tmp/es-libretro-test-XXXXXX";
		return mkdtemp(folder);
	}

	const char* SYSTEMS =
		"# test\n"
		"snes,sfc,snes-msu1\tNintendo - Super Nintendo Entertainment System\t1\tNintendo Super Nintendo Entertainment System\n"
		"sgb/.gbc,gbc\tNintendo - Game Boy Color\t1\t-\n"
		"sgb,gb\tNintendo - Game Boy\t1\t-\n"
		"psx\tSony - PlayStation\t0\t-\n";
}

TEST(region_codes_of_mister_packs_are_regions)
{
	CHECK_EQ(LibretroArtMatch::Parse("Ace o Nerae! (JP)").region, std::string("Japan"));
	CHECK_EQ(LibretroArtMatch::Parse("Aero the Acro-Bat (EU)").region, std::string("Europe"));
	CHECK_EQ(LibretroArtMatch::Parse("Bonkers (US)").region, std::string("USA"));
	CHECK_EQ(LibretroArtMatch::Parse("Breath of Fire (US, EU)").region, std::string("USA"));
	CHECK_EQ(LibretroArtMatch::Parse("World Championship Soccer (JP - US)").region, std::string("Japan"));
	CHECK_EQ(LibretroArtMatch::Parse("Super Locomotive (WR)").region, std::string("World"));
	CHECK_EQ(LibretroArtMatch::Parse("2020 Super Baseball (Japan)").region, std::string("Japan"));
	CHECK_EQ(LibretroArtMatch::Parse("Bonkers (U)").region, std::string("USA"));
	CHECK_EQ(LibretroArtMatch::Parse("Cool World (ES) (En)").region, std::string(""));
}

TEST(rom_hack_suffixes_leave_the_game_title)
{
	CHECK_EQ(LibretroArtMatch::Parse("Megaman X3 - Fastrom v2.1").title, std::string("Megaman X3"));
	CHECK_EQ(LibretroArtMatch::Parse("Gradius III - SA-1").title, std::string("Gradius III"));
	CHECK_EQ(LibretroArtMatch::Parse("Final Fight 3 Optimized Fastrom").title, std::string("Final Fight 3"));
	CHECK_EQ(LibretroArtMatch::Parse("Adventures of Kid Kleets, The (U) - Fastrom - Camera Fix").title, std::string("Adventures of Kid Kleets, The"));
	// a title's own words stay
	CHECK_EQ(LibretroArtMatch::Parse("Mega Man X (USA)").title, std::string("Mega Man X"));
	CHECK_EQ(LibretroArtMatch::Parse("Street Fighter II - The World Warrior (USA)").title, std::string("Street Fighter II - The World Warrior"));
	CHECK_EQ(LibretroArtMatch::Parse("Fix-It Felix (World)").title, std::string("Fix-It Felix"));
}

TEST(file_names_find_their_art)
{
	Names box;
	for (const char* n : { "2020 Super Baseball (Japan)", "2020 Super Baseball (USA)", "Ace o Nerae! (Japan)",
		"Aero the Acro-Bat (Europe)", "Aero the Acro-Bat (USA)", "Mega Man X3 (USA)", "Mega Man X3 (Japan)",
		"Final Fantasy III (USA)", "Final Fantasy II (USA)", "Contra III - The Alien Wars (USA)", "Gradius III (USA)",
		"Gradius III (Japan)", "Legend of Zelda, The - A Link to the Past (USA)", "Super Mario World (USA)" })
		box.add(n);

	CHECK_EQ(box.resolve("2020 Super Baseball (JP)", ""), std::string("2020 Super Baseball (Japan)"));
	CHECK_EQ(box.resolve("2020 Super Baseball (US)", ""), std::string("2020 Super Baseball (USA)"));
	CHECK_EQ(box.resolve("2020 Super Baseball", ""), std::string("2020 Super Baseball (USA)")); // USA first
	CHECK_EQ(box.resolve("2020 Super Baseball", "jp"), std::string("2020 Super Baseball (Japan)")); // the setting
	CHECK_EQ(box.resolve("Aero the Acro-Bat (EU)", "us"), std::string("Aero the Acro-Bat (Europe)")); // the file's own
	CHECK_EQ(box.resolve("Megaman X3 - Fastrom v2.1", ""), std::string("Mega Man X3 (USA)"));
	CHECK_EQ(box.resolve("Gradius III - SA-1", "jp"), std::string("Gradius III (Japan)"));
	CHECK_EQ(box.resolve("Contra 3 - The Alien Wars (US) (Virtual Console)", ""), std::string("Contra III - The Alien Wars (USA)"));
	CHECK_EQ(box.resolve("Legend of Zelda, The - A Link to the Past - Fastrom", ""), std::string("Legend of Zelda, The - A Link to the Past (USA)"));
	// of a region's releases, the plain one
	Names md;
	for (const char* n : { "Aladdin (USA) (Final Cut)", "Aladdin (USA)", "Aladdin (Europe)" })
		md.add(n);
	CHECK_EQ(md.resolve("Aladdin (US)", ""), std::string("Aladdin (USA)"));
	// another number is another game; nothing like it is nothing
	CHECK_EQ(box.resolve("Final Fantasy IV (US)", ""), std::string(""));
	CHECK_EQ(box.resolve("Super Mario Kart (US)", ""), std::string(""));
}

TEST(rom_crcs_are_no_intros)
{
	// plain: both the same
	std::vector<unsigned char> rom = bytes(256 * 1024, 1);
	RomCrc c = romCrc(rom);
	CHECK(c.known);
	CHECK_EQ(c.whole, crcOf(rom));
	CHECK_EQ(c.contents, crcOf(rom));

	// a SNES copier header (size % 1024 == 512)
	std::vector<unsigned char> snes(512, 0);
	snes.insert(snes.end(), rom.begin(), rom.end());
	c = romCrc(snes);
	CHECK_EQ(c.contents, crcOf(rom));
	CHECK_EQ(c.whole, crcOf(snes));

	// iNES, with and without a trainer
	std::vector<unsigned char> nes = { 'N', 'E', 'S', 0x1a, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	std::vector<unsigned char> prg = bytes(40 * 1024, 2);
	nes.insert(nes.end(), prg.begin(), prg.end());
	CHECK_EQ(romCrc(nes).contents, crcOf(prg));
	std::vector<unsigned char> trained = { 'N', 'E', 'S', 0x1a, 2, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	trained.resize(16 + 512, 0xee);
	trained.insert(trained.end(), prg.begin(), prg.end());
	CHECK_EQ(romCrc(trained).contents, crcOf(prg));

	// Lynx and 7800 headers
	std::vector<unsigned char> lynx = { 'L', 'Y', 'N', 'X' };
	lynx.resize(64, 0);
	lynx.insert(lynx.end(), prg.begin(), prg.end());
	CHECK_EQ(romCrc(lynx).contents, crcOf(prg));
	std::vector<unsigned char> a78 = { 1, 'A', 'T', 'A', 'R', 'I', '7', '8', '0', '0' };
	a78.resize(128, 0);
	a78.insert(a78.end(), prg.begin(), prg.end());
	CHECK_EQ(romCrc(a78).contents, crcOf(prg));

	// N64 in its three byte orders: the big-endian one's CRC
	std::vector<unsigned char> z64 = bytes(3 * 1024 * 1024 + 8, 3);
	z64[0] = 0x80; z64[1] = 0x37; z64[2] = 0x12; z64[3] = 0x40;
	std::vector<unsigned char> v64 = z64, n64 = z64;
	for (size_t i = 0; i + 1 < v64.size(); i += 2) std::swap(v64[i], v64[i + 1]);
	for (size_t i = 0; i + 3 < n64.size(); i += 4) { std::swap(n64[i], n64[i + 3]); std::swap(n64[i + 1], n64[i + 2]); }
	CHECK_EQ(romCrc(z64).contents, crcOf(z64));
	CHECK_EQ(romCrc(v64).contents, crcOf(z64));
	CHECK_EQ(romCrc(n64).contents, crcOf(z64));
	CHECK_EQ(romCrc(v64).whole, crcOf(v64));

	// a file, read in pieces of 1 MB
	const std::string folder = tempFolder();
	const std::string path = folder + "/big.z64";
	{
		std::ofstream out(path, std::ios::binary);
		out.write((const char*)v64.data(), v64.size());
	}
	c = romCrc(path);
	CHECK(c.known);
	CHECK_EQ(c.contents, crcOf(z64));
	CHECK_EQ(c.whole, crcOf(v64));
	Utils::FileSystem::removeFile(path);
	rmdir(folder.c_str());

	CHECK(!romCrc(std::vector<unsigned char>()).known);
}

TEST(dat_and_github_trees_are_read)
{
	std::unordered_map<uint32_t, std::string> byCrc;
	parseDat(
		"clrmamepro (\n\tname \"Nintendo - Super Nintendo Entertainment System\"\n)\n\n"
		"game (\n\tname \"'96 Zenkoku Koukou Soccer Senshuken (Japan)\"\n\tregion \"Japan\"\n"
		"\trom ( name \"'96 Zenkoku Koukou Soccer Senshuken (Japan).sfc\" size 1572864 crc 05FBB855 md5 33 sha1 00 )\n)\n"
		"game (\n\tname \"16 BIT XMAS 2011 - Christmas Craze (World)\"\n"
		"\trom ( name \"16 BIT XMAS 2011 - Christmas Craze (World) (Aftermarket) (Unl).sfc\" size 524288 crc 5F7B6627 )\n)\n"
		"game (\n\tname \"16 BIT XMAS 2011 - Christmas Craze (World)\"\n"
		"\trom ( name \"16 BIT XMAS 2011 - Christmas Craze (World) (Alt).sfc\" size 524288 crc d23ca2d7 )\n)\n", byCrc);
	CHECK_EQ(byCrc.size(), (size_t)3);
	CHECK_EQ(byCrc[0x05FBB855], std::string("'96 Zenkoku Koukou Soccer Senshuken (Japan)"));
	CHECK_EQ(byCrc[0xD23CA2D7], std::string("16 BIT XMAS 2011 - Christmas Craze (World)"));

	std::vector<std::pair<std::string, std::string>> entries;
	bool truncated = true;
	CHECK(parseTree("{\"sha\":\"abc\",\"url\":\"u\",\"tree\":["
		"{\"path\":\"Named_Boxarts\",\"mode\":\"040000\",\"type\":\"tree\",\"sha\":\"b0\",\"url\":\"u\"},"
		"{\"path\":\"Named_Snaps\",\"mode\":\"040000\",\"type\":\"tree\",\"sha\":\"s0\",\"url\":\"u\"},"
		"{\"path\":\"README.md\",\"mode\":\"100644\",\"type\":\"blob\",\"sha\":\"r0\",\"size\":10,\"url\":\"u\"}],"
		"\"truncated\":false}", entries, &truncated));
	CHECK(!truncated);
	CHECK_EQ(entries.size(), (size_t)3);
	CHECK_EQ(entries[1].first, std::string("Named_Snaps"));
	CHECK_EQ(entries[1].second, std::string("s0"));
	CHECK(!parseTree("{\"message\":\"API rate limit exceeded\"}", entries));
}

TEST(database_rows_are_found_by_crc_or_name)
{
	DbTable db;
	db.parse("# comment\n"
		"101 Dalmatas\thF22422423EE8A90435AFC53730E017312257DB26\tDEE0CD43\n"
		"16 BIT XMAS 2011 - Christmas Craze\th6780075E642EBE2030D1D92A574533931CBFD8BE t113048\t5F7B6627,D23CA2D7\n"
		"Kaizo Mario World\tt64415\t\n"
		"broken line without tabs\n");
	CHECK_EQ(db.size(), (size_t)3);

	const DbRow* row = db.find({ 0xD23CA2D7 }, {});
	CHECK(row != nullptr && row->name == "16 BIT XMAS 2011 - Christmas Craze");
	if (row != nullptr)
	{
		std::vector<std::string> urls = DbTable::urls(*row);
		CHECK_EQ(urls.size(), (size_t)2);
		CHECK_EQ(urls[0], std::string("https://hasheous.org/api/v1/images/6780075E642EBE2030D1D92A574533931CBFD8BE"));
		CHECK_EQ(urls[1], std::string("https://cdn.thegamesdb.net/images/original/boxart/front/113048-1.jpg"));
	}
	row = db.find({ 0x12345678 }, { "Kaizo Mario World (Hack) - Fastrom" });
	CHECK(row != nullptr && row->name == "Kaizo Mario World");
	CHECK(db.find({ 0x12345678 }, { "Super Mario World (US)" }) == nullptr);
}

TEST(systems_are_found_by_name_platform_and_extension)
{
	const std::string folder = tempFolder();
	write(folder + "/systems.tsv", SYSTEMS);
	endpoints().tables = folder;

	const System* s = findSystem({ "snes" }, ".sfc");
	CHECK(s != nullptr && s->repo == "Nintendo_-_Super_Nintendo_Entertainment_System" && s->dat);
	s = findSystem({ "mysnes", "snes-msu1" }, "");
	CHECK(s != nullptr && s->name == "Nintendo - Super Nintendo Entertainment System");
	s = findSystem({ "sgb" }, ".gbc");
	CHECK(s != nullptr && s->name == "Nintendo - Game Boy Color");
	s = findSystem({ "sgb" }, ".GB");
	CHECK(s != nullptr && s->name == "Nintendo - Game Boy");
	s = findSystem({ "psx" }, ".chd");
	CHECK(s != nullptr && !s->dat);
	CHECK(findSystem({ "amiga" }, ".adf") == nullptr);

	endpoints().tables.clear();
	Utils::FileSystem::removeFile(folder + "/systems.tsv");
	rmdir(folder.c_str());
}

TEST(art_is_chosen_by_crc_then_name_then_database)
{
	Art art;
	art.repo = "Nintendo_-_Super_Nintendo_Entertainment_System";
	art.indexed = true;
	for (const char* n : { "Mega Man X3 (USA)", "Mega Man X3 (Japan)", "Super Mario World (USA)", "Rockman X3 (Japan)",
		"Tom & Jerry (USA)" })
		art.folders[BOXART].add(n);
	for (const char* n : { "Mega Man X3 (USA)", "Rockman X3 (Japan)", "Kaizo Mario World", "Tom _ Jerry (USA)" })
		art.folders[SNAP].add(n);
	art.folders[TITLE].add("Rockman X3 (Japan)");
	art.byCrc[0x11111111] = "Rockman X3 (Japan)";
	art.byCrc[0x22222222] = "Tom & Jerry (USA)";
	art.db.parse("Kaizo Mario World\thABCDEF0123456789ABCDEF0123456789ABCDEF01 t64415\t33333333\n");

	// a renamed ROM, known by its CRC32: its own art, in every folder
	RomCrc crc;
	crc.known = true;
	crc.contents = 0x11111111;
	Choice c = choose(art, "Mega Man X3 (US) [!]", crc, "");
	CHECK_EQ(c.noIntro, std::string("Rockman X3 (Japan)"));
	CHECK_EQ(c.names[BOXART], std::string("Rockman X3 (Japan)"));
	CHECK_EQ(c.names[SNAP], std::string("Rockman X3 (Japan)"));
	CHECK_EQ(c.names[TITLE], std::string("Rockman X3 (Japan)"));
	CHECK(c.db.empty());

	// by its name: a folder without that game has nothing
	c = choose(art, "Megaman X3 (US) - Fastrom", RomCrc(), "");
	CHECK_EQ(c.names[BOXART], std::string("Mega Man X3 (USA)"));
	CHECK_EQ(c.names[SNAP], std::string("Mega Man X3 (USA)"));
	CHECK_EQ(c.names[TITLE], std::string(""));

	// No-Intro's & is libretro-thumbnails' _ (its box art here keeps the &, as some do)
	crc.contents = 0x22222222;
	c = choose(art, "tj", crc, "");
	CHECK_EQ(c.names[SNAP], std::string("Tom _ Jerry (USA)"));

	// no box art: the database's cover, the screenshot still from libretro-thumbnails
	crc.contents = 0x33333333;
	c = choose(art, "Kaizo Mario World (Hack)", crc, "");
	CHECK_EQ(c.names[BOXART], std::string(""));
	CHECK_EQ(c.names[SNAP], std::string("Kaizo Mario World"));
	CHECK_EQ(c.db.size(), (size_t)2);

	// not listed: the names worked out are tried, the CRC's first
	art.indexed = false;
	art.datNames.add("Rockman X3 (Japan)");
	art.datNames.add("Mega Man X3 (USA)");
	crc.contents = 0x11111111;
	c = choose(art, "Megaman X3 (US)", crc, "");
	CHECK_EQ(c.guesses.size(), (size_t)3);
	if (c.guesses.size() == 3)
	{
		CHECK_EQ(c.guesses[0], std::string("Rockman X3 (Japan)"));
		CHECK_EQ(c.guesses[1], std::string("Mega Man X3 (USA)"));
		CHECK_EQ(c.guesses[2], std::string("Megaman X3 (US)"));
	}

	CHECK_EQ(artUrl(art.repo, BOXART, "Tom _ Jerry (USA)"),
		std::string("https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Super_Nintendo_Entertainment_System/master/Named_Boxarts/Tom%20_%20Jerry%20%28USA%29.png"));
}

TEST(a_systems_art_loads_from_the_cache_or_without_network)
{
	const std::string folder = tempFolder();
	write(folder + "/systems.tsv", SYSTEMS);
	write(folder + "/Nintendo_-_Super_Nintendo_Entertainment_System.tsv", "Kaizo Mario World\tt64415\t33333333\n");
	write(folder + "/Nintendo_-_Super_Nintendo_Entertainment_System.names", "B\tMega Man X3 (USA)\nS\tMega Man X3 (USA)\nT\tMega Man X3 (Japan)\n");
	write(folder + "/Nintendo_-_Super_Nintendo_Entertainment_System.crc", "11111111\tMega Man X3 (USA)\n");
	endpoints().tables = folder;
	endpoints().cache = folder;
	// nothing answers here: what is not cached is not had
	endpoints().api = "http://127.0.0.1:9/";
	endpoints().dat = "http://127.0.0.1:9/";

	const System* snes = findSystem({ "snes" }, ".sfc");
	CHECK(snes != nullptr);
	if (snes != nullptr)
	{
		Source* source = Source::get(*snes);
		CHECK(source->poll()); // fresh cache: at once
		CHECK(source->art().indexed);
		CHECK_EQ(source->art().folders[BOXART].size(), (size_t)1);
		CHECK_EQ(source->art().byCrc.size(), (size_t)1);
		CHECK_EQ(source->art().db.size(), (size_t)1);
		CHECK(Source::get(*snes) == source);
	}

	// Game Boy: no cache, no network
	const System* gb = findSystem({ "gb" }, ".gb");
	CHECK(gb != nullptr);
	if (gb != nullptr)
	{
		Source* source = Source::get(*gb);
		bool done = false;
		for (int i = 0; i < 500 && !done; i++)
		{
			done = source->poll();
			if (!done)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		CHECK(done);
		CHECK(!source->art().indexed);
		CHECK(source->art().byCrc.empty());
		Choice c = choose(source->art(), "Tetris (World) (Rev 1)", RomCrc(), "");
		CHECK_EQ(c.guesses.size(), (size_t)1);
	}

	endpoints() = Endpoints();
	for (const char* f : { "systems.tsv", "Nintendo_-_Super_Nintendo_Entertainment_System.tsv",
		"Nintendo_-_Super_Nintendo_Entertainment_System.names", "Nintendo_-_Super_Nintendo_Entertainment_System.crc" })
		Utils::FileSystem::removeFile(folder + "/" + f);
	rmdir(folder.c_str());
}
