#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/EditorContext.h"
#include "panels/ContentBrowserModel.h"

namespace zio_editor
{

class FileExplorerPanel
{
public:
    void draw(EditorContext& context);

private:
    struct FolderNode
    {
        std::string name;
        std::string display;
        std::string relativePath;
        std::vector<FolderNode> children;
    };

    void rescan(const ProjectManager& project);
    void scan(const ProjectManager& project, FolderNode& node, int depth) const;
    void drawTree(const ProjectManager& project, const FolderNode& node);
    void drawFiles(EditorContext& context);

    ContentBrowserModel model_;
    std::filesystem::path boundRoot_;
    std::string selected_;
    FolderNode tree_;
    double lastScan_  = -10.0;
    float treeWidth_  = 220.f;
    float treeHeight_ = 170.f;
    bool listView_    = false;
};

}
