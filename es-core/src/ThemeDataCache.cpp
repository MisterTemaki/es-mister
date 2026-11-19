// The theme cache: what ThemeData::loadFile built for a system, kept on the SD card and loaded again without parsing
// the theme while everything the loading asked for gives the same answers.
//
// On the MiSTer, the Carbon theme is parsed once per system at every start (32 times, ~0.13 s of the ARM each, the
// startup's largest part): the same XML files, for systems that differ by a few variables. A system's file here
// holds what loadFile built (views, elements, properties, storyboards, subsets, the variables the theme set) and
// what its loading asked, recorded by InputRecorder on its thread:
//   - every theme file it read (X: size and date),
//   - every file it looked for (R, E, D, G, Z: ResourceManager::fileExists, Utils::FileSystem::...),
//   - every setting it read (S, B, I, F: the subsets' choices, ShowHelpPrompts, ThemeSet, ForceSmallScreen...),
//   - the variables it started with (the system's, the screen's, every top-level setting's), as a hash;
// and what it started from: the ES binary (size and date), the theme's file and folder, the ThemeData's own
// settings (color set, language, region...), the screen's size, the processor's family. The file is used only when
// all of them are still the same; otherwise the theme is parsed and the file written again. A parse that asked
// something that cannot be asked again (a theme expression with the time or a translation in it) is not kept.
//
// Variables the theme set are kept as the difference from the ones it started with: loaded, they go over the
// variables this start begins with, as the parse would have set them. One variable changes by itself between
// starts, settings.LastSystem (the system ES was on): it is left out of the hash unless a theme file, or another
// variable's value, names it. ES_THEME_CACHE names the folder (es-mister.sh: cache/themes); nothing is done
// without it. Files are written by a thread of their own, 10 s after the first is ready (the startup's reads go
// first), each to a temporary name and then renamed: a file is whole or absent. A file that does not read back
// whole (its checksum) is not used.

#include "ThemeData.h"

#include "anim/ThemeAnimation.h"
#include "anim/ThemeStoryboard.h"
#include "renderers/Renderer.h"
#include "resources/ResourceManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/InputRecorder.h"
#include "utils/Platform.h"
#include "utils/StartupTrace.h"
#include "Log.h"
#include "Settings.h"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <typeinfo>
#include <fcntl.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include <unistd.h>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

namespace
{
	const char MAGIC[4] = { 'E', 'S', 'T', 'C' };
	const char END[4] = { 'E', 'N', 'D', '!' };
	// the file's layout and what it holds; a change of either (or of what ThemeData holds) takes a new version
	const uint32_t VERSION = 1;

	// variables that change by themselves between starts: left out of the hash unless something names them
	const char* const VOLATILE_VARIABLES[] = { "settings.LastSystem" };

	std::string cacheFolder()
	{
		static const std::string folder = []
		{
			const char* env = getenv("ES_THEME_CACHE");
			std::string dir = env != nullptr ? env : "";
			while (dir.size() > 1 && dir.back() == '/')
				dir.pop_back();
			return dir;
		}();
		return folder;
	}

	// the ES binary that built the file: another one may parse a theme otherwise
	const std::string& binaryStamp()
	{
#ifdef MISTER_PERF
		// the measurement build under valgrind (/proc/self/exe is valgrind's): the stamp given
		if (const char* given = getenv("ES_THEME_CACHE_STAMP"))
		{
			static const std::string stamp = given;
			return stamp;
		}
#endif
		static const std::string stamp = ThemeData::fileStamp("/proc/self/exe");
		return stamp;
	}

	uint64_t fnv64(uint64_t h, const void* data, size_t size)
	{
		const unsigned char* p = (const unsigned char*)data;
		for (size_t i = 0; i < size; i++)
		{
			h ^= p[i];
			h *= 1099511628211ull;
		}
		return h;
	}

	const uint64_t FNV_START = 1469598103934665603ull;

	uint64_t fnv64(uint64_t h, const std::string& s)
	{
		h = fnv64(h, s.data(), s.size());
		const char separator = 0;
		return fnv64(h, &separator, 1);
	}

	uint64_t variablesHash(const ThemeVariables& variables, const std::vector<std::string>& leftOut)
	{
		uint64_t h = FNV_START;
		for (const auto& variable : variables)
		{
			if (std::find(leftOut.cbegin(), leftOut.cend(), variable.first) != leftOut.cend())
				continue;
			h = fnv64(h, variable.first);
			h = fnv64(h, variable.second);
		}
		return h;
	}

	// the ThemeData's own settings, set by its constructor (the parse does not change them)
	std::string screenKey()
	{
		return std::to_string(Renderer::getScreenWidth()) + "x" + std::to_string(Renderer::getScreenHeight()) + " " +
			Utils::Platform::getArchString();
	}

	std::string fileFor(const std::string& system, const std::map<std::string, std::string>& sysDataMap, const std::string& path)
	{
		auto name = sysDataMap.find("system.name");
		std::string systemName = name != sysDataMap.cend() ? name->second : system;
		uint64_t h = fnv64(fnv64(fnv64(FNV_START, path), system), systemName);

		std::string safe;
		for (char c : systemName)
			safe += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';

		char hex[24];
		snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);
		return cacheFolder() + "/" + safe + "-" + hex + ".estc";
	}

	// --- bytes ---

	// Strings go to a table, each once, and are written as their number in it; counts and numbers as varints. The
	// file is the header, the table, then the rest
	struct Out
	{
		std::string data;
		std::unordered_map<std::string, uint32_t> index;
		std::vector<const std::string*> table;

		void raw(const void* p, size_t n) { data.append((const char*)p, n); }
		void u8(uint8_t v) { raw(&v, 1); }
		void var(uint32_t v) { while (v >= 0x80) { u8((uint8_t)(v | 0x80)); v >>= 7; } u8((uint8_t)v); }
		void u32(uint32_t v) { var(v); }
		void i32(int32_t v) { var((uint32_t)v); }
		void u64(uint64_t v) { raw(&v, 8); }
		void f32(float v) { raw(&v, 4); }
		void f64(double v) { raw(&v, 8); }
		void str(const std::string& s)
		{
			auto it = index.find(s);
			if (it == index.end())
			{
				it = index.emplace(s, (uint32_t)table.size()).first;
				table.push_back(&it->first);
			}
			var(it->second);
		}
		void strings(const std::vector<std::string>& list) { u32((uint32_t)list.size()); for (const auto& s : list) str(s); }

		// the table, as its own bytes
		std::string tableBytes() const
		{
			Out t;
			t.var((uint32_t)table.size());
			for (const std::string* s : table)
			{
				t.var((uint32_t)s->size());
				t.raw(s->data(), s->size());
			}
			return t.data;
		}
	};

	struct In
	{
		const char* p;
		const char* end;
		bool ok = true;
		std::vector<std::pair<const char*, uint32_t>> table;

		In(const std::string& data) : p(data.data()), end(data.data() + data.size()) { }

		bool raw(void* out, size_t n)
		{
			if (!ok || (size_t)(end - p) < n)
			{
				ok = false;
				memset(out, 0, n);
				return false;
			}
			memcpy(out, p, n);
			p += n;
			return true;
		}
		uint8_t u8() { uint8_t v; raw(&v, 1); return v; }
		uint32_t var()
		{
			uint32_t v = 0;
			for (int shift = 0; shift < 35; shift += 7)
			{
				if (!ok || p >= end)
				{
					ok = false;
					return 0;
				}
				const uint8_t b = (uint8_t)*p++;
				v |= (uint32_t)(b & 0x7F) << shift;
				if ((b & 0x80) == 0)
					return v;
			}
			ok = false;
			return 0;
		}
		uint32_t u32() { return var(); }
		int32_t i32() { return (int32_t)var(); }
		uint64_t u64() { uint64_t v; raw(&v, 8); return v; }
		float f32() { float v; raw(&v, 4); return v; }
		double f64() { double v; raw(&v, 8); return v; }
		// a count of things that each take at least `least` bytes: more than the bytes left is a damaged file
		uint32_t count(size_t least = 1)
		{
			uint32_t n = var();
			if (ok && (size_t)n * least > (size_t)(end - p))
			{
				ok = false;
				return 0;
			}
			return n;
		}
		bool readTable()
		{
			const uint32_t n = count();
			table.reserve(n);
			for (uint32_t i = 0; i < n && ok; i++)
			{
				const uint32_t size = count();
				if (!ok)
					break;
				table.emplace_back(p, size);
				p += size;
			}
			return ok;
		}
		// the table's string, without a copy (compared, or made a string where one is kept)
		bool view(const char*& text, uint32_t& size)
		{
			const uint32_t i = var();
			if (!ok || i >= table.size())
			{
				ok = false;
				text = "";
				size = 0;
				return false;
			}
			text = table[i].first;
			size = table[i].second;
			return true;
		}
		std::string str()
		{
			const char* text;
			uint32_t size;
			view(text, size);
			return std::string(text, size);
		}
		bool same(const std::string& s)
		{
			const char* text;
			uint32_t size;
			return view(text, size) && size == s.size() && memcmp(text, s.data(), size) == 0;
		}
		std::vector<std::string> strings()
		{
			std::vector<std::string> list;
			uint32_t n = count();
			list.reserve(n);
			for (uint32_t i = 0; i < n && ok; i++)
				list.push_back(str());
			return list;
		}
	};

	// --- the writer ---

	class Writer
	{
	public:
		void push(const std::string& file, std::string&& data)
		{
			std::unique_lock<std::mutex> lock(mLock);
			mQueue.emplace_back(file, std::move(data));
			if (!mStarted)
			{
				mStarted = true;
				std::thread(&Writer::run, this).detach();
			}
			mWake.notify_one();
		}

	private:
		void run()
		{
#ifndef _WIN32
			setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
#endif
			// the startup's own reads first: the files wait for it
			std::this_thread::sleep_for(std::chrono::seconds(10));
			Utils::FileSystem::createDirectory(cacheFolder());

			for (;;)
			{
				std::pair<std::string, std::string> job;
				{
					std::unique_lock<std::mutex> lock(mLock);
					mWake.wait(lock, [this] { return !mQueue.empty(); });
					job = std::move(mQueue.front());
					mQueue.pop_front();
				}

#ifndef _WIN32
				// never the SD card's last 64 MB (the files are ~130 KB each)
				struct statvfs vfs;
				if (statvfs(cacheFolder().c_str(), &vfs) == 0 && (uint64_t)vfs.f_bavail * vfs.f_frsize < 64ull * 1024 * 1024)
					continue;
#endif

				const std::string temporary = job.first + ".tmp";
				FILE* file = fopen(temporary.c_str(), "wb");
				if (file == nullptr)
					continue;
				const bool written = fwrite(job.second.data(), 1, job.second.size(), file) == job.second.size();
				const bool closed = fclose(file) == 0;
				if (written && closed)
					rename(temporary.c_str(), job.first.c_str());
				else
					unlink(temporary.c_str());
			}
		}

		std::mutex mLock;
		std::condition_variable mWake;
		std::deque<std::pair<std::string, std::string>> mQueue;
		bool mStarted = false;
	};

	Writer& writer()
	{
		static Writer* instance = new Writer(); // never destroyed: its thread may still run when ES ends
		return *instance;
	}

	bool readFile(const std::string& path, std::string& data)
	{
		FILE* file = fopen(path.c_str(), "rb");
		if (file == nullptr)
			return false;
		bool ok = fseek(file, 0, SEEK_END) == 0;
		long size = ok ? ftell(file) : -1;
		ok = ok && size > 0 && size < 64 * 1024 * 1024 && fseek(file, 0, SEEK_SET) == 0;
		if (ok)
		{
			data.resize((size_t)size);
			ok = fread(&data[0], 1, (size_t)size, file) == (size_t)size;
		}
		fclose(file);
		return ok;
	}

	std::string floatText(double value)
	{
		char text[32];
		snprintf(text, sizeof(text), "%a", value);
		return text;
	}

	// the answer a recorded question gets now
	std::string answerNow(char kind, const std::string& name)
	{
		switch (kind)
		{
		case 'X': return ThemeData::fileStamp(name);
		case 'R': return ResourceManager::getInstance()->fileExists(name) ? "1" : "0";
		case 'E': return Utils::FileSystem::exists(name) ? "1" : "0";
		case 'D': return Utils::FileSystem::isDirectory(name) ? "1" : "0";
		case 'G': return Utils::FileSystem::isRegularFile(name) ? "1" : "0";
		case 'Z': return std::to_string(Utils::FileSystem::getFileSize(name));
		case 'S': return Settings::getInstance()->getString(name);
		case 'B': return Settings::getInstance()->getBool(name) ? "1" : "0";
		case 'I': return std::to_string(Settings::getInstance()->getInt(name));
		case 'F': return floatText(Settings::getInstance()->getFloat(name));
		}
		return std::string("\x01unknown");
	}
}

std::string ThemeData::fileStamp(const std::string& path)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return "-";

	char text[64];
#ifdef _WIN32
	snprintf(text, sizeof(text), "%lld:%lld", (long long)st.st_size, (long long)st.st_mtime);
#else
	snprintf(text, sizeof(text), "%lld:%lld.%09ld", (long long)st.st_size, (long long)st.st_mtim.tv_sec, (long)st.st_mtim.tv_nsec);
#endif
	return text;
}

// what ThemeData holds, written and read in a fixed order (a nested class: it sees ThemeData's private parts)
struct ThemeData::CacheCodec
{
	typedef ThemeData::ThemeElement::Property Property;

	enum AnimationKind : uint8_t { FLOAT_ANIMATION, COLOR_ANIMATION, VECTOR2_ANIMATION, VECTOR4_ANIMATION, STRING_ANIMATION, PATH_ANIMATION,
		BOOL_ANIMATION, SOUND_ANIMATION };

	// a property's parts, those that are not zero: a bit each first
	enum PropertyParts : uint8_t { UNION_PART = 1, STRING_PART = 2, PAIR_PART = 4, RECT_PART = 8 };

	static void writeProperty(Out& out, const Property& p)
	{
		uint64_t bits;
		memcpy(&bits, &p.f, sizeof(bits)); // the union as it is, whichever member it holds
		const float pair[2] = { p.v.x(), p.v.y() };
		const float rect[4] = { p.r.x(), p.r.y(), p.r.z(), p.r.w() };
		const float zeros[4] = { 0, 0, 0, 0 };

		uint8_t parts = 0;
		if (bits != 0) parts |= UNION_PART;
		if (!p.s.empty()) parts |= STRING_PART;
		if (memcmp(pair, zeros, sizeof(pair)) != 0) parts |= PAIR_PART;
		if (memcmp(rect, zeros, sizeof(rect)) != 0) parts |= RECT_PART;

		out.u8((uint8_t)p.type);
		out.u8(parts);
		if (parts & UNION_PART) out.u64(bits);
		if (parts & STRING_PART) out.str(p.s);
		if (parts & PAIR_PART) out.raw(pair, sizeof(pair));
		if (parts & RECT_PART) out.raw(rect, sizeof(rect));
	}

	static void readProperty(In& in, Property& p)
	{
		p.type = (Property::PropertyType)in.u8();
		const uint8_t parts = in.u8();
		uint64_t bits = 0;
		if (parts & UNION_PART) bits = in.u64();
		memcpy(&p.f, &bits, sizeof(bits));
		if (parts & STRING_PART) p.s = in.str(); else p.s.clear();
		float pair[2] = { 0, 0 };
		if (parts & PAIR_PART) in.raw(pair, sizeof(pair));
		p.v = Vector2f(pair[0], pair[1]);
		float rect[4] = { 0, 0, 0, 0 };
		if (parts & RECT_PART) in.raw(rect, sizeof(rect));
		p.r = Vector4f(rect[0], rect[1], rect[2], rect[3]);
	}

	static bool animationKind(const ThemeAnimation* a, uint8_t& kind)
	{
		const std::type_info& t = typeid(*a);
		if (t == typeid(ThemeFloatAnimation)) kind = FLOAT_ANIMATION;
		else if (t == typeid(ThemeColorAnimation)) kind = COLOR_ANIMATION;
		else if (t == typeid(ThemeVector2Animation)) kind = VECTOR2_ANIMATION;
		else if (t == typeid(ThemeVector4Animation)) kind = VECTOR4_ANIMATION;
		else if (t == typeid(ThemeStringAnimation)) kind = STRING_ANIMATION;
		else if (t == typeid(ThemePathAnimation)) kind = PATH_ANIMATION;
		else if (t == typeid(ThemeBoolAnimation)) kind = BOOL_ANIMATION;
		else if (t == typeid(ThemeSoundAnimation)) kind = SOUND_ANIMATION;
		else
			return false; // a kind this file does not know: nothing kept
		return true;
	}

	static ThemeAnimation* newAnimation(uint8_t kind)
	{
		switch (kind)
		{
		case FLOAT_ANIMATION: return new ThemeFloatAnimation();
		case COLOR_ANIMATION: return new ThemeColorAnimation();
		case VECTOR2_ANIMATION: return new ThemeVector2Animation();
		case VECTOR4_ANIMATION: return new ThemeVector4Animation();
		case STRING_ANIMATION: return new ThemeStringAnimation();
		case PATH_ANIMATION: return new ThemePathAnimation();
		case BOOL_ANIMATION: return new ThemeBoolAnimation();
		case SOUND_ANIMATION: return new ThemeSoundAnimation();
		}
		return nullptr;
	}

	static bool writeStoryboard(Out& out, const ThemeStoryboard& sb)
	{
		out.str(sb.eventName);
		out.i32(sb.repeat);
		out.i32(sb.repeatAt);
		out.u32((uint32_t)sb.animations.size());
		for (const ThemeAnimation* a : sb.animations)
		{
			uint8_t kind;
			if (a == nullptr || !animationKind(a, kind))
				return false;
			out.u8(kind);
			out.str(a->propertyName);
			out.i32(a->duration);
			out.i32(a->begin);
			out.u8(a->autoReverse ? 1 : 0);
			out.i32(a->repeat);
			out.i32((int32_t)a->easingMode);
			out.u8(a->enabled ? 1 : 0);
			out.str(a->enabledExpression);
			writeProperty(out, a->from);
			writeProperty(out, a->to);
		}
		return true;
	}

	static void readStoryboard(In& in, ThemeStoryboard& sb)
	{
		sb.eventName = in.str();
		sb.repeat = in.i32();
		sb.repeatAt = in.i32();
		uint32_t n = in.count(8);
		sb.animations.reserve(n);
		for (uint32_t i = 0; i < n && in.ok; i++)
		{
			ThemeAnimation* a = newAnimation(in.u8());
			if (a == nullptr)
			{
				in.ok = false;
				return;
			}
			sb.animations.push_back(a);
			a->propertyName = in.str();
			a->duration = in.i32();
			a->begin = in.i32();
			a->autoReverse = in.u8() != 0;
			a->repeat = in.i32();
			a->easingMode = (ThemeAnimation::EasingMode)in.i32();
			a->enabled = in.u8() != 0;
			a->enabledExpression = in.str();
			readProperty(in, a->from);
			readProperty(in, a->to);
		}
	}

	static bool writeElement(Out& out, const ThemeElement& e)
	{
		out.i32(e.extra);
		out.str(e.type);

		// the properties in their hash table's opposite order, with its size: read back into a table of the same
		// size in the file's order, they come out in the same order (each new one goes first in its bucket)
		std::vector<const std::pair<const std::string, Property>*> properties;
		properties.reserve(e.properties.size());
		for (const auto& p : e.properties)
			properties.push_back(&p);
		out.u32((uint32_t)e.properties.bucket_count());
		out.u32((uint32_t)properties.size());
		for (auto it = properties.rbegin(); it != properties.rend(); ++it)
		{
			out.str((*it)->first);
			writeProperty(out, (*it)->second);
		}

		out.u32((uint32_t)e.mStoryBoards.size());
		for (const auto& sb : e.mStoryBoards)
		{
			out.str(sb.first);
			if (sb.second == nullptr || !writeStoryboard(out, *sb.second))
				return false;
		}

		out.u32((uint32_t)e.children.size());
		for (const auto& child : e.children)
		{
			out.str(child.first);
			if (!writeElement(out, child.second))
				return false;
		}
		return true;
	}

	static void readElement(In& in, ThemeElement& e, int depth = 0)
	{
		if (depth > 64)
		{
			in.ok = false;
			return;
		}

		e.extra = in.i32();
		e.type = in.str();

		const uint32_t buckets = in.u32();
		const uint32_t count = in.count(3);
		if (buckets > 0 && buckets >= count)
			e.properties.rehash(buckets);
		for (uint32_t i = 0; i < count && in.ok; i++)
		{
			std::string name = in.str();
			Property property;
			readProperty(in, property);
			e.properties.emplace(std::move(name), std::move(property));
		}

		const uint32_t storyboards = in.count(4);
		for (uint32_t i = 0; i < storyboards && in.ok; i++)
		{
			std::string name = in.str();
			ThemeStoryboard*& sb = e.mStoryBoards[name];
			delete sb;
			sb = new ThemeStoryboard();
			readStoryboard(in, *sb);
		}

		const uint32_t children = in.count(4);
		e.children.reserve(children); // no copy of the ones read when the vector grows (ThemeElement copies deep)
		for (uint32_t i = 0; i < children && in.ok; i++)
		{
			e.children.emplace_back(in.str(), ThemeElement());
			readElement(in, e.children.back().second, depth + 1);
		}
	}

	static bool writeView(Out& out, const ThemeView& v)
	{
		out.u32((uint32_t)v.elements.size());
		for (const auto& e : v.elements)
		{
			out.str(e.first);
			if (!writeElement(out, e.second))
				return false;
		}
		out.strings(v.orderedKeys);
		out.str(v.baseType);
		out.str(v.extraTransition);
		out.str(v.extraTransitionDirection);
		out.f32(v.extraTransitionSpeed);
		out.strings(v.baseTypes);
		out.str(v.displayName);
		out.u8(v.isCustomView ? 1 : 0);
		return true;
	}

	static void readView(In& in, ThemeView& v)
	{
		const uint32_t elements = in.count(4);
		auto hint = v.elements.end();
		for (uint32_t i = 0; i < elements && in.ok; i++)
		{
			// written in the map's order: each goes at the end
			hint = v.elements.emplace_hint(v.elements.end(), in.str(), ThemeElement());
			readElement(in, hint->second);
		}
		v.orderedKeys = in.strings();
		v.baseType = in.str();
		v.extraTransition = in.str();
		v.extraTransitionDirection = in.str();
		v.extraTransitionSpeed = in.f32();
		v.baseTypes = in.strings();
		v.displayName = in.str();
		v.isCustomView = in.u8() != 0;
	}

	static void writeValue(Out& out, const Utils::MathExpr::Value& v)
	{
		out.u32(v.type);
		out.f64(v.number);
		out.str(v.string);
	}

	static Utils::MathExpr::Value readValue(In& in)
	{
		Utils::MathExpr::Value v;
		v.type = in.u32();
		v.number = in.f64();
		v.string = in.str();
		return v;
	}

	static bool sameValue(const Utils::MathExpr::Value& a, const Utils::MathExpr::Value& b)
	{
		return a.type == b.type && memcmp(&a.number, &b.number, sizeof(a.number)) == 0 && a.string == b.string;
	}

	// the ThemeData's own settings (its constructor's), which the parse reads and does not change
	static std::string ownSettings(const ThemeData& t)
	{
		return t.mColorset + '\n' + t.mIconset + '\n' + t.mMenu + '\n' + t.mSystemview + '\n' + t.mGamelistview + '\n' + t.mLanguage + '\n' +
			t.mLangAndRegion + '\n' + t.mRegion;
	}
};

static uint32_t layoutCheck()
{
	return (uint32_t)(sizeof(void*) * 16 + sizeof(ThemeData::ThemeElement::Property));
}

void ThemeData::saveToCache(const std::string& system, const std::map<std::string, std::string>& sysDataMap, const std::string& path,
	const InputRecorder::Record& inputs, const ThemeVariables& startVariables, const Utils::MathExpr::ValueMap& startEvaluator)
{
	if (cacheFolder().empty() || inputs.uncacheable)
		return;

	// a variable that changes by itself is left out of the hash unless a theme file or another variable names it
	std::vector<std::string> leftOut;
	for (const char* name : VOLATILE_VARIABLES)
	{
		const char* bare = strchr(name, '.') != nullptr ? strchr(name, '.') + 1 : name;
		bool named = false;
		for (const auto& e : inputs.entries)
		{
			if (e.kind == 'X' && ThemeFileCache::getInstance().getXmlDocument(e.name).find(bare) != std::string::npos)
			{
				named = true;
				break;
			}
		}
		for (const auto& v : startVariables)
		{
			if (named)
				break;
			if (v.first != name && v.second.find(bare) != std::string::npos)
				named = true;
		}
		if (!named)
			leftOut.push_back(name);
	}

	Out out;
	out.u32(layoutCheck());

	// what it started from
	out.str(binaryStamp());
	out.str(path);
	out.str(system);
	out.str(CacheCodec::ownSettings(*this));
	out.str(screenKey());
	out.strings(leftOut);
	out.u64(variablesHash(startVariables, leftOut));

	// what it asked
	out.u32((uint32_t)inputs.entries.size());
	for (const auto& e : inputs.entries)
	{
		out.u8((uint8_t)e.kind);
		out.str(e.name);
		out.str(e.value);
	}

	// what it built
	out.strings(std::vector<std::string>(mPaths.cbegin(), mPaths.cend()));
	out.f32(mVersion);
	out.str(mDefaultView);
	out.str(mDefaultTransition);
	out.str(mSystemThemeFolder);
	out.u8(mPerGameOverrideTmp ? 1 : 0);

	// the variables the theme set or took away, from those it started with
	std::vector<std::string> removed;
	std::vector<const std::pair<const std::string, std::string>*> set;
	for (const auto& v : startVariables)
		if (mVariables.find(v.first) == mVariables.cend())
			removed.push_back(v.first);
	for (const auto& v : mVariables)
	{
		auto start = startVariables.find(v.first);
		if (start == startVariables.cend() || start->second != v.second)
			set.push_back(&v);
	}
	out.strings(removed);
	out.u32((uint32_t)set.size());
	for (const auto* v : set)
	{
		out.str(v->first);
		out.str(v->second);
	}

	std::vector<std::string> removedValues;
	std::vector<const std::pair<const std::string, Utils::MathExpr::Value>*> setValues;
	for (const auto& v : startEvaluator)
		if (mEvaluatorVariables.find(v.first) == mEvaluatorVariables.cend())
			removedValues.push_back(v.first);
	for (const auto& v : mEvaluatorVariables)
	{
		auto start = startEvaluator.find(v.first);
		if (start == startEvaluator.cend() || !CacheCodec::sameValue(start->second, v.second))
			setValues.push_back(&v);
	}
	out.strings(removedValues);
	out.u32((uint32_t)setValues.size());
	for (const auto* v : setValues)
	{
		out.str(v->first);
		CacheCodec::writeValue(out, v->second);
	}

	out.u32((uint32_t)mSubsets.size());
	for (const auto& s : mSubsets)
	{
		out.str(s.subset);
		out.str(s.name);
		out.str(s.displayName);
		out.strings(s.appliesTo);
		out.str(s.subSetDisplayName);
	}

	out.u32((uint32_t)mSubsetDefault.size());
	for (const auto& s : mSubsetDefault)
	{
		out.str(s.first);
		out.str(s.second);
	}

	out.u32((uint32_t)mViews.size());
	for (const auto& view : mViews)
	{
		out.str(view.first);
		if (!CacheCodec::writeView(out, view.second))
			return; // an animation kind the file does not know: nothing kept
	}

	// the file: magic and version, the strings' table, the rest, a checksum of all that, the end mark
	std::string file(MAGIC, 4);
	const uint32_t version = VERSION;
	file.append((const char*)&version, 4);
	file += out.tableBytes();
	file += out.data;
	const uint64_t check = fnv64(FNV_START, file.data(), file.size());
	file.append((const char*)&check, 8);
	file.append(END, 4);

	writer().push(fileFor(system, sysDataMap, path), std::move(file));
}

bool ThemeData::loadFromCache(const std::string& system, const std::map<std::string, std::string>& sysDataMap, const std::string& path)
{
	if (cacheFolder().empty())
		return false;

	const std::string file = fileFor(system, sysDataMap, path);
	std::string data;
	if (!readFile(file, data))
	{
		StartupTrace::mark("theme cache", "none: " + system);
		return false;
	}

	auto miss = [&](const std::string& why)
	{
		StartupTrace::mark("theme cache", "miss: " + system + " (" + why + ")");
		return false;
	};

	// whole: magic, version, the end mark, and the checksum of everything before it
	uint32_t version = 0;
	if (data.size() < 20 || memcmp(data.data(), MAGIC, 4) != 0 || memcmp(data.data() + data.size() - 4, END, 4) != 0)
		return miss("damaged");
	memcpy(&version, data.data() + 4, 4);
	if (version != VERSION)
		return miss("version");
	uint64_t check;
	memcpy(&check, data.data() + data.size() - 12, 8);
	if (check != fnv64(FNV_START, data.data(), data.size() - 12))
		return miss("damaged");

	In in(data);
	in.p += 8;
	in.end -= 12;
	if (!in.readTable() || in.u32() != layoutCheck())
		return miss("damaged");

	if (!in.same(binaryStamp()))
		return miss("binary");
	if (!in.same(path) || !in.same(system))
		return miss("theme");
	if (!in.same(CacheCodec::ownSettings(*this)))
		return miss("theme settings");
	if (!in.same(screenKey()))
		return miss("screen");
	const std::vector<std::string> leftOut = in.strings();
	if (in.u64() != variablesHash(mVariables, leftOut))
		return miss("variables");

	const uint32_t entries = in.count(3);
	for (uint32_t i = 0; i < entries && in.ok; i++)
	{
		const char kind = (char)in.u8();
		const std::string name = in.str();
		if (in.ok && !in.same(answerNow(kind, name)))
			return miss(std::string(1, kind) + " " + name);
	}
	if (!in.ok)
		return miss("damaged");

	// everything the same: what the parse built, read into a ThemeData of its own first, and the variables' changes
	// apart (nothing changed here if the file does not read whole)
	ThemeData loaded(true);
	const std::vector<std::string> paths = in.strings();
	loaded.mPaths.assign(paths.cbegin(), paths.cend());
	loaded.mVersion = in.f32();
	loaded.mDefaultView = in.str();
	loaded.mDefaultTransition = in.str();
	loaded.mSystemThemeFolder = in.str();
	loaded.mPerGameOverrideTmp = in.u8() != 0;

	const std::vector<std::string> removed = in.strings();
	std::vector<std::pair<std::string, std::string>> set;
	const uint32_t setCount = in.count(2);
	set.reserve(setCount);
	for (uint32_t i = 0; i < setCount && in.ok; i++)
	{
		std::string name = in.str();
		set.emplace_back(std::move(name), in.str());
	}

	const std::vector<std::string> removedValues = in.strings();
	std::vector<std::pair<std::string, Utils::MathExpr::Value>> setValues;
	const uint32_t setValuesCount = in.count(11);
	setValues.reserve(setValuesCount);
	for (uint32_t i = 0; i < setValuesCount && in.ok; i++)
	{
		std::string name = in.str();
		setValues.emplace_back(std::move(name), CacheCodec::readValue(in));
	}

	const uint32_t subsets = in.count(5);
	for (uint32_t i = 0; i < subsets && in.ok; i++)
	{
		std::string subset = in.str(), name = in.str(), displayName = in.str();
		std::vector<std::string> appliesTo = in.strings();
		std::string subSetDisplayName = in.str();
		loaded.mSubsets.emplace_back(subset, name, displayName, subSetDisplayName);
		loaded.mSubsets.back().appliesTo = std::move(appliesTo);
	}

	const uint32_t defaults = in.count(2);
	for (uint32_t i = 0; i < defaults && in.ok; i++)
	{
		std::string name = in.str();
		loaded.mSubsetDefault[name] = in.str();
	}

	const uint32_t views = in.count(2);
	for (uint32_t i = 0; i < views && in.ok; i++)
	{
		std::string name = in.str();
		auto inserted = loaded.mViews.insert(name, ThemeView());
		CacheCodec::readView(in, inserted.first->second);
	}

	if (!in.ok || in.p != in.end)
		return miss("damaged");

	mPaths = std::move(loaded.mPaths);
	mVersion = loaded.mVersion;
	mDefaultView = std::move(loaded.mDefaultView);
	mDefaultTransition = std::move(loaded.mDefaultTransition);
	mSystemThemeFolder = std::move(loaded.mSystemThemeFolder);
	mPerGameOverrideTmp = loaded.mPerGameOverrideTmp;
	mSubsets = std::move(loaded.mSubsets);
	mSubsetDefault = std::move(loaded.mSubsetDefault);
	mViews = std::move(loaded.mViews);

	// the variables this start began with, changed as the parse changed them
	for (const auto& name : removed)
		mVariables.erase(name);
	for (auto& v : set)
		mVariables[v.first] = std::move(v.second);
	for (const auto& name : removedValues)
		mEvaluatorVariables.erase(name);
	for (auto& v : setValues)
		mEvaluatorVariables[v.first] = std::move(v.second);

	StartupTrace::mark("theme cache", "hit: " + system);
	return true;
}
