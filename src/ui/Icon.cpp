#include "zio/ui/Icon.h"

#include <vector>

#include "zio/render/Device.h"

namespace zio::ui
{

Icon::~Icon()
{
    auto& device = render::Device::instance();
    if (texture_ != render::kInvalidHandle && device.isInitialized())
        device.destroyTexture(texture_);
}

std::shared_ptr<Icon> Icon::fromAlpha(const std::uint8_t* alpha, int width, int height)
{
    auto& device = render::Device::instance();
    if (!alpha || width <= 0 || height <= 0 || !device.isInitialized())
        return nullptr;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++i)
    {
        pixels[i * 4]     = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = alpha[i];
    }

    render::TextureDesc desc{};
    desc.width       = static_cast<std::uint32_t>(width);
    desc.height      = static_cast<std::uint32_t>(height);
    desc.pixelsRgba8 = pixels.data();
    desc.filter      = render::TextureFilter::Linear;
    desc.wrap        = render::TextureWrap::Clamp;
    const render::TextureHandle handle = device.createTexture(desc);
    if (handle == render::kInvalidHandle)
        return nullptr;

    std::shared_ptr<Icon> icon(new Icon());
    icon->texture_ = handle;
    return icon;
}

}
