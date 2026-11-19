// Paths: a game's path given to the launch command through /bin/sh comes back as it was (A1: a name with
// ` or | ran commands), and getCanonicalPath on deep or odd paths (A2: more than 32 folders wrote past
// its array).
#include "TestMain.h"
#include "utils/FileSystemUtil.h"

#include <cstdio>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
	// what /bin/sh makes of "printf %s <escaped>": the path, if nothing in it was read as shell syntax
	std::string throughShell(const std::string& escaped, const std::string& folder)
	{
		const std::string command = "cd '" + folder + "' && printf %s " + escaped;
		FILE* pipe = popen(command.c_str(), "r");
		if (pipe == nullptr)
			return "<popen failed>";
		std::string out;
		char buffer[512];
		size_t n;
		while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
			out.append(buffer, n);
		pclose(pipe);
		return out;
	}
}

TEST(escaped_path_comes_back_through_the_shell)
{
	char folderTemplate[] = "/tmp/es-paths-test-XXXXXX";
	const std::string folder = mkdtemp(folderTemplate);

	std::vector<std::string> names = {
		"/media/fat/games/NES/Super Mario Bros. (World).nes",
		"/media/fat/games/NES/Tom & Jerry's Café (USA) [!] #1 ~ $HOME.nes",
		"/media/fat/games/NES/a`touch PWNED1`|touch PWNED2;b.nes",   // A1
		"/media/fat/games/NES/x $(touch PWNED3) \"q\" 'q' n .nes",
		"/media/fat/games/NES/tab\there, star * and ? glob [a-z] {a,b}.nes",
		"/media/fat/games/NES/line\nfeed.nes",
		"/media/fat/games/NES/ドラゴンクエスト (Japan).nes",
		"-starts with a dash.nes",
	};
	// every byte that can be in a file name, one after another (the old escaping left a character right after
	// an escaped one as it was). Not "\\": ES takes it for a folder separator everywhere (getGenericPath makes
	// it "/"), and the MiSTer's exFAT does not allow it in names.
	std::string all;
	for (int c = 1; c < 256; c++)
		if (c != '/' && c != '\\')
			all += (char)c;
	names.push_back("/x/" + all);
	names.push_back("/x/" + std::string("!!$$``||;;&&((") + all);

	for (const std::string& name : names)
	{
		const std::string escaped = Utils::FileSystem::getEscapedPath(name);
		// printf %s with a first argument starting with "-" is still the argument
		CHECK_EQ(throughShell(escaped, folder), name);
	}

	// nothing ran
	for (const char* pwned : { "PWNED1", "PWNED2", "PWNED3" })
		CHECK(access((folder + "/" + pwned).c_str(), F_OK) != 0);
	rmdir(folder.c_str());
}

TEST(canonical_path)
{
	using Utils::FileSystem::getCanonicalPath;
	CHECK_EQ(getCanonicalPath("/a/./b"), std::string("/a/b"));
	CHECK_EQ(getCanonicalPath("/a/b/../c"), std::string("/a/c"));
	CHECK_EQ(getCanonicalPath("/a/b/./../c/./d"), std::string("/a/c/d"));
	CHECK_EQ(getCanonicalPath("/a/.hidden/b"), std::string("/a/.hidden/b"));
	CHECK_EQ(getCanonicalPath("/a/b../c"), std::string("/a/b../c"));
	CHECK_EQ(getCanonicalPath("/a/..b/c/./d"), std::string("/a/..b/c/d"));
	CHECK_EQ(getCanonicalPath(":/resources/x.png"), std::string(":/resources/x.png"));
	// three dots or more are a name, not the folder above
	CHECK_EQ(getCanonicalPath("/a/.../c/./d"), std::string("/a/.../c/d"));
	CHECK_EQ(getCanonicalPath("/a/..../c/./d"), std::string("/a/..../c/d"));
	CHECK_EQ(getCanonicalPath("/a/b/.../../c/./d"), std::string("/a/b/c/d"));

	// deep: 200 folders and a "." (A2: 32 were the most)
	std::string deep, expected;
	for (int i = 0; i < 200; i++)
	{
		deep += "/d" + std::to_string(i);
		expected += "/d" + std::to_string(i);
		if (i == 150)
			deep += "/.";
	}
	CHECK_EQ(getCanonicalPath(deep + "/./x"), expected + "/x");
	CHECK_EQ(getCanonicalPath(deep + "/../x"), expected.substr(0, expected.rfind('/')) + "/x");
}

namespace
{
	// isAbsolute and getCanonicalPath as they were before they skipped work (getGenericPath's copy; the loop for a
	// "/." that is no "." folder): the new ones must give the same for any path
	bool oldIsAbsolute(const std::string& _path)
	{
		if (_path.size() >= 2 && _path[0] == ':' && _path[1] == '/')
			return true;
		std::string path = Utils::FileSystem::getGenericPath(_path);
		return ((path.size() > 0) && (path[0] == '/'));
	}

	std::string oldCanonicalPath(const std::string& _path)
	{
		using namespace Utils::FileSystem;
		if (_path.size() >= 2 && _path[0] == ':' && _path[1] == '/')
			return _path;
		std::string path = oldIsAbsolute(_path) ? getGenericPath(_path) : (exists(_path) ? getAbsolutePath(_path) : getGenericPath(_path));
		if (path.find("/.") == std::string::npos)
			return path;
		std::vector<int> indexes;
		int index = -1;
		std::vector<char> buffer(path.size() + 1);
		char* fp = buffer.data();
		int pos = 0;
		bool pointset = false;
		bool twopointset = false;
		for (int i = 0; i < (int)path.size(); i++)
		{
			char c = path[i];
			if (c == '/' || c == '\\')
			{
				if (twopointset)
				{
					if (index > 0)
						pos = indexes[--index] + 1;
					twopointset = false;
					pointset = false;
					continue;
				}
				else if (pointset)
				{
					pos = index >= 0 ? indexes[index] + 1 : 0;
					pointset = false;
					continue;
				}
				else
				{
					if (++index >= (int)indexes.size())
						indexes.resize(index + 1);
					indexes[index] = pos;
				}
				fp[pos++] = '/';
				continue;
			}
			else if (c == '.')
			{
				if (twopointset)
					twopointset = false;
				else if (pointset)
				{
					twopointset = true;
					pointset = false;
				}
				else if (index >= 0 && i > 0 && (path[i - 1] == '/' || path[i - 1] == '\\'))
					pointset = true;
			}
			else
			{
				twopointset = false;
				pointset = false;
			}
			fp[pos++] = c;
		}
		fp[pos] = 0;
		return fp;
	}
}

TEST(absolute_and_canonical_paths_as_before)
{
	// every path of up to 7 characters of these, and some longer ones made of them at random
	const char alphabet[] = { '/', '\\', '.', 'a', '?', ':', '\0' };
	const int letters = sizeof(alphabet);
	std::vector<std::string> paths;
	for (int length = 0; length <= 7; length++)
	{
		std::vector<int> digits(length, 0);
		while (true)
		{
			std::string path;
			for (int d : digits)
				path += alphabet[d];
			paths.push_back(path);
			int i = 0;
			while (i < length && ++digits[i] == letters)
				digits[i++] = 0;
			if (i == length)
				break;
		}
	}
	unsigned seed = 12345;
	for (int n = 0; n < 200000; n++)
	{
		std::string path;
		const int length = 8 + (seed = seed * 1103515245 + 12345) % 40;
		for (int i = 0; i < length; i++)
			path += alphabet[((seed = seed * 1103515245 + 12345) >> 16) % letters];
		paths.push_back(path);
	}
	for (const char* real : { "/media/fat/es-mister-720p/.emulationstation/themes/es-theme-carbon/./art/logos/snes.svg",
		"/media/fat/es-mister-720p/.emulationstation/themes/es-theme-carbon/art/../art/x.png", "/a/.hidden/./b/", "\\\\?\\/x", "///" })
		paths.push_back(real);

	int different = 0;
	for (const std::string& path : paths)
	{
		if (Utils::FileSystem::isAbsolute(path) != oldIsAbsolute(path) || Utils::FileSystem::getCanonicalPath(path) != oldCanonicalPath(path))
		{
			if (different++ < 5)
				CHECK_EQ(Utils::FileSystem::getCanonicalPath(path) + (Utils::FileSystem::isAbsolute(path) ? " abs" : " rel"), oldCanonicalPath(path) + (oldIsAbsolute(path) ? " abs" : " rel"));
		}
	}
	CHECK_EQ(different, 0);
	CHECK_EQ(paths.size() > 1000000, true);
}
