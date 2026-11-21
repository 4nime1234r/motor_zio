#pragma once

#include "zio/math/Mat4.h"
#include "zio/math/Vec3.h"

namespace zio::scene3d
{

struct Camera
{
    math::Vec3 position = {0.f, 0.f, 5.f};
    math::Vec3 target   = {0.f, 0.f, 0.f};
    math::Vec3 up       = {0.f, 1.f, 0.f};

    float fovYDegrees = 60.f;
    float nearZ       = 0.1f;
    float farZ        = 1000.f;

    math::Mat4 viewMatrix() const { return math::Mat4::lookAt(position, target, up); }
    math::Mat4 projectionMatrix(float aspect) const
    {
        return math::Mat4::perspective(fovYDegrees, aspect, nearZ, farZ);
    }
};

}
