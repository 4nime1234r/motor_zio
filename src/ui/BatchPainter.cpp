#include "zio/ui/BatchPainter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "zio/render/Device.h"
#include "zio/renderer/BatchRenderer.h"
#include "zio/ui/Text.h"

namespace zio::ui
{

namespace
{

constexpr int kRoundTextureSize   = 64;
constexpr float kRoundTextureRadius = 16.f;

}

struct BatchPainter::Impl
{
    render::TextureHandle white = render::kInvalidHandle;
    render::TextureHandle round = render::kInvalidHandle;
    std::vector<Rect> clips;
    std::vector<AtlasFont*> prepared;
    bool initialized = false;

    bool init()
    {
        auto& device = render::Device::instance();
        if (!device.isInitialized())
            return false;

        const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
        render::TextureDesc whiteDesc{};
        whiteDesc.width       = 1;
        whiteDesc.height      = 1;
        whiteDesc.pixelsRgba8 = whitePixel;
        white                 = device.createTexture(whiteDesc);

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kRoundTextureSize) * kRoundTextureSize * 4);
        const float half = static_cast<float>(kRoundTextureSize) * 0.5f;
        for (int y = 0; y < kRoundTextureSize; ++y)
        {
            for (int x = 0; x < kRoundTextureSize; ++x)
            {
                const float px      = std::fabs(static_cast<float>(x) + 0.5f - half) - (half - kRoundTextureRadius);
                const float py      = std::fabs(static_cast<float>(y) + 0.5f - half) - (half - kRoundTextureRadius);
                const float outside = std::sqrt((std::max)(px, 0.f) * (std::max)(px, 0.f) +
                                                (std::max)(py, 0.f) * (std::max)(py, 0.f));
                const float inside  = (std::min)((std::max)(px, py), 0.f);
                const float distance = outside + inside - kRoundTextureRadius;
                const float coverage = std::clamp(0.5f - distance, 0.f, 1.f);

                std::uint8_t* pixel = pixels.data() + (static_cast<std::size_t>(y) * kRoundTextureSize + x) * 4;
                pixel[0]            = 255;
                pixel[1]            = 255;
                pixel[2]            = 255;
                pixel[3]            = static_cast<std::uint8_t>(std::lround(coverage * 255.f));
            }
        }
        render::TextureDesc roundDesc{};
        roundDesc.width       = kRoundTextureSize;
        roundDesc.height      = kRoundTextureSize;
        roundDesc.pixelsRgba8 = pixels.data();
        roundDesc.filter      = render::TextureFilter::Linear;
        roundDesc.wrap        = render::TextureWrap::Clamp;
        round                 = device.createTexture(roundDesc);

        initialized = white != render::kInvalidHandle && round != render::kInvalidHandle;
        return initialized;
    }

    void quad(const Rect& rect, float u0, float v0, float u1, float v1, Color color, render::TextureHandle texture)
    {
        if (rect.w <= 0.f || rect.h <= 0.f)
            return;
        const Rect clip    = clips.back();
        const Rect visible = rect.intersect(clip);
        if (visible.empty())
            return;

        const float tx0 = (visible.x - rect.x) / rect.w;
        const float tx1 = (visible.right() - rect.x) / rect.w;
        const float ty0 = (visible.y - rect.y) / rect.h;
        const float ty1 = (visible.bottom() - rect.y) / rect.h;
        const float nu0 = u0 + (u1 - u0) * tx0;
        const float nu1 = u0 + (u1 - u0) * tx1;
        const float nv0 = v0 + (v1 - v0) * ty0;
        const float nv1 = v0 + (v1 - v0) * ty1;

        const std::array<math::Vec2, 4> positions = {math::Vec2{visible.x, visible.y},
                                                     math::Vec2{visible.right(), visible.y},
                                                     math::Vec2{visible.x, visible.bottom()},
                                                     math::Vec2{visible.right(), visible.bottom()}};
        const std::array<math::Vec2, 4> uvs       = {math::Vec2{nu0, nv0}, math::Vec2{nu1, nv0}, math::Vec2{nu0, nv1},
                                                     math::Vec2{nu1, nv1}};
        renderer::BatchRenderer::instance().submitQuad(positions, uvs, color, texture);
    }
};

BatchPainter::BatchPainter() : impl_(std::make_unique<Impl>()) {}
BatchPainter::~BatchPainter()
{
    auto& device = render::Device::instance();
    if (device.isInitialized())
    {
        if (impl_->white != render::kInvalidHandle)
            device.destroyTexture(impl_->white);
        if (impl_->round != render::kInvalidHandle)
            device.destroyTexture(impl_->round);
    }
}

bool BatchPainter::begin(float viewportWidth, float viewportHeight)
{
    if (!impl_->initialized && !impl_->init())
        return false;
    impl_->clips.clear();
    impl_->clips.push_back({0.f, 0.f, viewportWidth, viewportHeight});
    impl_->prepared.clear();
    return true;
}

void BatchPainter::pushClip(const Rect& rect)
{
    impl_->clips.push_back(impl_->clips.back().intersect(rect));
}

void BatchPainter::popClip()
{
    if (impl_->clips.size() > 1)
        impl_->clips.pop_back();
}

void BatchPainter::fillRect(const Rect& rect, Color color)
{
    if (color.a <= 0.f || impl_->clips.empty())
        return;
    impl_->quad(rect, 0.5f, 0.5f, 0.5f, 0.5f, color, impl_->white);
}

void BatchPainter::fillRoundRect(const Rect& rect, Color color, float radius)
{
    if (color.a <= 0.f || impl_->clips.empty())
        return;
    const float r = std::clamp(radius, 0.f, (std::min)(rect.w, rect.h) * 0.5f);
    if (r < 1.f)
    {
        fillRect(rect, color);
        return;
    }

    const float edge = kRoundTextureRadius / static_cast<float>(kRoundTextureSize);
    const float xs[4] = {rect.x, rect.x + r, rect.right() - r, rect.right()};
    const float ys[4] = {rect.y, rect.y + r, rect.bottom() - r, rect.bottom()};
    const float us[4] = {0.f, edge, 1.f - edge, 1.f};

    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            impl_->quad({xs[column], ys[row], xs[column + 1] - xs[column], ys[row + 1] - ys[row]}, us[column], us[row],
                        us[column + 1], us[row + 1], color, impl_->round);
}

void BatchPainter::line(Vec2 from, Vec2 to, Color color, float thickness)
{
    if (color.a <= 0.f)
        return;
    const Vec2 delta = to - from;
    const float length = delta.length();
    if (length < 1e-4f)
        return;
    const Vec2 normal{-delta.y / length * thickness * 0.5f, delta.x / length * thickness * 0.5f};

    const std::array<math::Vec2, 4> positions = {from + normal, to + normal, from - normal, to - normal};
    const std::array<math::Vec2, 4> uvs       = {math::Vec2{0.5f, 0.5f}, math::Vec2{0.5f, 0.5f}, math::Vec2{0.5f, 0.5f},
                                                 math::Vec2{0.5f, 0.5f}};
    renderer::BatchRenderer::instance().submitQuad(positions, uvs, color, impl_->white);
}

void BatchPainter::triangle(Vec2 a, Vec2 b, Vec2 c, Color color)
{
    if (color.a <= 0.f)
        return;
    const std::array<math::Vec2, 4> positions = {a, b, c, c};
    const std::array<math::Vec2, 4> uvs       = {math::Vec2{0.5f, 0.5f}, math::Vec2{0.5f, 0.5f}, math::Vec2{0.5f, 0.5f},
                                                 math::Vec2{0.5f, 0.5f}};
    renderer::BatchRenderer::instance().submitQuad(positions, uvs, color, impl_->white);
}

void BatchPainter::text(Font& font, Vec2 topLeft, std::string_view utf8, Color color)
{
    if (color.a <= 0.f || utf8.empty() || impl_->clips.empty())
        return;
    auto* atlas = dynamic_cast<AtlasFont*>(&font);
    if (!atlas)
        return;

    if (std::find(impl_->prepared.begin(), impl_->prepared.end(), atlas) == impl_->prepared.end())
    {
        atlas->uploadIfDirty();
        impl_->prepared.push_back(atlas);
    }
    const render::TextureHandle texture = atlas->atlasTexture();
    if (texture == render::kInvalidHandle)
        return;

    float penX            = std::floor(topLeft.x + 0.5f);
    const float baselineY = std::floor(topLeft.y + atlas->ascent() + 0.5f);

    std::size_t index = 0;
    while (index < utf8.size())
    {
        const char32_t code = text::decode(utf8, index);
        Glyph glyph;
        if (atlas->glyph(code, glyph))
            impl_->quad({penX + glyph.offsetX, baselineY + glyph.offsetY, glyph.width, glyph.height}, glyph.u0,
                        glyph.v0, glyph.u1, glyph.v1, color, texture);
        penX += glyph.advance > 0.f ? glyph.advance : atlas->advance(code);
    }
}

}
