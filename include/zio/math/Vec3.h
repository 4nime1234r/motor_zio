#pragma once

#include <cmath>

namespace zio::math
{

struct Vec3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    constexpr Vec3 operator-(const Vec3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& rhs) const { return {x * rhs.x, y * rhs.y, z * rhs.z}; }

    constexpr Vec3& operator+=(const Vec3& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    constexpr bool operator==(const Vec3&) const = default;

    constexpr float dot(const Vec3& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }
    constexpr Vec3 cross(const Vec3& rhs) const
    {
        return {y * rhs.z - z * rhs.y, z * rhs.x - x * rhs.z, x * rhs.y - y * rhs.x};
    }

    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const
    {
        const float len = length();
        return len > 0.f ? *this * (1.f / len) : *this;
    }

    static constexpr Vec3 zero() { return {0.f, 0.f, 0.f}; }
    static constexpr Vec3 one() { return {1.f, 1.f, 1.f}; }
    static constexpr Vec3 unitX() { return {1.f, 0.f, 0.f}; }
    static constexpr Vec3 unitY() { return {0.f, 1.f, 0.f}; }
    static constexpr Vec3 unitZ() { return {0.f, 0.f, 1.f}; }
};

}
