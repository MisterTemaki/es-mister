#pragma once
#ifndef ES_CORE_UTILS_INPUT_RECORDER_H
#define ES_CORE_UTILS_INPUT_RECORDER_H

#include <string>
#include <unordered_set>
#include <vector>

// What a piece of work on one thread asked the outside world, recorded: a setting's value, whether a file exists,
// a file's size and date, a theme file read. The theme cache (ThemeData) records a theme's loading this way, and
// keeps its result only while every answer is still the same. Nothing is recorded on a thread that did not start
// a record, and the calls below cost one thread-local read there.
namespace InputRecorder
{
	struct Entry
	{
		char kind;			// see the kinds below
		std::string name;
		std::string value;
	};

	// E exists, D isDirectory, G isRegularFile, Z getFileSize (Utils::FileSystem); R ResourceManager::fileExists;
	// S B I F a setting (string, bool, int, float); X a theme file read (its size and date)
	struct Record
	{
		std::vector<Entry> entries;
		std::unordered_set<std::string> seen;	// kind and name: each question once
		bool uncacheable = false;		// something answered that cannot be asked again (the time, say)
	};

	inline Record*& current()
	{
		static thread_local Record* record = nullptr;
		return record;
	}

	inline bool recording() { return current() != nullptr; }

	inline void add(char kind, const std::string& name, const std::string& value)
	{
		Record* record = current();
		if (record == nullptr)
			return;

		std::string key(1, kind);
		key += name;
		if (record->seen.insert(key).second)
			record->entries.push_back({ kind, name, value });
	}

	inline void markUncacheable()
	{
		if (Record* record = current())
			record->uncacheable = true;
	}

	// records this thread's questions into record until the scope ends
	class Scope
	{
	public:
		explicit Scope(Record* record) : mPrevious(current()) { current() = record; }
		~Scope() { current() = mPrevious; }

	private:
		Record* mPrevious;
	};
}

#endif // ES_CORE_UTILS_INPUT_RECORDER_H
