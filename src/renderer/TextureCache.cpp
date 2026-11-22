#include "zio/renderer/Texture.h"

#include <unordered_map>

#include "zio/assets/Image.h"
#include "zio/render/Device.h"

namespace zio::renderer
{

namespace
{

std::shared_ptr<Texture> makeTexture(const assets::Image& image, render::TextureFilter filter = render::TextureFilter::Point,
                                     render::TextureWrap wrap = render::TextureWrap::Clamp)
{
    render::TextureDesc desc{};
    desc.width       = image.width;
    desc.height      = image.height;
    desc.pixelsRgba8 = image.pixelsRgba8.data();
    desc.filter      = filter;
    desc.wrap        = wrap;

    const render::TextureHandle handle = render::Device::instance().createTexture(desc);
    if (handle == render::kInvalidHandle)
        return nullptr;

    return std::shared_ptr<Texture>(new Texture{handle, image.width, image.height}, [](Texture* t) {
        render::Device::instance().destroyTexture(t->handle);
        delete t;
    });
}

}

struct TextureCache::Impl
{
    std::unordered_map<std::string, std::weak_ptr<Texture>> cache;

    std::shared_ptr<Texture> findLive(const std::string& path)
    {
        auto it = cache.find(path);
        if (it == cache.end())
            return nullptr;
        return it->second.lock();
    }
};

TextureCache::TextureCache() : impl_(std::make_unique<Impl>()) {}
TextureCache::~TextureCache() = default;

TextureCache& TextureCache::instance()
{
    static TextureCache global;
    return global;
}

std::shared_ptr<Texture> TextureCache::loadSync(std::string_view path)
{
    return loadSync(path, render::TextureFilter::Point, render::TextureWrap::Clamp);
}

std::shared_ptr<Texture> TextureCache::loadSync(std::string_view path, render::TextureFilter filter,
                                                render::TextureWrap wrap)
{
    std::string key(path);
    if (filter != render::TextureFilter::Point || wrap != render::TextureWrap::Clamp)
        key += std::string("|") + (filter == render::TextureFilter::Linear ? "linear" : "point") +
               (wrap == render::TextureWrap::Repeat ? "|repeat" : "|clamp");
    if (auto existing = impl_->findLive(key))
        return existing;

    if (!render::Device::instance().isInitialized())
        return nullptr;

    auto image = assets::decodeImageFile(path);
    if (!image)
        return nullptr;

    auto texture = makeTexture(*image, filter, wrap);
    if (texture)
        impl_->cache[key] = texture;
    return texture;
}

std::shared_ptr<Texture> TextureCache::loadFromMemory(std::string_view key, const std::uint8_t* data, std::size_t size,
                                                      render::TextureFilter filter, render::TextureWrap wrap)
{
    std::string cacheKey(key);
    cacheKey += std::string("|") + (filter == render::TextureFilter::Linear ? "linear" : "point") +
                (wrap == render::TextureWrap::Repeat ? "|repeat" : "|clamp");
    if (auto existing = impl_->findLive(cacheKey))
        return existing;

    if (!render::Device::instance().isInitialized())
        return nullptr;

    auto image = assets::decodeImageMemory(data, size);
    if (!image)
        return nullptr;

    auto texture = makeTexture(*image, filter, wrap);
    if (texture)
        impl_->cache[cacheKey] = texture;
    return texture;
}

void TextureCache::loadAsync(std::string path, TextureLoadCallback onLoaded)
{
    if (auto existing = impl_->findLive(path))
    {
        onLoaded(std::move(existing));
        return;
    }

    assets::loadImageAsync(path, [this, path, onLoaded = std::move(onLoaded)](std::optional<assets::Image> image) {
        if (!image)
        {
            onLoaded(nullptr);
            return;
        }
        auto texture = makeTexture(*image);
        if (texture)
            impl_->cache[path] = texture;
        onLoaded(std::move(texture));
    });
}

std::size_t TextureCache::liveCount() const
{
    std::size_t count = 0;
    for (auto& [path, weak] : impl_->cache)
        if (!weak.expired())
            ++count;
    return count;
}

}
