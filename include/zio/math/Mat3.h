#pragma once

#include <cmath>

#include "zio/math/Vec2.h"

namespace zio::math
{

struct Mat3
{
    float a = 1.f, b = 0.f;
    float c = 0.f, d = 1.f;
    float tx = 0.f, ty = 0.f;

    static constexpr Mat3 identity() { return Mat3{}; }

    static constexpr Mat3 translation(Vec2 t) { return Mat3{1.f, 0.f, 0.f, 1.f, t.x, t.y}; }

    static constexpr Mat3 scale(Vec2 s) { return Mat3{s.x, 0.f, 0.f, s.y, 0.f, 0.f}; }

    static Mat3 rotationDegrees(float degrees)
    {
        const float radians = degrees * (3.14159265358979323846f / 180.f);
        const float s        = std::sin(radians);
        const float cosv      = std::cos(radians);
        return Mat3{cosv, s, -s, cosv, 0.f, 0.f};
    }

    constexpr Mat3 operator*(const Mat3& rhs) const
    {
        return Mat3{
            a * rhs.a + b * rhs.c,
            a * rhs.b + b * rhs.d,
            c * rhs.a + d * rhs.c,
            c * rhs.b + d * rhs.d,
            a * rhs.tx + b * rhs.ty + tx,
            c * rhs.tx + d * rhs.ty + ty,
        };
    }

    constexpr Vec2 transformPoint(Vec2 p) const { return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty}; }
};

}
