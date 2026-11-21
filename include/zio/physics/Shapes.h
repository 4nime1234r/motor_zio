#pragma once

#include <variant>

#include "zio/math/Vec2.h"

namespace zio::physics
{

struct AABB
{
    math::Vec2 min;
    math::Vec2 max;

    static constexpr AABB fromHalfExtents(math::Vec2 halfExtents) { return {halfExtents * -1.f, halfExtents}; }
    static constexpr AABB fromMinMax(math::Vec2 min, math::Vec2 max) { return {min, max}; }

    constexpr math::Vec2 center() const { return (min + max) * 0.5f; }
    constexpr math::Vec2 halfExtents() const { return (max - min) * 0.5f; }

    constexpr AABB translated(math::Vec2 offset) const { return {min + offset, max + offset}; }

    constexpr bool contains(math::Vec2 p) const
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
    constexpr bool overlaps(const AABB& other) const
    {
        return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y;
    }
};

struct Circle
{
    math::Vec2 center;
    float radius = 0.f;

    constexpr Circle translated(math::Vec2 offset) const { return {center + offset, radius}; }
};

using Shape = std::variant<AABB, Circle>;

constexpr Shape translated(const Shape& shape, math::Vec2 offset)
{
    return std::visit([offset](const auto& s) -> Shape { return s.translated(offset); }, shape);
}

}
