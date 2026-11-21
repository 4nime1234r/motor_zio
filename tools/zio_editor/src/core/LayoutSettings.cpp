#include "core/LayoutSettings.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

constexpr const char* kKeys[PanelCount] = {"jerarquia", "contenido", "inspector", "archivos", "rendimiento"};

const char* sideKey(PanelSide side)
{
    switch (side)
    {
    case PanelSide::Left:
        return "izquierda";
    case PanelSide::Right:
        return "derecha";
    case PanelSide::Bottom:
        return "abajo";
    }
    return "izquierda";
}

bool parseSide(const std::string& text, PanelSide& out)
{
    for (PanelSide side : {PanelSide::Left, PanelSide::Right, PanelSide::Bottom})
        if (text == sideKey(side))
        {
            out = side;
            return true;
        }
    return false;
}

std::string trim(std::string text)
{
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\t'))
        text.pop_back();
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.erase(0, 1);
    return text;
}

}

const char* panelTitle(int panel)
{
    switch (panel)
    {
    case PanelHierarchy:
        return "Jerarquía";
    case PanelContent:
        return "Contenido";
    case PanelInspector:
        return "Inspector";
    case PanelFiles:
        return "Archivos";
    case PanelPerformance:
        return "Rendimiento";
    default:
        return "";
    }
}

const char* sideLabel(PanelSide side)
{
    switch (side)
    {
    case PanelSide::Left:
        return "Izquierda";
    case PanelSide::Right:
        return "Derecha";
    case PanelSide::Bottom:
        return "Abajo";
    }
    return "";
}

fs::path layoutSettingsFile()
{
    const char* appData = std::getenv("APPDATA");
    if (!appData || !*appData)
        return {};
    return fs::path(appData) / "ZioEditor" / "disposicion.txt";
}

LayoutSettings readLayoutSettings(const fs::path& file)
{
    LayoutSettings settings;
    if (file.empty())
        return settings;

    std::ifstream in(file, std::ios::binary);
    if (!in)
        return settings;

    std::string line;
    while (std::getline(in, line))
    {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key   = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key == "bloqueada")
        {
            settings.locked = value != "0";
            continue;
        }
        for (int i = 0; i < PanelCount; ++i)
        {
            PanelSide side;
            if (key == kKeys[i] && parseSide(value, side))
                settings.side[i] = side;
        }
    }
    return settings;
}

bool writeLayoutSettings(const fs::path& file, const LayoutSettings& settings)
{
    if (file.empty())
        return false;

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    for (int i = 0; i < PanelCount; ++i)
        out << kKeys[i] << "=" << sideKey(settings.side[i]) << "\n";
    out << "bloqueada=" << (settings.locked ? 1 : 0) << "\n";
    return static_cast<bool>(out);
}

}
