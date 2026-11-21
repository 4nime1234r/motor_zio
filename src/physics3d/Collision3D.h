#pragma once

#include "zio/math/Quaternion.h"
#include "zio/math/Vec3.h"
#include "zio/physics3d/Shapes3D.h"

namespace zio::physics3d::detail
{

struct Pose
{
    math::Vec3 position       = math::Vec3::zero();
    math::Quaternion rotation = math::Quaternion::identity();
};

struct ContactPoint
{
    math::Vec3 point  = math::Vec3::zero();
    float penetration = 0.f;
};

struct Manifold
{
    math::Vec3 normal = math::Vec3::zero();
    int count         = 0;
    ContactPoint points[4];
};

struct Aabb
{
    math::Vec3 min;
    math::Vec3 max;

    bool overlaps(const Aabb& o) const
    {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y && min.z <= o.max.z &&
               max.z >= o.min.z;
    }
};

bool collide(const Shape3D& a, const Pose& pa, const Shape3D& b, const Pose& pb, Manifold& out);
Aabb computeAabb(const Shape3D& shape, const Pose& pose);
bool raycastShape(const Shape3D& shape, const Pose& pose, math::Vec3 origin, math::Vec3 direction, float maxDistance,
                  float& distance, math::Vec3& normal);

}
