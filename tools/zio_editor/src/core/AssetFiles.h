#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "project/ProjectManager.h"

namespace zio_editor
{

struct AssetEntry
{
    std::string relative;
    std::string name;
    std::string extension;
};

class AssetFiles
{
public:
    static constexpr std::size_t kMaxEntries = 5000;

    static std::vector<AssetEntry> scan(const std::filesystem::path& projectRoot);

    static std::string className(std::string_view fileName);
    static std::string folderFor(std::string_view fileName);
    static bool isValidName(std::string_view name, std::string* reason = nullptr);

    static bool create(const ProjectManager& project, std::string_view name, std::string* relativeOut,
                       std::string* error = nullptr);
    static bool remove(const ProjectManager& project, std::string_view relative, std::string* error = nullptr);
};

}
