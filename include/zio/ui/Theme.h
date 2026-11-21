#pragma once

#include <memory>

#include "zio/core/Platform.h"
#include "zio/ui/Font.h"
#include "zio/ui/Types.h"

namespace zio::ui
{

struct Theme
{
    std::shared_ptr<Font> font;

    Color page          = rgb(0x0f1117);
    Color panel         = rgb(0x15171f);
    Color deep          = rgb(0x090a0f);
    Color raised        = rgb(0x1c1f2a);
    Color raisedHover   = rgb(0x262a38);
    Color raisedPressed = rgb(0x30354a);
    Color border        = rgb(0xffffff, 0.06f);
    Color borderStrong  = rgb(0xffffff, 0.12f);
    Color accent        = rgb(0x6366f1);
    Color accentHover   = rgb(0x818cf8);
    Color accentDim     = rgb(0x6366f1, 0.35f);
    Color text          = rgb(0xffffff);
    Color textDim       = rgb(0x9ca3af);
    Color danger        = rgb(0xef4444);
    Color selection     = rgb(0x6366f1, 0.40f);

    float radius          = 5.f;
    float padding         = 8.f;
    float spacing         = 6.f;
    float controlHeight   = 30.f;
    float scrollbarWidth  = 10.f;
    float splitterSize    = 6.f;
    float tabHeight       = 32.f;
    float borderThickness = 1.f;
};

ZIO_API Theme makeZionTheme(std::shared_ptr<Font> font);

}
