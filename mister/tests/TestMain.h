#pragma once
// A test framework of a few lines for es-mister-tests (ES has no tests of its own): TEST(name) { CHECK...; }
// registers a test; main runs them all, or those whose name holds argv[1]. Exit 0 when all passed.

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace TestMain
{
	struct Test
	{
		const char* name;
		std::function<void()> run;
	};

	std::vector<Test>& tests();
	int& failures();

	struct Register
	{
		Register(const char* name, std::function<void()> run) { tests().push_back({ name, run }); }
	};

	void fail(const char* file, int line, const std::string& what);

	template<typename A, typename B>
	void checkEqual(const A& a, const B& b, const char* textA, const char* textB, const char* file, int line)
	{
		if (a == b)
			return;
		std::ostringstream message;
		message << textA << " == " << textB << "\n      got:      [" << a << "]\n      expected: [" << b << "]";
		fail(file, line, message.str());
	}
}

#define TEST(name) \
	static void name(); \
	static TestMain::Register register_##name(#name, name); \
	static void name()

#define CHECK(condition) do { if (!(condition)) TestMain::fail(__FILE__, __LINE__, #condition); } while (0)
#define CHECK_EQ(a, b) TestMain::checkEqual((a), (b), #a, #b, __FILE__, __LINE__)
