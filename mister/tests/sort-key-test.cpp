// Utils::String::ignoreCaseKey: two keys compare as compareIgnoreCase compares their names, so a list
// sorted by keys (FolderData::getChildrenListToDisplay, by name) comes out in the same order.
// ES_TEST_NAMES=FILE: also every pair of FILE's lines (a system's game names).
#include "TestMain.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <random>

namespace
{
	int sign(int value) { return (value > 0) - (value < 0); }

	// names as games have them: ASCII, accented letters, a few other scripts and stray bytes; a multi-byte
	// character is never cut at the end (compareIgnoreCase reads past the string there)
	std::vector<std::string> randomNames(size_t count, unsigned seed)
	{
		static const char* pieces[] = { "a", "B", "z", "Z", " ", "-", "(", ")", "0", "9", "'", "!", "_", "~",
			"\xC3\xA9", "\xC3\x89", "\xC3\xA7", "\xC3\x87", "\xC3\xB1", "\xC3\x91", "\xC3\xBC", "\xC3\x9C",
			"\xC5\x93", "\xCE\xB1", "\xCE\x91", "\xD0\xB4", "\xD0\x94", "\xE3\x81\x82", "\xE6\x97\xA5",
			"\xF0\x9F\x8E\xAE", "\x80", "\xBF", "the ", "The ", "A ", "an " };
		std::mt19937 random(seed);
		std::vector<std::string> names;
		for (size_t i = 0; i < count; i++)
		{
			std::string name;
			const int length = random() % 9;
			for (int j = 0; j < length; j++)
				name += pieces[random() % (sizeof(pieces) / sizeof(pieces[0]))];
			names.push_back(name);
		}
		// equal names and prefixes of each other
		names.push_back("Mario");
		names.push_back("mario");
		names.push_back("MARIO");
		names.push_back("Mario Kart");
		names.push_back("");
		return names;
	}

	void checkPairs(const std::vector<std::string>& names)
	{
		std::vector<std::u32string> keys;
		for (const std::string& name : names)
			keys.push_back(Utils::String::ignoreCaseKey(name));
		for (size_t i = 0; i < names.size(); i++)
			for (size_t j = 0; j < names.size(); j++)
			{
				const int expected = sign(Utils::String::compareIgnoreCase(names[i], names[j]));
				const int got = sign(keys[i].compare(keys[j]));
				if (expected != got)
				{
					CHECK_EQ(got, expected);
					fprintf(stderr, "    '%s' / '%s'\n", names[i].c_str(), names[j].c_str());
					return;
				}
			}
	}

	// the sort of getChildrenListToDisplay: stable, ascending or not, on pointers; by names, then by keys
	void checkSort(const std::vector<std::string>& names)
	{
		std::vector<std::u32string> keys;
		for (const std::string& name : names)
			keys.push_back(Utils::String::ignoreCaseKey(name));
		for (const bool ascending : { true, false })
		{
			std::vector<const std::string*> byName, byKey;
			for (const std::string& name : names)
				byName.push_back(&name);
			byKey = byName;
			std::stable_sort(byName.begin(), byName.end(), [ascending](const std::string* a, const std::string* b)
				{ return (Utils::String::compareIgnoreCase(*a, *b) < 0) == ascending; });
			std::stable_sort(byKey.begin(), byKey.end(), [&](const std::string* a, const std::string* b)
				{ return (keys[a - names.data()].compare(keys[b - names.data()]) < 0) == ascending; });
			CHECK(byName == byKey);
		}
	}
}

TEST(sort_key_compares_as_compare_ignore_case)
{
	for (unsigned seed = 1; seed <= 4; seed++)
	{
		const std::vector<std::string> names = randomNames(700, seed);
		checkPairs(names);
		checkSort(names);
	}

	const char* file = getenv("ES_TEST_NAMES");
	if (file != nullptr && *file != 0)
	{
		std::ifstream in(file);
		std::vector<std::string> names;
		for (std::string line; std::getline(in, line);)
			names.push_back(line);
		printf("  %zu names from %s\n", names.size(), file);
		checkPairs(names);
		checkSort(names);
	}
}
