#include "ui/EditorIcons.h"

#include <map>

#include "ui/IconData.h"

namespace zio_editor
{

namespace
{

std::map<std::string, std::shared_ptr<zio::ui::Icon>>& table()
{
    static std::map<std::string, std::shared_ptr<zio::ui::Icon>> icons;
    return icons;
}

}

void EditorIcons::init()
{
    int count                = 0;
    const IconBitmap* bitmaps = iconBitmaps(count);
    for (int i = 0; i < count; ++i)
        if (auto icon = zio::ui::Icon::fromAlpha(bitmaps[i].alpha, kIconBitmapSize, kIconBitmapSize))
            table()[bitmaps[i].name] = std::move(icon);
}

void EditorIcons::shutdown()
{
    table().clear();
}

std::shared_ptr<zio::ui::Icon> EditorIcons::get(const std::string& name)
{
    const auto it = table().find(name);
    return it == table().end() ? nullptr : it->second;
}

}
