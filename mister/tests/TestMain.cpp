#include "TestMain.h"

#include <cstring>

namespace TestMain
{
	std::vector<Test>& tests()
	{
		static std::vector<Test> all;
		return all;
	}

	int& failures()
	{
		static int count = 0;
		return count;
	}

	void fail(const char* file, int line, const std::string& what)
	{
		failures()++;
		// at most a few lines a test: a broken invariant can fail thousands of times
		if (failures() <= 20)
			fprintf(stderr, "  FAILED %s:%d: %s\n", file, line, what.c_str());
	}
}

int main(int argc, char** argv)
{
	int failedTests = 0, ran = 0;
	for (const TestMain::Test& test : TestMain::tests())
	{
		if (argc > 1 && strstr(test.name, argv[1]) == nullptr)
			continue;
		const int before = TestMain::failures();
		printf("%s\n", test.name);
		fflush(stdout);
		test.run();
		ran++;
		if (TestMain::failures() != before)
		{
			failedTests++;
			printf("  FAILED (%d checks)\n", TestMain::failures() - before);
		}
		else
			printf("  ok\n");
	}
	printf("%d of %d tests passed\n", ran - failedTests, ran);
	return failedTests == 0 && ran > 0 ? 0 : 1;
}
