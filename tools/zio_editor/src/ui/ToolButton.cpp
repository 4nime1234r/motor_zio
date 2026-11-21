#include "ui/ToolButton.h"

#include <cmath>

namespace zio_editor
{

namespace
{

using zio::ui::Color;
using zio::ui::Painter;
using zio::ui::Vec2;

constexpr float kPi = 3.14159265f;

void arrowHead(Painter& painter, Vec2 tip, Vec2 direction, float size, Color color)
{
    const Vec2 side(-direction.y, direction.x);
    const Vec2 base(tip.x - direction.x * size, tip.y - direction.y * size);
    painter.triangle(tip, {base.x + side.x * size * 0.6f, base.y + side.y * size * 0.6f},
                     {base.x - side.x * size * 0.6f, base.y - side.y * size * 0.6f}, color);
}

void drawMove(Painter& painter, Vec2 c, float r, Color color, float t)
{
    const float head = r * 0.4f;
    const Vec2 dirs[4] = {{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}};
    for (const Vec2& d : dirs)
    {
        const Vec2 tip(c.x + d.x * r, c.y + d.y * r);
        painter.line(c, {tip.x - d.x * head * 0.5f, tip.y - d.y * head * 0.5f}, color, t);
        arrowHead(painter, tip, d, head, color);
    }
}

void drawRotate(Painter& painter, Vec2 c, float r, Color color, float t)
{
    const float radius = r * 0.82f;
    const float start  = -kPi * 0.15f;
    const float end    = kPi * 1.45f;
    constexpr int kSegments = 22;
    Vec2 previous(c.x + std::cos(start) * radius, c.y + std::sin(start) * radius);
    for (int i = 1; i <= kSegments; ++i)
    {
        const float a = start + (end - start) * static_cast<float>(i) / static_cast<float>(kSegments);
        const Vec2 point(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius);
        painter.line(previous, point, color, t);
        previous = point;
    }
    const Vec2 tangent(-std::sin(end), std::cos(end));
    arrowHead(painter, {previous.x + tangent.x * r * 0.18f, previous.y + tangent.y * r * 0.18f}, tangent, r * 0.46f, color);
}

void drawScale(Painter& painter, Vec2 c, float r, Color color, float t)
{
    const float box = r * 0.9f;
    const float x0  = c.x - r;
    const float y0  = c.y + r - box;
    painter.line({x0, y0}, {x0 + box, y0}, color, t);
    painter.line({x0 + box, y0}, {x0 + box, y0 + box}, color, t);
    painter.line({x0 + box, y0 + box}, {x0, y0 + box}, color, t);
    painter.line({x0, y0 + box}, {x0, y0}, color, t);
    const Vec2 from(c.x - r * 0.1f, c.y + r * 0.1f);
    const Vec2 tip(c.x + r, c.y - r);
    painter.line(from, tip, color, t);
    arrowHead(painter, tip, {0.7071f, -0.7071f}, r * 0.5f, color);
}

void drawFocus(Painter& painter, Vec2 c, float r, Color color, float t)
{
    constexpr int kSegments = 20;
    const float radius      = r * 0.55f;
    Vec2 previous(c.x + radius, c.y);
    for (int i = 1; i <= kSegments; ++i)
    {
        const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(kSegments);
        const Vec2 point(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius);
        painter.line(previous, point, color, t);
        previous = point;
    }
    const float gap = r * 0.9f;
    painter.line({c.x - r, c.y}, {c.x - gap * 0.5f, c.y}, color, t);
    painter.line({c.x + gap * 0.5f, c.y}, {c.x + r, c.y}, color, t);
    painter.line({c.x, c.y - r}, {c.x, c.y - gap * 0.5f}, color, t);
    painter.line({c.x, c.y + gap * 0.5f}, {c.x, c.y + r}, color, t);
}

}

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

    const Vec2 center = rect().center();
    const float r     = size_ * 0.28f;
    const float line  = std::fmax(1.5f, size_ * 0.07f);
    switch (icon_)
    {
    case ToolIcon::Move:
        drawMove(painter, center, r, tint, line);
        break;
    case ToolIcon::Rotate:
        drawRotate(painter, center, r, tint, line);
        break;
    case ToolIcon::Scale:
        drawScale(painter, center, r, tint, line);
        break;
    case ToolIcon::Focus:
        drawFocus(painter, center, r, tint, line);
        break;
    case ToolIcon::Play:
        painter.triangle({center.x - r * 0.65f, center.y - r}, {center.x - r * 0.65f, center.y + r},
                         {center.x + r * 0.95f, center.y}, tint);
        break;
    case ToolIcon::Stop:
        painter.fillRoundRect({center.x - r * 0.8f, center.y - r * 0.8f, r * 1.6f, r * 1.6f}, tint, 2.f);
        break;
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
