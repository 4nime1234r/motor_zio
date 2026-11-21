#include "zio/scene2d/Sprite.h"

#include <array>

#include "zio/renderer/BatchRenderer.h"

namespace zio::scene2d
{

Sprite::Sprite()
{
    setAnchorPoint({0.5f, 0.5f});
}

Sprite::Sprite(std::string_view path) : Sprite()
{
    setTexturePath(std::string(path));
}

void Sprite::setTexturePath(std::string path)
{
    setTexture(renderer::TextureCache::instance().loadSync(path));
    texturePath_ = std::move(path);
}

void Sprite::setTexture(std::shared_ptr<renderer::Texture> texture)
{
    texture_ = std::move(texture);
    if (texture_)
        setContentSize({static_cast<float>(texture_->width), static_cast<float>(texture_->height)});
}

void Sprite::onDraw()
{
    if (!texture_ || texture_->handle == render::kInvalidHandle)
        return;

    const math::Vec2 size = contentSize();
    const math::Mat3& world = worldMatrix();

    const std::array<math::Vec2, 4> corners = {
        world.transformPoint({0.f, 0.f}),
        world.transformPoint({size.x, 0.f}),
        world.transformPoint({0.f, size.y}),
        world.transformPoint({size.x, size.y}),
    };
    constexpr std::array<math::Vec2, 4> uvs = {
        math::Vec2{0.f, 0.f},
        math::Vec2{1.f, 0.f},
        math::Vec2{0.f, 1.f},
        math::Vec2{1.f, 1.f},
    };

    renderer::BatchRenderer::instance().submitQuad(corners, uvs, color(), texture_->handle, pipeline());
}

}
