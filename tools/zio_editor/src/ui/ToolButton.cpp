#include "ui/ToolButton.h"

#include <cmath>

#include "ui/EditorIcons.h"

namespace zio_editor
{

using zio::ui::Painter;
using zio::ui::Vec2;

ToolButton::ToolButton(ToolIcon icon, float size) : icon_(icon), size_(size) {}

void ToolButton::draw(Painter& painter)
{
    using zio::ui::Color;
    const auto& t      = theme();
    const bool enabled = enabledInTree();
    const bool hot     = hovered() && enabled;

    Color tint = t.text;
    if (icon_ == ToolIcon::Play)
        tint = zio::ui::rgb(0x61e08c);
    else if (icon_ == ToolIcon::Stop)
        tint = active_ ? t.danger : t.textDim;
    else if (active_)
        tint = t.accentHover;
    if (!enabled)
        tint.a *= 0.4f;

    if (active_ || hot)
    {
        const Color fill = active_ ? Color{0.388f, 0.400f, 0.945f, 0.24f} : Color{1.f, 1.f, 1.f, 0.07f};
        painter.fillRoundRect(rect(), fill, 6.f);
    }

    const char* name = "move";
    switch (icon_)
    {
    case ToolIcon::Move:
        name = "move";
        break;
    case ToolIcon::Rotate:
        name = "rotate-3d";
        break;
    case ToolIcon::Scale:
        name = "scale-3d";
        break;
    case ToolIcon::Focus:
        name = "focus";
        break;
    case ToolIcon::Play:
        name = "play";
        break;
    case ToolIcon::Stop:
        name = "square";
        break;
    }
    if (const auto icon = EditorIcons::get(name))
    {
        const float side  = size_ * 0.58f;
        const Vec2 center = rect().center();
        painter.image(icon->texture(), {std::floor(center.x - side * 0.5f), std::floor(center.y - side * 0.5f), side, side},
                      tint);
    }
}

bool ToolButton::onMouseDown(const zio::ui::MouseEvent& event)
{
    if (event.button != zio::events::MouseButton::Left)
        return false;
    pressed_ = true;
    return true;
}

bool ToolButton::onMouseUp(const zio::ui::MouseEvent& event)
{
    if (event.button != zio::events::MouseButton::Left)
        return false;
    const bool fire = pressed_ && rect().contains(event.position);
    pressed_        = false;
    if (fire && onClick_)
        onClick_();
    return true;
}

}
