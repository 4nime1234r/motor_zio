#include "zio/scene2d/Label.h"

#include <array>

#include "zio/renderer/BatchRenderer.h"
#include "zio/render/Device.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <objidl.h>

#include <algorithm>
#include <vector>

#include <gdiplus.h>

namespace zio::scene2d
{

namespace
{

struct GdiplusSession
{
    ULONG_PTR token = 0;
    GdiplusSession()
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }
    ~GdiplusSession() { Gdiplus::GdiplusShutdown(token); }
};

void ensureGdiplus()
{
    static GdiplusSession session;
    (void)session;
}

std::wstring toWide(std::string_view s)
{
    return std::wstring(s.begin(), s.end());
}

}

void Label::rebuildTexture()
{
    auto& device = render::Device::instance();
    if (texture_ != render::kInvalidHandle)
    {
        device.destroyTexture(texture_);
        texture_ = render::kInvalidHandle;
    }

    if (text_.empty())
    {
        setContentSize({0.f, 0.f});
        return;
    }

    ensureGdiplus();

    const std::wstring wideText = toWide(text_);
    const std::wstring wideFont = toWide(fontFamily_);

    Gdiplus::FontFamily family(wideFont.c_str());
    Gdiplus::Font font(&family, fontSizePx_, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    Gdiplus::Bitmap measureBitmap(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics measureGraphics(&measureBitmap);
    Gdiplus::RectF layoutRect(0.f, 0.f, 8192.f, 8192.f);
    Gdiplus::RectF bounds;
    measureGraphics.MeasureString(wideText.c_str(), static_cast<int>(wideText.size()), &font, layoutRect, &bounds);

    const std::uint32_t width  = std::max(1u, static_cast<std::uint32_t>(std::ceil(bounds.Width)));
    const std::uint32_t height = std::max(1u, static_cast<std::uint32_t>(std::ceil(bounds.Height)));

    Gdiplus::Bitmap bitmap(static_cast<INT>(width), static_cast<INT>(height), PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::Color textColor(static_cast<BYTE>(color_.a * 255.f), static_cast<BYTE>(color_.r * 255.f),
                              static_cast<BYTE>(color_.g * 255.f), static_cast<BYTE>(color_.b * 255.f));
    Gdiplus::SolidBrush brush(textColor);
    graphics.DrawString(wideText.c_str(), static_cast<int>(wideText.size()), &font, Gdiplus::PointF(0.f, 0.f),
                         &brush);

    Gdiplus::BitmapData locked{};
    Gdiplus::Rect fullRect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    if (bitmap.LockBits(&fullRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &locked) != Gdiplus::Ok)
    {
        setContentSize({0.f, 0.f});
        return;
    }

    std::vector<std::uint8_t> pixelsRgba8(static_cast<std::size_t>(width) * height * 4);
    const auto* src = static_cast<const std::uint8_t*>(locked.Scan0);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint8_t* row = src + static_cast<std::size_t>(y) * locked.Stride;
        std::uint8_t* dstRow    = pixelsRgba8.data() + static_cast<std::size_t>(y) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            dstRow[x * 4 + 0] = row[x * 4 + 2];
            dstRow[x * 4 + 1] = row[x * 4 + 1];
            dstRow[x * 4 + 2] = row[x * 4 + 0];
            dstRow[x * 4 + 3] = row[x * 4 + 3];
        }
    }
    bitmap.UnlockBits(&locked);

    render::TextureDesc desc{};
    desc.width       = width;
    desc.height      = height;
    desc.pixelsRgba8 = pixelsRgba8.data();
    texture_          = device.createTexture(desc);

    setContentSize({static_cast<float>(width), static_cast<float>(height)});
}

}

#else

namespace zio::scene2d
{

void Label::rebuildTexture()
{
    setContentSize({0.f, 0.f});
}

}

#endif

namespace zio::scene2d
{

Label::Label()
{
    setAnchorPoint({0.5f, 0.5f});
}

Label::Label(std::string text, std::string fontFamily, float fontSizePx) : Label()
{
    fontFamily_ = std::move(fontFamily);
    fontSizePx_ = fontSizePx;
    setText(std::move(text));
}

Label::~Label()
{
    if (texture_ != render::kInvalidHandle)
        render::Device::instance().destroyTexture(texture_);
}

void Label::setText(std::string text)
{
    if (text_ == text)
        return;
    text_ = std::move(text);
    rebuildTexture();
}

void Label::setFont(std::string fontFamily, float fontSizePx)
{
    fontFamily_ = std::move(fontFamily);
    fontSizePx_ = fontSizePx;
    rebuildTexture();
}

void Label::setColor(renderer::Color color)
{
    color_ = color;
    rebuildTexture();
}

void Label::onDraw()
{
    if (texture_ == render::kInvalidHandle)
        return;

    const math::Vec2 size   = contentSize();
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

    renderer::BatchRenderer::instance().submitQuad(corners, uvs, renderer::Color::white(), texture_,
                                                    render::kInvalidHandle);
}

}
