#pragma once

#include <cmath>

#include "zio/math/Quaternion.h"
#include "zio/math/Vec3.h"
#include "zio/scene3d/Node3D.h"

namespace zio_editor
{

class EulerCache
{
public:
    zio::math::Vec3& anglesFor(const zio::scene3d::Node3D& node)
    {
        const zio::math::Quaternion q = node.rotation();
        if (&node != node_ || !same(q, lastRotation_))
        {
            node_         = &node;
            lastRotation_ = q;
            angles_       = q.toEulerDegrees();
            angles_.x     = cleanZero(angles_.x);
            angles_.y     = cleanZero(angles_.y);
            angles_.z     = cleanZero(angles_.z);
        }
        return angles_;
    }

    void apply(zio::scene3d::Node3D& node)
    {
        node.setRotation(zio::math::Quaternion::fromEulerDegrees(angles_));
        node_         = &node;
        lastRotation_ = node.rotation();
    }

    void reset() { node_ = nullptr; }

private:
    static float cleanZero(float v)
    {
        return std::fabs(v) < 5e-4f ? 0.f : v;
    }

    static bool same(const zio::math::Quaternion& a, const zio::math::Quaternion& b)
    {
        constexpr float kEps = 1e-6f;
        return std::fabs(a.x - b.x) < kEps && std::fabs(a.y - b.y) < kEps && std::fabs(a.z - b.z) < kEps &&
               std::fabs(a.w - b.w) < kEps;
    }

    const zio::scene3d::Node3D* node_ = nullptr;
    zio::math::Quaternion lastRotation_;
    zio::math::Vec3 angles_;
};

}
