// es-core's Window (linked in by FileSystemUtil through Settings and Platform) calls a few of es-app's
// ApiSystem functions; the tests do not reach them, and link with these in their place.
#include "ApiSystem.h"

ApiSystem* ApiSystem::getInstance() { return nullptr; }
bool ApiSystem::mergeDrive(const std::string&) { return false; }
bool ApiSystem::prepareDrive(const std::string&, const std::string&) { return false; }
bool ApiSystem::ignoreDevicePermanently(const std::string&) { return false; }
