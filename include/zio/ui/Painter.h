#pragma once

#include <cstdint>
#include <string_view>

#include "zio/core/Platform.h"
#include "zio/ui/Font.h"
#include "zio/ui/Types.h"

namespace zio::ui
{

class ZIO_API Painter
{
public:
    virtual ~Painter() = default;

    virtual void pushClip(const Rect& rect) = 0;
    virtual void popClip()                  = 0;

    virtual void fillRect(const Rect& rect, Color color)                          = 0;
    virtual void fillRoundRect(const Rect& rect, Color color, float radius)       = 0;
    virtual void line(Vec2 from, Vec2 to, Color color, float thickness)           = 0;
    virtual void triangle(Vec2 a, Vec2 b, Vec2 c, Color color)                    = 0;
    virtual void text(Font& font, Vec2 topLeft, std::string_view utf8, Color color) = 0;

    virtual void image(std::uint32_t texture, const Rect& rect, Color tint) {}

    void roundRectWithBorder(const Rect& rect, Color fill, Color border, float radius, float thickness)
    {
        fillRoundRect(rect, border, radius);
        const Rect inner = rect.shrink(thickness);
        if (!inner.empty())
            fillRoundRect(inner, fill, (std::max)(0.f, radius - thickness));
    }
};

}
