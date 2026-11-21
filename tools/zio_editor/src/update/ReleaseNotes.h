#pragma once

#include <string>
#include <vector>

namespace zio_editor
{

struct NoteLine
{
    std::string text;
    bool heading = false;
};

std::string releaseBodyFromJson(const std::string& json);

std::vector<NoteLine> formatReleaseNotes(const std::string& markdown, std::size_t width);

}
