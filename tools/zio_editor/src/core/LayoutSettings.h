#pragma once

#include <filesystem>

namespace zio_editor
{

enum class PanelSide
{
    Left,
    Right,
    Bottom
};

enum PanelId
{
    PanelHierarchy,
    PanelContent,
    PanelInspector,
    PanelPerformance,
    PanelCount
};

struct LayoutSettings
{
    PanelSide side[PanelCount] = {PanelSide::Left, PanelSide::Left, PanelSide::Right, PanelSide::Bottom};
    bool locked                = true;
};

const char* panelTitle(int panel);
const char* sideLabel(PanelSide side);

std::filesystem::path layoutSettingsFile();
LayoutSettings readLayoutSettings(const std::filesystem::path& file);
bool writeLayoutSettings(const std::filesystem::path& file, const LayoutSettings& settings);

}
