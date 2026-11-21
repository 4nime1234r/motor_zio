#include "zio/scene2d/Sprite.h"

#include <algorithm>
#include <array>
#include <cmath>

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
    applyFrameSize();
}

void Sprite::applyFrameSize()
{
    if (texture_)
        setContentSize({static_cast<float>(texture_->width) / static_cast<float>(columns_),
                        static_cast<float>(texture_->height) / static_cast<float>(rows_)});
}

void Sprite::setSheet(int columns, int rows)
{
    columns_ = std::max(1, columns);
    rows_    = std::max(1, rows);
    frame_   = std::clamp(frame_, 0, frameCount() - 1);
    applyFrameSize();
}

void Sprite::setFrame(int frame)
{
    frame_ = std::clamp(frame, 0, frameCount() - 1);
}

void Sprite::addAnimation(SpriteAnimation animation)
{
    for (SpriteAnimation& existing : animations_)
        if (existing.name == animation.name)
        {
            existing = std::move(animation);
            return;
        }
    animations_.push_back(std::move(animation));
}

void Sprite::clearAnimations()
{
    animations_.clear();
    stop();
}

bool Sprite::play(const std::string& name, bool restart)
{
    for (const SpriteAnimation& a : animations_)
    {
        if (a.name != name)
            continue;
        if (playing_ && current_ == name && !restart)
            return true;
        current_ = name;
        playing_ = true;
        clock_   = 0.f;
        setFrame(a.firstFrame);
        return true;
    }
    return false;
}

void Sprite::stop()
{
    playing_ = false;
    current_.clear();
}

void Sprite::update(float dt)
{
    if (!playing_)
        return;
    const SpriteAnimation* anim = nullptr;
    for (const SpriteAnimation& a : animations_)
        if (a.name == current_)
            anim = &a;
    if (!anim || anim->fps <= 0.f)
        return;

    clock_ += dt * speed_;
    const int first = std::clamp(std::min(anim->firstFrame, anim->lastFrame), 0, frameCount() - 1);
    const int last  = std::clamp(std::max(anim->firstFrame, anim->lastFrame), 0, frameCount() - 1);
    const int count = last - first + 1;
    const int step  = static_cast<int>(std::floor(clock_ * anim->fps));
    if (anim->loop)
    {
        setFrame(first + ((step % count) + count) % count);
    }
    else if (step >= count)
    {
        setFrame(last);
        playing_ = false;
    }
    else
    {
        setFrame(first + std::max(step, 0));
    }
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
    const float cellW = 1.f / static_cast<float>(columns_);
    const float cellH = 1.f / static_cast<float>(rows_);
    const float u0    = static_cast<float>(frame_ % columns_) * cellW;
    const float v0    = static_cast<float>(frame_ / columns_) * cellH;
    const std::array<math::Vec2, 4> uvs = {
        math::Vec2{u0, v0},
        math::Vec2{u0 + cellW, v0},
        math::Vec2{u0, v0 + cellH},
        math::Vec2{u0 + cellW, v0 + cellH},
    };

    renderer::BatchRenderer::instance().submitQuad(corners, uvs, color(), texture_->handle, pipeline());
}

}
