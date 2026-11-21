#pragma once

#include <cstdint>
#include <memory>

#include "zio/core/Platform.h"

namespace zio::ui
{

class ZIO_API Icon
{
public:
    ~Icon();

    Icon(const Icon&)            = delete;
    Icon& operator=(const Icon&) = delete;

    static std::shared_ptr<Icon> fromAlpha(const std::uint8_t* alpha, int width, int height);

    std::uint32_t texture() const { return texture_; }

private:
    Icon() = default;

    std::uint32_t texture_ = 0;
};

}
