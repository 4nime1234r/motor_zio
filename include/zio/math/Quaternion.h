#pragma once

#include <cmath>

#include "zio/math/Vec3.h"

namespace zio::math
{

struct Quaternion
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 1.f;

    constexpr Quaternion() = default;
    constexpr Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static constexpr Quaternion identity() { return Quaternion{}; }

    static Quaternion fromAxisAngle(Vec3 axis, float degrees)
    {
        const Vec3 normAxis = axis.normalized();
        if (normAxis.length() == 0.f)
            return identity();

        const float radians  = degrees * (3.14159265358979323846f / 180.f);
        const float halfSin  = std::sin(radians * 0.5f);
        const float halfCos  = std::cos(radians * 0.5f);
        return Quaternion{normAxis.x * halfSin, normAxis.y * halfSin, normAxis.z * halfSin, halfCos};
    }

    static Quaternion fromEulerDegrees(Vec3 d)
    {
        return fromAxisAngle({0.f, 1.f, 0.f}, d.y) * fromAxisAngle({1.f, 0.f, 0.f}, d.x) *
               fromAxisAngle({0.f, 0.f, 1.f}, d.z);
    }

    Vec3 toEulerDegrees() const
    {
        const float xx = x * x, yy = y * y, zz = z * z;
        const float r12 = 2.f * (y * z - w * x);
        constexpr float kRad = 180.f / 3.14159265358979323846f;
        float pitch, yaw, roll;
        if (std::fabs(r12) > 0.999999f)
        {
            pitch = std::asin(r12 > 0.f ? -1.f : 1.f);
            yaw   = std::atan2(-2.f * (x * z - w * y), 1.f - 2.f * (yy + zz));
            roll  = 0.f;
        }
        else
        {
            pitch = std::asin(-r12);
            yaw   = std::atan2(2.f * (x * z + w * y), 1.f - 2.f * (xx + yy));
            roll  = std::atan2(2.f * (x * y + w * z), 1.f - 2.f * (xx + zz));
        }
        return {pitch * kRad, yaw * kRad, roll * kRad};
    }

    constexpr Quaternion operator*(const Quaternion& rhs) const
    {
        return Quaternion{
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
            w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
        };
    }

    constexpr float dot(const Quaternion& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w; }
    float length() const { return std::sqrt(dot(*this)); }

    Quaternion normalized() const
    {
        const float len = length();
        return len > 0.f ? Quaternion{x / len, y / len, z / len, w / len} : identity();
    }

    constexpr Quaternion conjugate() const { return {-x, -y, -z, w}; }
    Quaternion inverse() const
    {
        const float lenSq = dot(*this);
        if (lenSq <= 0.f)
            return identity();
        const float invLenSq = 1.f / lenSq;
        return Quaternion{-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq};
    }

    constexpr Vec3 rotate(Vec3 v) const
    {
        const Vec3 qv{x, y, z};
        const Vec3 t = qv.cross(v) * 2.f;
        return v + t * w + qv.cross(t);
    }

    static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t)
    {
        float cosTheta = a.dot(b);
        Quaternion end  = b;
        if (cosTheta < 0.f)
        {
            cosTheta = -cosTheta;
            end       = Quaternion{-b.x, -b.y, -b.z, -b.w};
        }

        constexpr float kEpsilon = 1e-4f;
        if (cosTheta > 1.f - kEpsilon)
        {
            return Quaternion{
                a.x + (end.x - a.x) * t,
                a.y + (end.y - a.y) * t,
                a.z + (end.z - a.z) * t,
                a.w + (end.w - a.w) * t,
            }
                .normalized();
        }

        const float theta          = std::acos(cosTheta);
        const float sinTheta       = std::sin(theta);
        const float weightA        = std::sin((1.f - t) * theta) / sinTheta;
        const float weightB        = std::sin(t * theta) / sinTheta;
        return Quaternion{
            a.x * weightA + end.x * weightB,
            a.y * weightA + end.y * weightB,
            a.z * weightA + end.z * weightB,
            a.w * weightA + end.w * weightB,
        };
    }
};

}
