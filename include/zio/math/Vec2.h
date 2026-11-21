#pragma once

#include <cmath>

namespace zio::math
{

struct Vec2
{
    float x = 0.f;
    float y = 0.f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& rhs) const { return {x + rhs.x, y + rhs.y}; }
    constexpr Vec2 operator-(const Vec2& rhs) const { return {x - rhs.x, y - rhs.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator*(const Vec2& rhs) const { return {x * rhs.x, y * rhs.y}; }

    constexpr Vec2& operator+=(const Vec2& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    constexpr bool operator==(const Vec2&) const  = default;

    constexpr float dot(const Vec2& rhs) const { return x * rhs.x + y * rhs.y; }
    float length() const { return std::sqrt(dot(*this)); }

    static constexpr Vec2 zero() { return {0.f, 0.f}; }
    static constexpr Vec2 one() { return {1.f, 1.f}; }
};

}
