#pragma once

namespace zio::renderer
{

struct Color
{
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;

    static constexpr Color white() { return {1.f, 1.f, 1.f, 1.f}; }
    static constexpr Color black() { return {0.f, 0.f, 0.f, 1.f}; }
    static constexpr Color transparent() { return {1.f, 1.f, 1.f, 0.f}; }
};

}
