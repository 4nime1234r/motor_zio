#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <stb_truetype.h>

namespace zio::internal
{

std::filesystem::path findSystemFontFile(std::string_view family, bool bold);

std::u32string decodeUtf8(std::string_view text);

class TrueTypeFace
{
public:
    bool load(std::string_view family, bool bold);
    bool ready() const { return ready_; }

    float emScale(float sizePx) const;
    float ascent(float sizePx) const;
    float lineHeight(float sizePx) const;
    float advance(char32_t codepoint, float sizePx) const;

    struct GlyphBitmap
    {
        int width   = 0;
        int height  = 0;
        int offsetX = 0;
        int offsetY = 0;
        std::vector<std::uint8_t> alpha;
    };

    bool rasterize(char32_t codepoint, float sizePx, GlyphBitmap& out) const;

private:
    std::vector<unsigned char> data_;
    stbtt_fontinfo info_{};
    bool ready_ = false;
};

}
