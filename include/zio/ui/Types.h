#pragma once

#include <algorithm>
#include <cstdint>

#include "zio/events/Types.h"
#include "zio/math/Vec2.h"
#include "zio/renderer/Color.h"

namespace zio::ui
{

using math::Vec2;
using renderer::Color;

struct Rect
{
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;

    constexpr float right() const { return x + w; }
    constexpr float bottom() const { return y + h; }
    constexpr Vec2 position() const { return {x, y}; }
    constexpr Vec2 size() const { return {w, h}; }
    constexpr Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    constexpr bool empty() const { return w <= 0.f || h <= 0.f; }

    constexpr bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }

    constexpr Rect shrink(float left, float top, float right_, float bottom_) const
    {
        return {x + left, y + top, w - left - right_, h - top - bottom_};
    }
    constexpr Rect shrink(float amount) const { return shrink(amount, amount, amount, amount); }

    Rect intersect(const Rect& other) const
    {
        const float l = (std::max)(x, other.x);
        const float t = (std::max)(y, other.y);
        const float r = (std::min)(right(), other.right());
        const float b = (std::min)(bottom(), other.bottom());
        return {l, t, (std::max)(0.f, r - l), (std::max)(0.f, b - t)};
    }

    constexpr bool operator==(const Rect&) const = default;
};

struct Edges
{
    float left   = 0.f;
    float top    = 0.f;
    float right  = 0.f;
    float bottom = 0.f;

    static constexpr Edges all(float v) { return {v, v, v, v}; }
    static constexpr Edges symmetric(float horizontal, float vertical) { return {horizontal, vertical, horizontal, vertical}; }
    constexpr float horizontal() const { return left + right; }
    constexpr float vertical() const { return top + bottom; }
};

enum class Align : std::uint8_t
{
    Start,
    Center,
    End
};

enum class Cursor : std::uint8_t
{
    Arrow,
    IBeam,
    ResizeHorizontal,
    ResizeVertical,
    Hand
};

struct Modifiers
{
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;
};

struct MouseEvent
{
    Vec2 position;
    events::MouseButton button = events::MouseButton::Left;
    Modifiers modifiers;
    int clicks = 1;
};

struct KeyEvent
{
    events::KeyCode code = events::KeyCode::Unknown;
    Modifiers modifiers;
    bool repeat = false;
};

constexpr Color rgb(std::uint32_t hex, float alpha = 1.f)
{
    return {static_cast<float>((hex >> 16) & 0xFF) / 255.f, static_cast<float>((hex >> 8) & 0xFF) / 255.f,
            static_cast<float>(hex & 0xFF) / 255.f, alpha};
}

}
