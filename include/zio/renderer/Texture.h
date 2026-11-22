#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "zio/core/Platform.h"
#include "zio/render/Types.h"

namespace zio::renderer
{

struct Texture
{
    render::TextureHandle handle = render::kInvalidHandle;
    std::uint32_t width          = 0;
    std::uint32_t height         = 0;
};

using TextureLoadCallback = std::function<void(std::shared_ptr<Texture>)>;

class ZIO_API TextureCache
{
public:
    TextureCache();
    ~TextureCache();

    TextureCache(const TextureCache&)            = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    static TextureCache& instance();

    std::shared_ptr<Texture> loadSync(std::string_view path);
    std::shared_ptr<Texture> loadSync(std::string_view path, render::TextureFilter filter, render::TextureWrap wrap);

    std::shared_ptr<Texture> loadFromMemory(std::string_view key, const std::uint8_t* data, std::size_t size,
                                            render::TextureFilter filter, render::TextureWrap wrap);

    void loadAsync(std::string path, TextureLoadCallback onLoaded);

    std::size_t liveCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
