#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "zio/render/Device.h"
#include "zio/ui/Font.h"
#include "zio/ui/Text.h"

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

namespace zio::ui
{

namespace
{

constexpr int kAtlasSize = 1024;
constexpr int kPadding   = 2;

class GdiFont final : public AtlasFont
{
public:
    GdiFont(std::string_view family, float sizePx) : size_(sizePx)
    {
        dc_ = CreateCompatibleDC(nullptr);
        if (!dc_)
            return;

        const std::wstring wideFamily(family.begin(), family.end());
        font_ = CreateFontW(-static_cast<int>(std::lround(sizePx)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, wideFamily.c_str());
        if (!font_)
            return;
        oldFont_ = SelectObject(dc_, font_);

        TEXTMETRICW metrics{};
        GetTextMetricsW(dc_, &metrics);
        lineHeight_ = static_cast<float>(metrics.tmHeight);
        ascent_     = static_cast<float>(metrics.tmAscent);
        cellHeight_ = metrics.tmHeight + 2 * kPadding;
        bitmapWidth_ = static_cast<int>(std::ceil(sizePx * 3.f)) + 2 * kPadding + 8;

        BITMAPINFO info{};
        info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth       = bitmapWidth_;
        info.bmiHeader.biHeight      = -cellHeight_;
        info.bmiHeader.biPlanes      = 1;
        info.bmiHeader.biBitCount    = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!bitmap_ || !bits_)
            return;
        oldBitmap_ = SelectObject(dc_, bitmap_);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, RGB(255, 255, 255));

        atlas_.assign(static_cast<std::size_t>(kAtlasSize) * kAtlasSize * 4, 0);
        ready_ = true;

        for (char32_t c = 32; c < 127; ++c)
            entry(c);
        for (char32_t c = 160; c < 256; ++c)
            entry(c);
        for (char32_t c : {0x2022u, 0x2026u, 0x20ACu})
            entry(static_cast<char32_t>(c));
    }

    ~GdiFont() override
    {
        auto& device = render::Device::instance();
        if (device.isInitialized())
        {
            if (texture_ != render::kInvalidHandle)
                device.destroyTexture(texture_);
            for (render::TextureHandle handle : retired_)
                device.destroyTexture(handle);
            for (render::TextureHandle handle : retiredOld_)
                device.destroyTexture(handle);
        }
        if (dc_)
        {
            if (oldBitmap_)
                SelectObject(dc_, oldBitmap_);
            if (oldFont_)
                SelectObject(dc_, oldFont_);
            DeleteDC(dc_);
        }
        if (bitmap_)
            DeleteObject(bitmap_);
        if (font_)
            DeleteObject(font_);
    }

    bool ready() const { return ready_; }

    float size() const override { return size_; }
    float lineHeight() const override { return lineHeight_; }
    float ascent() const override { return ascent_; }

    float advance(char32_t codepoint) override
    {
        if (codepoint < 32 || !ready_)
            return 0.f;
        return entry(codepoint).glyph.advance;
    }

    bool glyph(char32_t codepoint, Glyph& out) override
    {
        if (codepoint < 32 || !ready_)
            return false;
        const Entry& e = entry(codepoint);
        out            = e.glyph;
        return e.glyph.width > 0.f;
    }

    render::TextureHandle atlasTexture() override { return texture_; }

    void uploadIfDirty() override
    {
        auto& device = render::Device::instance();
        for (render::TextureHandle handle : retiredOld_)
            device.destroyTexture(handle);
        retiredOld_ = std::move(retired_);
        retired_.clear();

        if (!dirty_ || !ready_ || !device.isInitialized())
            return;

        render::TextureDesc desc{};
        desc.width       = kAtlasSize;
        desc.height      = kAtlasSize;
        desc.pixelsRgba8 = atlas_.data();
        desc.filter      = render::TextureFilter::Linear;
        desc.wrap        = render::TextureWrap::Clamp;
        const render::TextureHandle created = device.createTexture(desc);
        if (created == render::kInvalidHandle)
            return;
        if (texture_ != render::kInvalidHandle)
            retired_.push_back(texture_);
        texture_ = created;
        dirty_   = false;
    }

private:
    struct Entry
    {
        Glyph glyph;
    };

    Entry& entry(char32_t codepoint)
    {
        auto it = glyphs_.find(codepoint);
        if (it != glyphs_.end())
            return it->second;
        return glyphs_.emplace(codepoint, rasterize(codepoint)).first->second;
    }

    Entry rasterize(char32_t codepoint)
    {
        Entry result;

        wchar_t buffer[2] = {};
        int count         = 0;
        if (codepoint >= 0x10000)
        {
            const char32_t v = codepoint - 0x10000;
            buffer[0]        = static_cast<wchar_t>(0xD800 + (v >> 10));
            buffer[1]        = static_cast<wchar_t>(0xDC00 + (v & 0x3FF));
            count            = 2;
        }
        else
        {
            buffer[0] = static_cast<wchar_t>(codepoint);
            count     = 1;
        }

        SIZE extent{};
        GetTextExtentPoint32W(dc_, buffer, count, &extent);
        result.glyph.advance = static_cast<float>(extent.cx);

        if (codepoint == ' ' || codepoint == 0xA0)
            return result;

        ABC abc{0, static_cast<UINT>(extent.cx), 0};
        if (count == 1)
        {
            ABC measured{};
            if (GetCharABCWidthsW(dc_, static_cast<UINT>(codepoint), static_cast<UINT>(codepoint), &measured))
                abc = measured;
        }

        const int leftExtra = abc.abcA < 0 ? -abc.abcA : 0;
        const int inkWidth = (std::max)(static_cast<int>(extent.cx), abc.abcA + static_cast<int>(abc.abcB));
        int cellWidth      = leftExtra + inkWidth + 2 * kPadding + 1;
        cellWidth     = (std::min)(cellWidth, bitmapWidth_);

        if (penX_ + cellWidth > kAtlasSize)
        {
            penX_ = 0;
            penY_ += cellHeight_;
        }
        if (penY_ + cellHeight_ > kAtlasSize)
            return result;

        std::memset(bits_, 0, static_cast<std::size_t>(bitmapWidth_) * cellHeight_ * 4);
        TextOutW(dc_, kPadding + leftExtra, kPadding, buffer, count);
        GdiFlush();

        const auto* source = static_cast<const std::uint8_t*>(bits_);
        for (int y = 0; y < cellHeight_; ++y)
        {
            for (int x = 0; x < cellWidth; ++x)
            {
                const std::uint8_t* pixel = source + (static_cast<std::size_t>(y) * bitmapWidth_ + x) * 4;
                const std::uint8_t alpha  = (std::max)({pixel[0], pixel[1], pixel[2]});
                std::uint8_t* target =
                    atlas_.data() + (static_cast<std::size_t>(penY_ + y) * kAtlasSize + (penX_ + x)) * 4;
                target[0] = 255;
                target[1] = 255;
                target[2] = 255;
                target[3] = alpha;
            }
        }

        const float inverse = 1.f / static_cast<float>(kAtlasSize);
        result.glyph.u0       = static_cast<float>(penX_) * inverse;
        result.glyph.v0       = static_cast<float>(penY_) * inverse;
        result.glyph.u1       = static_cast<float>(penX_ + cellWidth) * inverse;
        result.glyph.v1       = static_cast<float>(penY_ + cellHeight_) * inverse;
        result.glyph.width    = static_cast<float>(cellWidth);
        result.glyph.height   = static_cast<float>(cellHeight_);
        result.glyph.offsetX  = static_cast<float>(-(kPadding + leftExtra));
        result.glyph.offsetY  = -(static_cast<float>(kPadding) + ascent_);

        penX_ += cellWidth;
        dirty_ = true;
        return result;
    }

    float size_       = 0.f;
    float lineHeight_ = 0.f;
    float ascent_     = 0.f;
    int cellHeight_   = 0;
    int bitmapWidth_  = 0;
    int penX_         = 0;
    int penY_         = 0;
    bool ready_       = false;
    bool dirty_       = false;

    HDC dc_            = nullptr;
    HFONT font_        = nullptr;
    HBITMAP bitmap_    = nullptr;
    HGDIOBJ oldFont_   = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* bits_        = nullptr;

    std::vector<std::uint8_t> atlas_;
    std::unordered_map<char32_t, Entry> glyphs_;
    render::TextureHandle texture_ = render::kInvalidHandle;
    std::vector<render::TextureHandle> retired_;
    std::vector<render::TextureHandle> retiredOld_;
};

}

std::shared_ptr<AtlasFont> createSystemFont(std::string_view family, float sizePx)
{
    auto font = std::make_shared<GdiFont>(family, sizePx);
    if (!font->ready())
        return nullptr;
    return font;
}

}

#else

namespace zio::ui
{

std::shared_ptr<AtlasFont> createSystemFont(std::string_view, float)
{
    return nullptr;
}

}

#endif
