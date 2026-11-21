#pragma once

#include <filesystem>
#include <string>

#include "core/EditorContext.h"
#include "panels/ContentBrowserModel.h"

namespace zio_editor
{

class ContentBrowserPanel
{
public:
    void draw(EditorContext& context);

private:
    ContentBrowserModel model_;
    std::filesystem::path boundRoot_;
    std::string selected_;
};

}
