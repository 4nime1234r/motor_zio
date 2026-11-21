#pragma once

#include <variant>

#include "zio/math/Vec3.h"

namespace zio::physics3d
{

struct SphereShape
{
    float radius = 0.5f;
};

struct BoxShape
{
    math::Vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

using Shape3D = std::variant<SphereShape, BoxShape>;

}
