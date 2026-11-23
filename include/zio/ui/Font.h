#pragma once

#include <memory>
#include <string_view>

#include "zio/core/Platform.h"
#include "zio/render/Types.h"

namespace zio::ui
{

struct Glyph
{
    float u0 = 0.f;
    float v0 = 0.f;
    float u1 = 0.f;
    float v1 = 0.f;
    float width    = 0.f;
    float height   = 0.f;
    float offsetX  = 0.f;
    float offsetY  = 0.f;
    float advance  = 0.f;
};

class ZIO_API Font
{
public:
    virtual ~Font() = default;

    virtual float size() const       = 0;
    virtual float lineHeight() const = 0;
    virtual float ascent() const     = 0;
    virtual float advance(char32_t codepoint) = 0;

    float textWidth(std::string_view utf8);
};

class ZIO_API AtlasFont : public Font
{
public:
    virtual bool glyph(char32_t codepoint, Glyph& out) = 0;
    virtual render::TextureHandle atlasTexture()       = 0;
    virtual void uploadIfDirty()                       = 0;
};

ZIO_API std::shared_ptr<Font> createFixedFont(float sizePx);
ZIO_API std::shared_ptr<AtlasFont> createSystemFont(std::string_view family, float sizePx, bool bold = false);
ZIO_API std::shared_ptr<AtlasFont> createScaledSystemFont(std::string_view family, float sizePx, bool bold, float scale);

}
