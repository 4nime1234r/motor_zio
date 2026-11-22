#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "zio/core/Platform.h"

namespace zio::assets
{

struct Image
{
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixelsRgba8;
};

ZIO_API std::optional<Image> decodeImageFile(std::string_view path);

ZIO_API std::optional<Image> decodeImageMemory(const std::uint8_t* data, std::size_t size);

using ImageLoadCallback = std::function<void(std::optional<Image>)>;

ZIO_API void loadImageAsync(std::string path, ImageLoadCallback onLoaded);

}
