// Isolate platform settings and logging; all HTTP, string, ZIP and file operations are production code.
#include "Log.h"
#include "Paths.h"
#include "Settings.h"
#include "Scripting.h"
#include <cstdlib>
LogLevel Log::mReportingLevel = LogError;
bool Log::mEnabled = false;
Log::Log(LogLevel level) : mLevel(level) {}
Log::~Log() {}
std::ostringstream& Log::stream() { static thread_local std::ostringstream output; return output; }
#ifndef AUDIT_REAL_SETTINGS
bool Settings::_UseFileCache = false;
#else
int auditScriptEvents = 0;
namespace Scripting {
void fireEvent(const std::string&, const std::string&, const std::string&, const std::string&) { ++auditScriptEvents; }
}
namespace Utils { namespace Platform {
unsigned long long getTotalSystemMemory() { return 1024ULL * 1024 * 1024; }
} }
#endif
Paths* Paths::_instance = nullptr;
Paths::Paths() : mUserEmulationStationPath(std::getenv("ES_TEST_HOME")) {}
