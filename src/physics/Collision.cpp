#include "zio/physics/Collision.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace zio::physics
{

Manifold testAABBvsAABB(const AABB& a, const AABB& b)
{
    Manifold m;

    const float overlapX = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
    const float overlapY = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
    if (overlapX <= 0.f || overlapY <= 0.f)
        return m;

    const math::Vec2 delta = b.center() - a.center();
    m.colliding             = true;
    if (overlapX < overlapY)
    {
        m.normal      = {delta.x < 0.f ? -1.f : 1.f, 0.f};
        m.penetration = overlapX;
    }
    else
    {
        m.normal      = {0.f, delta.y < 0.f ? -1.f : 1.f};
        m.penetration = overlapY;
    }
    return m;
}

Manifold testCircleVsCircle(const Circle& a, const Circle& b)
{
    Manifold m;

    const math::Vec2 delta = b.center - a.center;
    const float distSq      = delta.dot(delta);
    const float radiusSum   = a.radius + b.radius;
    if (distSq >= radiusSum * radiusSum)
        return m;

    const float dist = std::sqrt(distSq);
    m.colliding        = true;
    m.normal          = dist > 0.f ? delta * (1.f / dist) : math::Vec2{1.f, 0.f};
    m.penetration     = radiusSum - dist;
    return m;
}

Manifold testAABBvsCircle(const AABB& box, const Circle& circle)
{
    Manifold m;

    const math::Vec2 closest{
        std::clamp(circle.center.x, box.min.x, box.max.x),
        std::clamp(circle.center.y, box.min.y, box.max.y),
    };
    const math::Vec2 delta = circle.center - closest;
    const float distSq      = delta.dot(delta);
    if (distSq >= circle.radius * circle.radius)
        return m;

    m.colliding = true;
    if (distSq > 0.f)
    {
        const float dist = std::sqrt(distSq);
        m.normal          = delta * (1.f / dist);
        m.penetration     = circle.radius - dist;
        return m;
    }

    const math::Vec2 toCircle    = circle.center - box.center();
    const math::Vec2 halfExtents = box.halfExtents();
    const float overlapX          = halfExtents.x - std::abs(toCircle.x);
    const float overlapY          = halfExtents.y - std::abs(toCircle.y);
    if (overlapX < overlapY)
    {
        m.normal      = {toCircle.x < 0.f ? -1.f : 1.f, 0.f};
        m.penetration = overlapX + circle.radius;
    }
    else
    {
        m.normal      = {0.f, toCircle.y < 0.f ? -1.f : 1.f};
        m.penetration = overlapY + circle.radius;
    }
    return m;
}

Manifold testShapes(const Shape& a, const Shape& b)
{
    return std::visit(
        [](const auto& shapeA, const auto& shapeB) -> Manifold {
            using TA = std::decay_t<decltype(shapeA)>;
            using TB = std::decay_t<decltype(shapeB)>;
            if constexpr (std::is_same_v<TA, AABB> && std::is_same_v<TB, AABB>)
            {
                return testAABBvsAABB(shapeA, shapeB);
            }
            else if constexpr (std::is_same_v<TA, Circle> && std::is_same_v<TB, Circle>)
            {
                return testCircleVsCircle(shapeA, shapeB);
            }
            else if constexpr (std::is_same_v<TA, AABB> && std::is_same_v<TB, Circle>)
            {
                return testAABBvsCircle(shapeA, shapeB);
            }
            else
            {
                Manifold m = testAABBvsCircle(shapeB, shapeA);
                m.normal    = m.normal * -1.f;
                return m;
            }
        },
        a, b);
}

}
