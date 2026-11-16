#pragma once
#ifndef ES_APP_SCRAPERS_LIBRETRO_ART_H
#define ES_APP_SCRAPERS_LIBRETRO_ART_H

// The "Libretro-thumbnails" scraper's data, without ES's game list: which art a game is, where it is, and how what
// that takes is fetched and kept (LibretroThumbnailsScraper.cpp asks this for each game).
//
// The art comes from libretro-thumbnails (https://github.com/libretro-thumbnails, one repository a system, its images
// named after No-Intro's names): the box art, the title screen and a screenshot, downloaded from
// raw.githubusercontent.com as Snes9x PS5 does. A game is known by its ROM's CRC32 (libretro-database's No-Intro DAT
// names it) or by the words of its file name (LibretroArtMatch.h). The names of a system's art come from GitHub's
// API (four requests a system, kept 14 days); without them (no network, GitHub's limit of 60 requests an hour) the
// names worked out are tried in turn.
//
// For a game libretro-thumbnails has no box art of, resources/scrapers/libretro/<repository>.tsv (Hasheous' dumps,
// mister/tools/make-db-art.py) may say where an online database has its cover: an IGDB image served by hasheous.org,
// or TheGamesDB's front box art. Only that image is downloaded: nothing about the ROM is sent anywhere.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class HttpReq;

namespace LibretroArtMatch
{
	struct Index;
}

namespace LibretroArt
{
	enum Kind { BOXART = 0, SNAP = 1, TITLE = 2, KINDS = 3 };
	extern const char* const FOLDERS[KINDS]; // "Named_Boxarts", "Named_Snaps", "Named_Titles"

	// libretro-thumbnails' file name for a name: &*/:`<>?\|" become _
	std::string thumbnailName(const std::string& name);

	// A list of names with its word index (LibretroArtMatch) made the first time it is asked for.
	class Names
	{
	public:
		Names();
		~Names();

		void add(const std::string& name);
		void clear();
		bool has(const std::string& name) const { return mSet.count(name) != 0; }
		bool empty() const { return mList.empty(); }
		size_t size() const { return mList.size(); }
		const std::vector<std::string>& list() const { return mList; }

		// the name of the list's that means the same game as name, or "" (LibretroArtMatch::Resolve); whole: by the
		// whole title, not by its main part before " - " (a list of variants and hacks)
		std::string resolve(const std::string& name, const std::string& region, bool whole = false) const;

	private:
		std::vector<std::string> mList;
		std::unordered_set<std::string> mSet;
		mutable std::unique_ptr<LibretroArtMatch::Index> mIndex;
		mutable std::mutex mIndexLock;
	};

	// resources/scrapers/libretro/<repository>.tsv: name<TAB>sources<TAB>crc,crc... (Snes9x PS5's fe_dbart.h)
	struct DbRow
	{
		std::string name;
		std::vector<std::string> sources; // "h<image id>" (hasheous.org) or "t<id>" (TheGamesDB), best first
	};

	class DbTable
	{
	public:
		void parse(const std::string& text);
		bool empty() const { return mRows.empty(); }
		size_t size() const { return mRows.size(); }

		// the row of a ROM: by one of its CRC32s (0: unknown), else by the words of one of its names
		const DbRow* find(const std::vector<uint32_t>& crcs, const std::vector<std::string>& names) const;
		static std::vector<std::string> urls(const DbRow& row);

	private:
		std::vector<DbRow> mRows;
		Names mNames;
		std::unordered_map<uint32_t, size_t> mByCrc;
		std::unordered_map<std::string, size_t> mByName;
	};

	// What is known of a system's art.
	struct Art
	{
		std::string repo;                // "Nintendo_-_Super_Nintendo_Entertainment_System"
		bool indexed = false;            // folders[] hold the names libretro-thumbnails has
		Names folders[KINDS];
		std::unordered_map<uint32_t, std::string> byCrc; // No-Intro's names by CRC32
		Names datNames;                  // the DAT's names as art names: what a file name is matched to when !indexed
		DbTable db;
	};

	// A ROM's CRC32: of the whole file, and of its contents (without a copier or iNES/FDS/Lynx/7800 header, an N64
	// ROM in big-endian order), as No-Intro's DATs have them. Files over 64 MB are not read.
	struct RomCrc
	{
		bool known = false;
		uint32_t whole = 0;
		uint32_t contents = 0;
	};
	RomCrc romCrc(const std::string& path);
	RomCrc romCrc(const std::vector<unsigned char>& data); // the same, of bytes

	// The art chosen for a game.
	struct Choice
	{
		std::string noIntro;             // the No-Intro name its CRC32 has, or ""
		std::string names[KINDS];        // indexed: the art of each kind that exists, or ""
		std::vector<std::string> guesses; // !indexed: art names to try, best first
		std::vector<std::string> db;     // where a database has its cover, best first (no box art)
	};

	// region: "USA", "Europe", "Japan", "World" or "" (the one the file name has, else USA's, World's...)
	Choice choose(const Art& art, const std::string& fileName, const RomCrc& crc, const std::string& region);

	std::string artUrl(const std::string& repo, Kind kind, const std::string& name);

	// GitHub's tree of a repository folder: its entries' paths and object ids ("truncated": not all of them)
	bool parseTree(const std::string& json, std::vector<std::pair<std::string, std::string>>& entries, bool* truncated = nullptr);
	// a clrmamepro DAT: the games' names by their ROMs' CRC32
	void parseDat(const std::string& text, std::unordered_map<uint32_t, std::string>& byCrc);

	// A line of resources/scrapers/libretro/systems.tsv.
	struct System
	{
		std::vector<std::string> keys;   // ES system or platform names, "name/.ext" for some games of one
		std::string name;                // "Nintendo - Super Nintendo Entertainment System"
		std::string repo;                // the same with _ for spaces
		bool dat = false;                // libretro-database has its No-Intro DAT
	};

	// The system of a game, from ES's system name and platform names, and the game's extension (with its dot).
	const System* findSystem(const std::vector<std::string>& names, const std::string& extension);

	// A system's art as it is loaded: from the cache folder, then (when older than its time, or absent) from the
	// network, the requests going on while poll() is called. One a repository, kept for the program's life.
	class Source
	{
	public:
		static Source* get(const System& system);

		bool poll();                      // true once the art is loaded (with what could be had)
		const Art& art() const { return mArt; }

	private:
		explicit Source(const System& system);

		void start();
		void onRoot();
		void finish();

		enum State { IDLE, LOADING, READY };

		System mSystem;
		Art mArt;
		State mState = IDLE;
		std::mutex mLock;

		std::string mCache;               // the cache folder
		std::unique_ptr<HttpReq> mRoot;
		std::unique_ptr<HttpReq> mFolders[KINDS];
		std::unique_ptr<HttpReq> mDat;
		bool mRootDone = false;
	};

	// Where the cache is (default: ~/.emulationstation/scrapers/libretro), where systems.tsv and the database tables
	// are (default: the resources' scrapers/libretro) and where things are downloaded from; the tests point these
	// elsewhere. The URLs end in /.
	struct Endpoints
	{
		std::string cache;
		std::string tables;
		std::string api = "https://api.github.com/repos/libretro-thumbnails/";
		std::string raw = "https://raw.githubusercontent.com/libretro-thumbnails/";
		std::string dat = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/";
		std::string hasheous = "https://hasheous.org/api/v1/images/";
		std::string thegamesdb = "https://cdn.thegamesdb.net/images/original/boxart/front/";
	};
	Endpoints& endpoints();
}

#endif // ES_APP_SCRAPERS_LIBRETRO_ART_H
