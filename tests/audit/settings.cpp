#include "Settings.h"
#include "Paths.h"
#include "utils/FileSystemUtil.h"
#include "utils/Xml.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
extern int auditScriptEvents;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
  try
  {
    const std::string path = Paths::getUserEmulationStationPath() + "/es_settings.cfg";
    auto settings = Settings::getInstance();
    settings->setBool("UseFileCache", false);
    settings->setString("AuditSetting", u8"ação & <jogo>");
    // Block the final destination with a directory. No event may announce a successful save.
    std::filesystem::create_directory(path);
    require(!settings->saveFile(), "failed save reported success");
    require(auditScriptEvents == 0, "failed save fired change scripts");
    std::filesystem::remove(path);
    require(settings->saveFile(), "failed save lost its dirty flag; retry did not save");
    require(auditScriptEvents == 2, "successful save must fire both change scripts");
    const auto original = Utils::FileSystem::readAllText(path);
    settings->setString("AuditSetting", "second value");
    std::filesystem::create_directory(path + ".tmp");
    require(!settings->saveFile(), "blocked temporary path reported success");
    require(Utils::FileSystem::readAllText(path) == original, "failed save damaged existing settings");
    require(auditScriptEvents == 2, "failed replacement fired scripts");
    std::filesystem::remove(path + ".tmp");
    require(settings->saveFile(), "replacement retry failed");
    pugi::xml_document document;
    require(document.load_file(path.c_str()), "saved settings are invalid XML");
    require(std::string(document.child("config").find_child_by_attribute("string", "name", "AuditSetting").attribute("value").value()) == "second value", "setting did not round trip");
    require(!settings->saveFile(), "clean settings saved again");
    std::cout << "PASS settings: failed create, retry, preserved replacement, XML round trip, script events\n";
    return 0;
  }
  catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
