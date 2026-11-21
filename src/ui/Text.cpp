#include "zio/ui/Text.h"

namespace zio::ui::text
{

char32_t decode(std::string_view s, std::size_t& index)
{
    if (index >= s.size())
        return 0;

    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const unsigned char lead = byte(index);

    int extra       = 0;
    char32_t result = 0;
    if (lead < 0x80)
    {
        ++index;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0)
    {
        extra  = 1;
        result = lead & 0x1F;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        extra  = 2;
        result = lead & 0x0F;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        extra  = 3;
        result = lead & 0x07;
    }
    else
    {
        ++index;
        return 0xFFFD;
    }

    if (index + static_cast<std::size_t>(extra) >= s.size())
    {
        index = s.size();
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; ++i)
    {
        const unsigned char next = byte(index + static_cast<std::size_t>(i));
        if ((next & 0xC0) != 0x80)
        {
            ++index;
            return 0xFFFD;
        }
        result = (result << 6) | (next & 0x3F);
    }
    index += static_cast<std::size_t>(extra) + 1;
    return result;
}

void append(std::string& out, char32_t cp)
{
    if (cp < 0x80)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::size_t nextBoundary(std::string_view s, std::size_t index)
{
    if (index >= s.size())
        return s.size();
    std::size_t i = index;
    decode(s, i);
    return i;
}

std::size_t previousBoundary(std::string_view s, std::size_t index)
{
    if (index == 0)
        return 0;
    if (index > s.size())
        index = s.size();
    std::size_t i = index - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
        --i;
    return i;
}

bool isWordCharacter(char32_t cp)
{
    if (cp == '_')
        return true;
    if (cp >= '0' && cp <= '9')
        return true;
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
        return true;
    return cp >= 0xC0;
}

std::size_t nextWordBoundary(std::string_view s, std::size_t index)
{
    std::size_t i = index;
    while (i < s.size())
    {
        std::size_t j = i;
        if (isWordCharacter(decode(s, j)))
            break;
        i = j;
    }
    while (i < s.size())
    {
        std::size_t j = i;
        if (!isWordCharacter(decode(s, j)))
            break;
        i = j;
    }
    return i;
}

std::size_t previousWordBoundary(std::string_view s, std::size_t index)
{
    std::size_t i = index;
    while (i > 0)
    {
        const std::size_t p = previousBoundary(s, i);
        std::size_t j       = p;
        if (isWordCharacter(decode(s, j)))
            break;
        i = p;
    }
    while (i > 0)
    {
        const std::size_t p = previousBoundary(s, i);
        std::size_t j       = p;
        if (!isWordCharacter(decode(s, j)))
            break;
        i = p;
    }
    return i;
}

}
