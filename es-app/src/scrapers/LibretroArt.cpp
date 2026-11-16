#include "scrapers/LibretroArt.h"
#include "scrapers/LibretroArtMatch.h"

#include "HttpReq.h"
#include "Log.h"
#include "Paths.h"
#include "resources/ResourceManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "utils/ZipFile.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace LibretroArt
{
	const char* const FOLDERS[KINDS] = { "Named_Boxarts", "Named_Snaps", "Named_Titles" };

	namespace
	{
		const char* const KIND_LETTERS = "BST"; // the cache's names file: B, S or T, a tab, the name

		const long NAMES_DAYS = 14;  // libretro-thumbnails changes every week
		const long CRCS_DAYS = 60;   // No-Intro's DATs, every month or two

		std::string readFile(const std::string& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
				return std::string();
			std::ostringstream text;
			text << in.rdbuf();
			return text.str();
		}

		// written beside, then renamed: a cache file is whole or absent
		bool writeFile(const std::string& path, const std::string& text)
		{
			const std::string part = path + ".part";
			FILE* f = fopen(part.c_str(), "wb");
			if (f == nullptr)
				return false;
			bool ok = text.empty() || fwrite(text.data(), 1, text.size(), f) == text.size();
			ok = fclose(f) == 0 && ok;
			if (!ok || rename(part.c_str(), path.c_str()) != 0)
			{
				remove(part.c_str());
				return false;
			}
			return true;
		}

		// how old a file is, in days (-1: there is none)
		long ageInDays(const std::string& path)
		{
			if (!Utils::FileSystem::exists(path))
				return -1;
			const time_t changed = Utils::FileSystem::getFileModificationDate(path).getTime();
			const time_t now = time(nullptr);
			return now > changed ? long((now - changed) / 86400) : 0;
		}

		template<typename F>
		void eachLine(const std::string& text, F f)
		{
			size_t at = 0;
			while (at < text.size())
			{
				size_t end = text.find('\n', at);
				if (end == std::string::npos)
					end = text.size();
				size_t len = end - at;
				if (len > 0 && text[at + len - 1] == '\r')
					--len;
				f(text.substr(at, len));
				at = end + 1;
			}
		}

		std::string regionOfSetting(const std::string& region)
		{
			if (region == "us") return "USA";
			if (region == "eu") return "Europe";
			if (region == "jp") return "Japan";
			if (region == "wor") return "World";
			return region;
		}
	}

	std::string thumbnailName(const std::string& name)
	{
		std::string out = name;
		for (char& c : out)
			if (strchr("&*/:`<>?\\|\"", c) != nullptr && c != 0)
				c = '_';
		return out;
	}

	// Names --------------------------------------------------------------------------------------------------------

	Names::Names() {}
	Names::~Names() {}

	void Names::add(const std::string& name)
	{
		if (name.empty() || !mSet.insert(name).second)
			return;
		mList.push_back(name);
		std::lock_guard<std::mutex> hold(mIndexLock);
		mIndex.reset();
	}

	void Names::clear()
	{
		mList.clear();
		mSet.clear();
		std::lock_guard<std::mutex> hold(mIndexLock);
		mIndex.reset();
	}

	std::string Names::resolve(const std::string& name, const std::string& region) const
	{
		if (mList.empty() || name.empty())
			return std::string();
		std::lock_guard<std::mutex> hold(mIndexLock);
		if (!mIndex)
		{
			mIndex.reset(new LibretroArtMatch::Index);
			LibretroArtMatch::Build(*mIndex, mList);
		}
		return LibretroArtMatch::Resolve(*mIndex, name, regionOfSetting(region));
	}

	// DbTable ------------------------------------------------------------------------------------------------------

	void DbTable::parse(const std::string& text)
	{
		eachLine(text, [this](const std::string& line)
		{
			if (line.empty() || line[0] == '#')
				return;
			const size_t a = line.find('\t');
			const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
			if (b == std::string::npos || a == 0)
				return;
			DbRow row;
			row.name = line.substr(0, a);
			for (const std::string& s : Utils::String::split(line.substr(a + 1, b - a - 1), ' ', true))
				if (s.size() > 1 && (s[0] == 'h' || s[0] == 't'))
					row.sources.push_back(s);
			if (row.sources.empty() || mByName.count(row.name))
				return;
			const size_t index = mRows.size();
			for (const std::string& crc : Utils::String::split(line.substr(b + 1), ',', true))
				if (crc.size() == 8)
					mByCrc.emplace(uint32_t(strtoul(crc.c_str(), nullptr, 16)), index);
			mByName.emplace(row.name, index);
			mNames.add(row.name);
			mRows.push_back(std::move(row));
		});
	}

	const DbRow* DbTable::find(const std::vector<uint32_t>& crcs, const std::vector<std::string>& names) const
	{
		for (uint32_t crc : crcs)
		{
			if (crc == 0)
				continue;
			auto it = mByCrc.find(crc);
			if (it != mByCrc.end())
				return &mRows[it->second];
		}
		for (const std::string& name : names)
		{
			const std::string found = mNames.resolve(name, "");
			auto it = mByName.find(found);
			if (it != mByName.end())
				return &mRows[it->second];
		}
		return nullptr;
	}

	std::vector<std::string> DbTable::urls(const DbRow& row)
	{
		std::vector<std::string> out;
		for (const std::string& s : row.sources)
			out.push_back(s[0] == 'h' ? endpoints().hasheous + s.substr(1) : endpoints().thegamesdb + s.substr(1) + "-1.jpg");
		return out;
	}

	// CRC32 --------------------------------------------------------------------------------------------------------

	namespace
	{
		const uint64_t MAX_ROM = 64ULL << 20;

		// What comes before the contents, and the byte order of an N64 ROM.
		struct Layout
		{
			size_t skip = 0;
			int swap = 0; // 2: v64 (16-bit words swapped), 4: n64 (32-bit words reversed)
		};

		Layout layoutOf(const unsigned char* head, size_t n, uint64_t size)
		{
			Layout l;
			if (n >= 16 && memcmp(head, "NES\x1a", 4) == 0)
				l.skip = 16 + ((head[6] & 4) ? 512 : 0); // iNES, with its trainer
			else if (n >= 16 && memcmp(head, "FDS\x1a", 4) == 0)
				l.skip = 16;
			else if (n >= 64 && memcmp(head, "LYNX", 4) == 0)
				l.skip = 64;
			else if (n >= 128 && memcmp(head + 1, "ATARI7800", 9) == 0)
				l.skip = 128;
			else if (n >= 4 && head[0] == 0x37 && head[1] == 0x80 && head[2] == 0x40 && head[3] == 0x12)
				l.swap = 2;
			else if (n >= 4 && head[0] == 0x40 && head[1] == 0x12 && head[2] == 0x37 && head[3] == 0x80)
				l.swap = 4;
			else if (size % 1024 == 512 && size > 512)
				l.skip = 512; // a copier's header (SNES, PC Engine)
			return l;
		}

		void swapped(unsigned char* p, size_t n, int swap)
		{
			if (swap == 2)
				for (size_t i = 0; i + 1 < n; i += 2)
					std::swap(p[i], p[i + 1]);
			else if (swap == 4)
				for (size_t i = 0; i + 3 < n; i += 4)
				{
					std::swap(p[i], p[i + 3]);
					std::swap(p[i + 1], p[i + 2]);
				}
		}

		// read(buffer, size) returns how many bytes it put there, 0 at the end
		template<typename R>
		RomCrc crcOf(uint64_t size, R read)
		{
			RomCrc out;
			if (size == 0 || size > MAX_ROM)
				return out;
			std::vector<unsigned char> buffer(1 << 20);
			size_t got = read(buffer.data(), buffer.size());
			const Layout layout = layoutOf(buffer.data(), got, size);
			uint64_t offset = 0;
			unsigned int whole = 0, contents = 0;
			while (got > 0)
			{
				whole = Utils::Zip::ZipFile::computeCRC(whole, buffer.data(), got);
				size_t from = 0;
				if (offset < layout.skip)
					from = size_t(std::min<uint64_t>(got, layout.skip - offset));
				if (from < got)
				{
					swapped(buffer.data() + from, got - from, layout.swap);
					contents = Utils::Zip::ZipFile::computeCRC(contents, buffer.data() + from, got - from);
				}
				offset += got;
				got = read(buffer.data(), buffer.size());
			}
			out.known = offset == size;
			out.whole = whole;
			out.contents = contents;
			return out;
		}
	}

	RomCrc romCrc(const std::string& path)
	{
		const uint64_t size = Utils::FileSystem::getFileSize(path);
		FILE* f = fopen(path.c_str(), "rb");
		if (f == nullptr)
			return RomCrc();
		RomCrc out = crcOf(size, [f](unsigned char* p, size_t n) { return fread(p, 1, n, f); });
		fclose(f);
		return out;
	}

	RomCrc romCrc(const std::vector<unsigned char>& data)
	{
		size_t at = 0;
		return crcOf(data.size(), [&](unsigned char* p, size_t n)
		{
			n = std::min(n, data.size() - at);
			memcpy(p, data.data() + at, n);
			at += n;
			return n;
		});
	}

	// Choosing -----------------------------------------------------------------------------------------------------

	Choice choose(const Art& art, const std::string& fileName, const RomCrc& crc, const std::string& region)
	{
		Choice c;
		if (crc.known)
		{
			for (uint32_t value : { crc.contents, crc.whole })
			{
				auto it = art.byCrc.find(value);
				if (it != art.byCrc.end())
				{
					c.noIntro = it->second;
					break;
				}
			}
		}
		const std::string known = thumbnailName(c.noIntro);

		// the art of a kind: the No-Intro name's own, else another release of that game, else the file name's game
		auto find = [&](const Names& folder) -> std::string
		{
			if (!known.empty())
			{
				if (folder.has(known))
					return known;
				std::string other = folder.resolve(known, region);
				if (!other.empty())
					return other;
			}
			return folder.resolve(fileName, region);
		};

		if (art.indexed)
		{
			c.names[BOXART] = find(art.folders[BOXART]);
			for (Kind kind : { SNAP, TITLE })
			{
				const std::string& box = c.names[BOXART];
				c.names[kind] = !box.empty() && art.folders[kind].has(box) ? box : find(art.folders[kind]);
			}
		}
		else
		{
			auto guess = [&c](const std::string& name)
			{
				if (!name.empty() && std::find(c.guesses.begin(), c.guesses.end(), name) == c.guesses.end())
					c.guesses.push_back(name);
			};
			guess(known);
			if (!art.datNames.empty())
				guess(art.datNames.resolve(fileName, region));
			guess(thumbnailName(fileName)); // a file named as No-Intro names it
		}

		if (c.names[BOXART].empty())
		{
			std::vector<uint32_t> crcs;
			if (crc.known)
				crcs = { crc.contents, crc.whole };
			std::vector<std::string> names;
			if (!c.noIntro.empty())
				names.push_back(c.noIntro);
			names.push_back(fileName);
			if (const DbRow* row = art.db.find(crcs, names))
				c.db = DbTable::urls(*row);
		}
		return c;
	}

	std::string artUrl(const std::string& repo, Kind kind, const std::string& name)
	{
		return endpoints().raw + repo + "/master/" + FOLDERS[kind] + "/" + HttpReq::urlEncode(name + ".png");
	}

	// Parsing ------------------------------------------------------------------------------------------------------

	bool parseTree(const std::string& json, std::vector<std::pair<std::string, std::string>>& entries, bool* truncated)
	{
		rapidjson::Document doc;
		doc.Parse(json.c_str(), json.size());
		if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("tree") || !doc["tree"].IsArray())
			return false;
		if (truncated != nullptr)
			*truncated = doc.HasMember("truncated") && doc["truncated"].IsBool() && doc["truncated"].GetBool();
		for (const auto& e : doc["tree"].GetArray())
		{
			if (!e.IsObject() || !e.HasMember("path") || !e["path"].IsString())
				continue;
			const std::string sha = e.HasMember("sha") && e["sha"].IsString() ? e["sha"].GetString() : "";
			entries.emplace_back(e["path"].GetString(), sha);
		}
		return true;
	}

	void parseDat(const std::string& text, std::unordered_map<uint32_t, std::string>& byCrc)
	{
		std::string game;
		bool inGame = false;
		eachLine(text, [&](const std::string& line)
		{
			size_t b = line.find_first_not_of(" \t");
			if (b == std::string::npos)
				return;
			if (line.compare(b, 5, "game ") == 0 || line.compare(b, 5, "game(") == 0)
			{
				inGame = true;
				game.clear();
				return;
			}
			if (!inGame)
				return;
			if (b == 0 && line[0] == ')')
			{
				inGame = false;
				return;
			}
			if (game.empty() && line.compare(b, 6, "name \"") == 0)
			{
				const size_t end = line.rfind('"');
				if (end > b + 6)
					game = line.substr(b + 6, end - b - 6);
				return;
			}
			const size_t rom = line.find("crc ", b);
			if (!game.empty() && line.compare(b, 3, "rom") == 0 && rom != std::string::npos && rom + 12 <= line.size())
			{
				const std::string hex = line.substr(rom + 4, 8);
				if (hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
					byCrc.emplace(uint32_t(strtoul(hex.c_str(), nullptr, 16)), game);
			}
		});
	}

	// Systems ------------------------------------------------------------------------------------------------------

	namespace
	{
		std::string tablePath(const std::string& file)
		{
			if (!endpoints().tables.empty())
				return endpoints().tables + "/" + file;
			return ResourceManager::getInstance()->getResourcePath(":/scrapers/libretro/" + file);
		}

		const std::vector<System>& systems()
		{
			static std::mutex lock;
			static std::vector<System> list;
			static std::string loadedFrom;
			std::lock_guard<std::mutex> hold(lock);
			const std::string path = tablePath("systems.tsv");
			if (loadedFrom == path)
				return list;
			loadedFrom = path;
			list.clear();
			eachLine(readFile(path), [](const std::string& line)
			{
				if (line.empty() || line[0] == '#')
					return;
				const std::vector<std::string> cols = Utils::String::split(line, '\t');
				if (cols.size() < 3 || cols[1].empty())
					return;
				System s;
				s.keys = Utils::String::split(cols[0], ',', true);
				s.name = cols[1];
				s.repo = Utils::String::replace(s.name, " ", "_");
				s.dat = cols[2] == "1";
				list.push_back(s);
			});
			LOG(LogInfo) << "Libretro-thumbnails: " << list.size() << " systems in " << path;
			return list;
		}
	}

	const System* findSystem(const std::vector<std::string>& names, const std::string& extension)
	{
		const std::string ext = Utils::String::toLower(extension);
		const System* plain = nullptr;
		for (const std::string& name : names)
		{
			for (const System& s : systems())
				for (const std::string& key : s.keys)
				{
					const size_t slash = key.find('/');
					if (slash == std::string::npos)
					{
						if (key == name && plain == nullptr)
							plain = &s;
					}
					else if (!ext.empty() && key.compare(0, slash, name) == 0 && slash == name.size() &&
						Utils::String::toLower(key.substr(slash + 1)) == ext)
						return &s; // "sgb/.gbc": that extension's own line comes first
				}
			if (plain != nullptr)
				return plain;
		}
		return plain;
	}

	// Source -------------------------------------------------------------------------------------------------------

	Endpoints& endpoints()
	{
		static Endpoints e;
		return e;
	}

	Source* Source::get(const System& system)
	{
		static std::mutex lock;
		static std::map<std::string, std::unique_ptr<Source>> sources;
		std::lock_guard<std::mutex> hold(lock);
		std::unique_ptr<Source>& s = sources[system.repo];
		if (!s)
			s.reset(new Source(system));
		return s.get();
	}

	Source::Source(const System& system) : mSystem(system)
	{
		mArt.repo = system.repo;
	}

	namespace
	{
		std::unique_ptr<HttpReq> githubRequest(const std::string& url)
		{
			HttpReqOptions options;
			options.customHeaders.push_back("Accept: application/vnd.github+json");
			options.customHeaders.push_back("X-GitHub-Api-Version: 2022-11-28");
			return std::unique_ptr<HttpReq>(new HttpReq(url, &options));
		}

		// the cache's names file
		void loadNames(const std::string& text, Art& art)
		{
			for (Names& f : art.folders)
				f.clear();
			eachLine(text, [&art](const std::string& line)
			{
				if (line.size() < 3 || line[1] != '\t')
					return;
				const char* k = strchr(KIND_LETTERS, line[0]);
				if (k != nullptr && line[0] != 0)
					art.folders[k - KIND_LETTERS].add(line.substr(2));
			});
			art.indexed = !art.folders[BOXART].empty();
		}

		void loadCrcs(const std::string& text, Art& art)
		{
			art.byCrc.clear();
			art.datNames.clear();
			eachLine(text, [&art](const std::string& line)
			{
				if (line.size() < 10 || line[8] != '\t')
					return;
				const std::string name = line.substr(9);
				art.byCrc.emplace(uint32_t(strtoul(line.substr(0, 8).c_str(), nullptr, 16)), name);
				art.datNames.add(thumbnailName(name));
			});
		}
	}

	bool Source::poll()
	{
		std::lock_guard<std::mutex> hold(mLock);
		if (mState == READY)
			return true;
		if (mState == IDLE)
		{
			start();
			if (mState == READY)
				return true;
		}

		if (mRoot && !mRootDone && mRoot->status() != HttpReq::REQ_IN_PROGRESS)
			onRoot();

		for (auto& f : mFolders)
			if (f && f->status() == HttpReq::REQ_IN_PROGRESS)
				return false;
		if (mDat && mDat->status() == HttpReq::REQ_IN_PROGRESS)
			return false;
		if (mRoot && !mRootDone)
			return false;

		finish();
		mState = READY;
		return true;
	}

	void Source::start()
	{
		mCache = endpoints().cache.empty() ? Paths::getUserEmulationStationPath() + "/scrapers/libretro" : endpoints().cache;
		Utils::FileSystem::createDirectory(mCache);

		const std::string db = tablePath(mSystem.repo + ".tsv");
		if (Utils::FileSystem::exists(db))
			mArt.db.parse(readFile(db));

		const std::string names = mCache + "/" + mSystem.repo + ".names";
		const long namesAge = ageInDays(names);
		if (namesAge >= 0)
			loadNames(readFile(names), mArt);
		if (namesAge < 0 || namesAge >= NAMES_DAYS)
			mRoot = githubRequest(endpoints().api + mSystem.repo + "/git/trees/master");

		if (mSystem.dat)
		{
			const std::string crcs = mCache + "/" + mSystem.repo + ".crc";
			const long crcsAge = ageInDays(crcs);
			if (crcsAge >= 0)
				loadCrcs(readFile(crcs), mArt);
			if (crcsAge < 0 || crcsAge >= CRCS_DAYS)
				mDat.reset(new HttpReq(endpoints().dat + HttpReq::urlEncode(mSystem.name + ".dat")));
		}

		mState = (mRoot || mDat) ? LOADING : READY;
		if (mState == READY)
		{
			LOG(LogInfo) << "Libretro-thumbnails: " << mSystem.repo << " from the cache (" << mArt.folders[BOXART].size()
				<< " box arts, " << mArt.byCrc.size() << " CRCs, " << mArt.db.size() << " database covers)";
		}
	}

	void Source::onRoot()
	{
		mRootDone = true;
		std::vector<std::pair<std::string, std::string>> entries;
		if (mRoot->status() != HttpReq::REQ_SUCCESS || !parseTree(mRoot->getContent(), entries))
		{
			LOG(LogWarning) << "Libretro-thumbnails: no list of " << mSystem.repo << "'s art from GitHub (" << mRoot->getErrorMsg()
				<< "): " << (mArt.indexed ? "the cached one is used" : "the names worked out are tried");
			return;
		}
		for (int k = 0; k < KINDS; k++)
			for (const auto& e : entries)
				if (e.first == FOLDERS[k] && !e.second.empty())
					mFolders[k] = githubRequest(endpoints().api + mSystem.repo + "/git/trees/" + e.second);
	}

	void Source::finish()
	{
		// the folders: all three, or the cache stays as it was
		bool all = mRoot && mRootDone && mFolders[BOXART];
		std::vector<std::pair<std::string, std::string>> lists[KINDS];
		for (int k = 0; k < KINDS && all; k++)
		{
			if (!mFolders[k])
				continue; // a system without title screens, say
			bool truncated = false;
			all = mFolders[k]->status() == HttpReq::REQ_SUCCESS && parseTree(mFolders[k]->getContent(), lists[k], &truncated);
			if (truncated)
			{
				LOG(LogWarning) << "Libretro-thumbnails: GitHub listed part of " << mSystem.repo << "/" << FOLDERS[k];
			}
		}
		if (all)
		{
			std::string text;
			for (int k = 0; k < KINDS; k++)
				for (const auto& e : lists[k])
					if (e.first.size() > 4 && Utils::String::toLower(e.first.substr(e.first.size() - 4)) == ".png")
						text += std::string(1, KIND_LETTERS[k]) + "\t" + e.first.substr(0, e.first.size() - 4) + "\n";
			loadNames(text, mArt);
			writeFile(mCache + "/" + mSystem.repo + ".names", text);
		}
		else if (mRoot && mRootDone && mRoot->status() == HttpReq::REQ_SUCCESS)
		{
			LOG(LogWarning) << "Libretro-thumbnails: part of " << mSystem.repo << "'s list did not come";
		}

		if (mDat)
		{
			if (mDat->status() == HttpReq::REQ_SUCCESS)
			{
				std::unordered_map<uint32_t, std::string> byCrc;
				parseDat(mDat->getContent(), byCrc);
				if (!byCrc.empty())
				{
					std::string text;
					char hex[16];
					for (const auto& e : byCrc)
					{
						snprintf(hex, sizeof(hex), "%08X\t", (unsigned)e.first);
						text += hex + e.second + "\n";
					}
					loadCrcs(text, mArt);
					writeFile(mCache + "/" + mSystem.repo + ".crc", text);
				}
			}
			else
			{
				LOG(LogWarning) << "Libretro-thumbnails: no No-Intro DAT for " << mSystem.name << " (" << mDat->getErrorMsg() << ")";
			}
		}

		LOG(LogInfo) << "Libretro-thumbnails: " << mSystem.repo << ": " << mArt.folders[BOXART].size() << " box arts, "
			<< mArt.folders[SNAP].size() << " screenshots, " << mArt.folders[TITLE].size() << " title screens"
			<< (mArt.indexed ? "" : " (no list: names tried)") << ", " << mArt.byCrc.size() << " CRCs, "
			<< mArt.db.size() << " database covers";

		mRoot.reset();
		for (auto& f : mFolders)
			f.reset();
		mDat.reset();
	}
}
