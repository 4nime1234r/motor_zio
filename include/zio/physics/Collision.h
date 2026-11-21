#pragma once

#include "zio/core/Platform.h"
#include "zio/math/Vec2.h"
#include "zio/physics/Shapes.h"

namespace zio::physics
{

struct Manifold
{
    bool colliding      = false;
    math::Vec2 normal   = math::Vec2::zero();
    float penetration   = 0.f;
};

ZIO_API Manifold testAABBvsAABB(const AABB& a, const AABB& b);
ZIO_API Manifold testCircleVsCircle(const Circle& a, const Circle& b);
ZIO_API Manifold testAABBvsCircle(const AABB& box, const Circle& circle);

ZIO_API Manifold testShapes(const Shape& a, const Shape& b);

}
