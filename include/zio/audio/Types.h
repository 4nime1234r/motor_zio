#pragma once

#include <cstddef>
#include <cstdint>

#include "zio/core/Platform.h"
#include "zio/math/Vec3.h"

namespace zio::audio
{

using VoiceHandle                                   = std::uint32_t;
inline constexpr VoiceHandle kInvalidVoiceHandle = 0;

using BusHandle                                = std::uint32_t;
inline constexpr BusHandle kInvalidBusHandle = 0;

enum class Category : std::uint8_t
{
    Sfx = 0,
    Music,
};

inline constexpr std::size_t kCategoryCount = static_cast<std::size_t>(Category::Music) + 1;

enum class Effect : std::uint8_t
{
    Reverb,
    LowPass,
    HighPass,
};

struct PlayParams
{
    Category category = Category::Sfx;
    BusHandle bus     = kInvalidBusHandle;
    float volume = 1.f;
    float pitch = 1.f;
    bool loop = false;
    bool spatial        = false;
    math::Vec3 position = math::Vec3::zero();
    float minDistance   = 1.f;
    float maxDistance   = 50.f;
};

struct Listener
{
    math::Vec3 position = math::Vec3::zero();
    math::Vec3 forward  = {0.f, 0.f, -1.f};
    math::Vec3 up       = {0.f, 1.f, 0.f};
};

struct SpatialGains
{
    float left  = 1.f;
    float right = 1.f;
};

ZIO_API SpatialGains computeSpatialGains(const Listener& listener, const math::Vec3& emitter, float minDistance,
                                         float maxDistance);

}
