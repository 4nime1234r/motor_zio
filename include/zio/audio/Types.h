#pragma once

#include <cstddef>
#include <cstdint>

namespace zio::audio
{

using VoiceHandle                                   = std::uint32_t;
inline constexpr VoiceHandle kInvalidVoiceHandle = 0;

enum class Category : std::uint8_t
{
    Sfx = 0,
    Music,
};

inline constexpr std::size_t kCategoryCount = static_cast<std::size_t>(Category::Music) + 1;

struct PlayParams
{
    Category category = Category::Sfx;
    float volume = 1.f;
    float pitch = 1.f;
    bool loop = false;
};

}
