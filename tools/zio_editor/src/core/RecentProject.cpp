#include "core/RecentProject.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace zio_editor
{

fs::path recentProjectFile()
{
    const char* appData = std::getenv("APPDATA");
    if (!appData || !*appData)
        return {};
    return fs::path(appData) / "ZioEditor" / "ultimo_proyecto.txt";
}

std::optional<fs::path> readRecentProject(const fs::path& file)
{
    if (file.empty())
        return std::nullopt;

    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;

    std::string line;
    std::getline(in, line);

    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.erase(0, 1);
    if (line.empty())
        return std::nullopt;

    const fs::path project(line);
    std::error_code ec;
    if (!fs::is_directory(project, ec))
        return std::nullopt;
    return project;
}

bool writeRecentProject(const fs::path& file, const fs::path& projectRoot)
{
    if (file.empty() || projectRoot.empty())
        return false;

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << projectRoot.string() << "\n";
    return static_cast<bool>(out);
}

}
