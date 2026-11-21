#pragma once

#include <memory>
#include <string>

#include "zio/ui/Icon.h"

namespace zio_editor
{

class EditorIcons
{
public:
    static void init();
    static void shutdown();
    static std::shared_ptr<zio::ui::Icon> get(const std::string& name);
};

}
