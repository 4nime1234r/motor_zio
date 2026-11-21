#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::audio
{

struct Clip
{
    std::uint32_t channels   = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 16;
    std::vector<std::uint8_t> pcm;

    std::size_t frameCount() const
    {
        return channels == 0 ? 0 : pcm.size() / (static_cast<std::size_t>(channels) * sizeof(std::int16_t));
    }
};

ZIO_API std::optional<Clip> decodeAudioFile(std::string_view path);

using ClipLoadCallback = std::function<void(std::optional<Clip>)>;

ZIO_API void loadClipAsync(std::string path, ClipLoadCallback onLoaded);

}
