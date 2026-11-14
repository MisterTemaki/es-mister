#pragma once
#include "utils/ZipFile.h"
#include "utils/FileSystemUtil.h"
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>

namespace ThemeInstall {
// A Git archive is validated/extracted in an exclusive sibling directory. The installed
// theme is retained until the new tree is complete; a failed publication restores it.
inline bool install(const std::string& archive, const std::string& directory,
                    const std::string& name, std::string* recovery = nullptr)
{
    namespace fs = std::filesystem;
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    if (recovery) recovery->clear();
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos) return false;
    std::error_code error;
    fs::path root = fs::weakly_canonical(fs::u8path(directory), error);
    if (error) return false;
    fs::create_directories(root, error);
    if (error) return false;
    const auto destination = root / fs::u8path(name);
    auto status = fs::symlink_status(destination, error);
    if ((error && error != std::errc::no_such_file_or_directory) || fs::is_symlink(status)) return false;
    const bool hadOld = fs::exists(status);
    if (hadOld && !fs::is_directory(status)) return false;
    fs::path work;
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto candidate = root / (".es-theme-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (fs::create_directory(candidate, error)) { work = candidate; break; }
        if (error && error != std::errc::file_exists) return false;
    }
    if (work.empty()) return false;
    bool keep = false;
    struct Cleanup {
        const fs::path& path; const fs::path& root; bool& keep;
        ~Cleanup()
        {
            // Only remove the exclusive directory created above, never a computed archive path.
            if (!keep && path.parent_path() == root && path.filename().u8string().find(".es-theme-") == 0)
            { std::error_code ignored; fs::remove_all(path, ignored); }
            Utils::FileSystem::FileSystemCache::reset();
        }
    } cleanup{work, root, keep};
    const auto contents = work / "contents", previous = work / "previous";
    fs::create_directory(contents, error);
    if (error) return false;
    Utils::Zip::ZipFile zip;
    if (!zip.load(archive)) return false;
    const auto entries = zip.namelist();
    for (const auto& entry : entries)
        if (!Utils::Zip::ZipFile::isSafeExtractionPath(entry, contents.u8string())) return false;
    for (const auto& entry : entries)
    {
        if (!entry.empty() && entry.back() == '/')
        {
            fs::create_directories(contents / fs::u8path(entry), error);
            if (error) return false;
        }
        else if (!zip.extract(entry, contents.u8string())) return false;
    }
    fs::path staged;
    for (const auto& entry : fs::directory_iterator(contents, error))
    {
        if (entry.is_directory(error) && !entry.is_symlink(error))
        {
            if (!staged.empty()) return false;
            staged = entry.path();
        }
        if (error) return false;
    }
    if (error || staged.empty()) return false;
    if (hadOld)
    {
        fs::rename(destination, previous, error);
        if (error) return false;
    }
    fs::rename(staged, destination, error);
    if (error)
    {
        if (hadOld)
        {
            fs::rename(previous, destination, error);
            if (error)
            {
                keep = true; // never clean up the only remaining copy of the installed theme
                if (recovery) *recovery = previous.u8string();
            }
        }
        return false;
    }
    return true;
}
}
