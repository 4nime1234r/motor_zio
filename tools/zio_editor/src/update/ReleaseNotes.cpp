#include "update/ReleaseNotes.h"

#include <algorithm>
#include <variant>

#include "internal/Json.h"

namespace zio_editor
{

namespace
{

constexpr std::size_t kMaxLines = 200;

std::string trim(const std::string& text)
{
    std::size_t a = 0;
    std::size_t b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r'))
        --b;
    return text.substr(a, b - a);
}

std::string stripInline(const std::string& line)
{
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '`' || (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '*'))
        {
            if (line[i] == '*')
                ++i;
            continue;
        }
        if (line[i] == '[')
        {
            const std::size_t close = line.find(']', i);
            if (close != std::string::npos && close + 1 < line.size() && line[close + 1] == '(')
            {
                const std::size_t end = line.find(')', close);
                if (end != std::string::npos)
                {
                    out += line.substr(i + 1, close - i - 1);
                    i = end;
                    continue;
                }
            }
        }
        out += line[i];
    }
    return out;
}

void wrap(const std::string& text, const std::string& indent, std::size_t width, bool heading,
          std::vector<NoteLine>& out)
{
    std::string current;
    std::size_t position = 0;
    bool firstLine       = true;
    const auto flush     = [&]() {
        out.push_back({(firstLine ? std::string() : indent) + current, heading});
        firstLine = false;
        current.clear();
    };
    while (position < text.size())
    {
        while (position < text.size() && text[position] == ' ')
            ++position;
        std::size_t end = text.find(' ', position);
        if (end == std::string::npos)
            end = text.size();
        const std::string word = text.substr(position, end - position);
        position               = end;
        if (word.empty())
            continue;
        const std::size_t room = width > indent.size() ? width - indent.size() : width;
        if (!current.empty() && current.size() + 1 + word.size() > room)
            flush();
        current += (current.empty() ? "" : " ") + word;
    }
    if (!current.empty())
        flush();
}

}

std::string releaseBodyFromJson(const std::string& json)
{
    zio::json::Value root;
    std::string error;
    if (!zio::json::Parser(json).parse(root, error))
        return {};
    const zio::json::Value* body = root.find("body");
    if (!body)
        return {};
    const auto* text = std::get_if<std::string>(&body->v);
    return text ? *text : std::string();
}

std::vector<NoteLine> formatReleaseNotes(const std::string& markdown, std::size_t width)
{
    std::vector<NoteLine> out;
    std::size_t position = 0;
    bool lastBlank       = true;
    while (position <= markdown.size() && out.size() < kMaxLines)
    {
        std::size_t end = markdown.find('\n', position);
        if (end == std::string::npos)
            end = markdown.size();
        const std::string raw = trim(markdown.substr(position, end - position));
        position              = end + 1;

        if (raw.empty())
        {
            if (!lastBlank)
                out.push_back({});
            lastBlank = true;
            if (end == markdown.size())
                break;
            continue;
        }
        lastBlank = false;

        if (raw[0] == '#')
        {
            std::size_t skip = 0;
            while (skip < raw.size() && raw[skip] == '#')
                ++skip;
            wrap(stripInline(trim(raw.substr(skip))), {}, width, true, out);
        }
        else if ((raw[0] == '-' || raw[0] == '*') && raw.size() > 1 && raw[1] == ' ')
        {
            std::vector<NoteLine> lines;
            wrap(stripInline(trim(raw.substr(2))), "  ", width, false, lines);
            for (std::size_t i = 0; i < lines.size(); ++i)
                out.push_back({(i == 0 ? std::string("\xE2\x80\xA2 ") : std::string()) + lines[i].text, false});
        }
        else
        {
            wrap(stripInline(raw), {}, width, false, out);
        }
        if (end == markdown.size())
            break;
    }
    while (!out.empty() && out.back().text.empty())
        out.pop_back();
    return out;
}

}
