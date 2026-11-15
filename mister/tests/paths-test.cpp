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
