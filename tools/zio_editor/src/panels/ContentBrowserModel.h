#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "project/ProjectManager.h"

namespace zio_editor
{

class ContentBrowserModel
{
public:
    struct Entry
    {
        std::string name;
        std::string displayName;
        std::string relativePath;
        bool isDirectory = false;
        bool isScene     = false;
        bool isMesh      = false;
    };

    static constexpr const char* kRoot = "assets";

    static std::string displayName(std::string_view parentDir, std::string_view name);

    void reset() { currentDir_ = kRoot; }

    const std::string& currentDir() const { return currentDir_; }
    bool canGoUp() const { return currentDir_ != kRoot; }

    bool goUp();
    bool enter(const ProjectManager& project, std::string_view folderName);
    bool goTo(const ProjectManager& project, std::string_view relativeDir);

    std::vector<std::pair<std::string, std::string>> breadcrumbs() const;
    std::vector<Entry> list(const ProjectManager& project, std::string* error = nullptr) const;

private:
    std::string currentDir_ = kRoot;
};

}
