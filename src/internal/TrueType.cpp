#include "internal/TrueType.h"

#include "zio/core/Platform.h"

#if !defined(ZIO_PLATFORM_WINDOWS)

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace zio::internal
{

namespace
{

std::string lowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

const std::map<std::string, fs::path>& installedFonts()
{
    static const std::map<std::string, fs::path> fonts = [] {
        std::map<std::string, fs::path> found;
        std::vector<fs::path> roots = {"/usr/share/fonts", "/usr/local/share/fonts"};
        if (const char* home = std::getenv("HOME"))
        {
            roots.push_back(fs::path(home) / ".fonts");
            roots.push_back(fs::path(home) / ".local" / "share" / "fonts");
        }
        for (const fs::path& root : roots)
        {
            std::error_code ec;
            if (!fs::is_directory(root, ec))
                continue;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                 it != end && !ec; it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                    continue;
                const std::string ext = lowerCase(it->path().extension().string());
                if (ext == ".ttf" || ext == ".otf" || ext == ".ttc")
                    found.emplace(lowerCase(it->path().filename().string()), it->path());
            }
        }
        return found;
    }();
    return fonts;
}


#if defined(__linux__)
fs::path fontconfigMatch(std::string_view family, bool bold)
{
    std::string pattern;
    for (const char c : family)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-')
            pattern.push_back(c);
    if (pattern.empty())
        pattern = "sans";
    if (bold)
        pattern += ":bold";

    const std::string command = "fc-match -f '%{file}' '" + pattern + "' 2>/dev/null";
    FILE* pipe                = popen(command.c_str(), "r");
    if (!pipe)
        return {};
    std::string output;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();
    if (output.empty())
        return {};
    std::error_code ec;
    const fs::path file(output);
    const std::string ext = lowerCase(file.extension().string());
    if ((ext == ".ttf" || ext == ".otf" || ext == ".ttc") && fs::is_regular_file(file, ec))
        return file;
    return {};
}
#endif

}

fs::path findSystemFontFile(std::string_view family, bool bold)
{
#if defined(__linux__)
    if (const fs::path matched = fontconfigMatch(family, bold); !matched.empty())
        return matched;
#endif
    const auto& fonts = installedFonts();
    if (fonts.empty())
        return {};

    std::string name = lowerCase(std::string(family));
    name.erase(std::remove(name.begin(), name.end(), ' '), name.end());

    std::vector<std::string> candidates;
    for (const char* suffix : bold ? std::initializer_list<const char*>{"-bold", "bold", "-semibold", "b"}
                                   : std::initializer_list<const char*>{"", "-regular", "regular", "r"})
        for (const char* ext : {".ttf", ".otf", ".ttc"})
            candidates.push_back(name + suffix + ext);

    const char* fallbacks[][2] = {{"dejavusans.ttf", "dejavusans-bold.ttf"},
                                  {"liberationsans-regular.ttf", "liberationsans-bold.ttf"},
                                  {"notosans-regular.ttf", "notosans-bold.ttf"},
                                  {"ubuntu-r.ttf", "ubuntu-b.ttf"},
                                  {"arial.ttf", "arialbold.ttf"},
                                  {"arial.ttf", "arial bold.ttf"}};
    for (const auto& pair : fallbacks)
        candidates.push_back(pair[bold ? 1 : 0]);

    for (const std::string& candidate : candidates)
    {
        const auto it = fonts.find(candidate);
        if (it != fonts.end())
            return it->second;
    }
    for (const auto& entry : fonts)
        if (entry.first.find("sans") != std::string::npos && entry.first.find("mono") == std::string::npos)
            return entry.second;
    return fonts.begin()->second;
}

std::u32string decodeUtf8(std::string_view text)
{
    std::u32string out;
    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        char32_t codepoint     = c;
        std::size_t length     = 1;
        if (c >= 0xF0 && i + 3 < text.size())
        {
            codepoint = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
                        ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
            length = 4;
        }
        else if (c >= 0xE0 && i + 2 < text.size())
        {
            codepoint = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                        (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            length = 3;
        }
        else if (c >= 0xC0 && i + 1 < text.size())
        {
            codepoint = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
            length    = 2;
        }
        out.push_back(codepoint);
        i += length;
    }
    return out;
}

bool TrueTypeFace::load(std::string_view family, bool bold)
{
    ready_                = false;
    const fs::path file = findSystemFontFile(family, bold);
    if (file.empty())
        return false;
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (data_.empty())
        return false;
    const int offset = stbtt_GetFontOffsetForIndex(data_.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&info_, data_.data(), offset))
        return false;
    ready_ = true;
    return true;
}

float TrueTypeFace::emScale(float sizePx) const
{
    return stbtt_ScaleForMappingEmToPixels(&info_, sizePx);
}

float TrueTypeFace::ascent(float sizePx) const
{
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info_, &ascent, &descent, &gap);
    return static_cast<float>(ascent) * emScale(sizePx);
}

float TrueTypeFace::lineHeight(float sizePx) const
{
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info_, &ascent, &descent, &gap);
    return static_cast<float>(ascent - descent + gap) * emScale(sizePx);
}

float TrueTypeFace::advance(char32_t codepoint, float sizePx) const
{
    int advance = 0, bearing = 0;
    stbtt_GetCodepointHMetrics(&info_, static_cast<int>(codepoint), &advance, &bearing);
    return static_cast<float>(advance) * emScale(sizePx);
}

bool TrueTypeFace::rasterize(char32_t codepoint, float sizePx, GlyphBitmap& out) const
{
    const float scale = emScale(sizePx);
    int width = 0, height = 0, offsetX = 0, offsetY = 0;
    unsigned char* bitmap =
        stbtt_GetCodepointBitmap(&info_, scale, scale, static_cast<int>(codepoint), &width, &height, &offsetX, &offsetY);
    if (!bitmap)
        return false;
    out.width   = width;
    out.height  = height;
    out.offsetX = offsetX;
    out.offsetY = offsetY;
    out.alpha.assign(bitmap, bitmap + static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    stbtt_FreeBitmap(bitmap, nullptr);
    return width > 0 && height > 0;
}

}

#endif
