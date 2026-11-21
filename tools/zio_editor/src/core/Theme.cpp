#include "core/Theme.h"

#include <imgui.h>

#include <filesystem>
#include <string>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace zio_editor
{

namespace
{

constexpr float kFontSize = 17.f;

ImVec4 rgb(float r, float g, float b, float a = 1.f)
{
    return ImVec4(r, g, b, a);
}

ImVec4 hex(unsigned int value, float alpha = 1.f)
{
    return ImVec4(static_cast<float>((value >> 16) & 0xFF) / 255.f, static_cast<float>((value >> 8) & 0xFF) / 255.f,
                  static_cast<float>(value & 0xFF) / 255.f, alpha);
}

}

void applyZioTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding          = ImVec2(10.f, 8.f);
    style.FramePadding           = ImVec2(9.f, 5.f);
    style.CellPadding            = ImVec2(8.f, 4.f);
    style.ItemSpacing            = ImVec2(8.f, 6.f);
    style.ItemInnerSpacing       = ImVec2(6.f, 4.f);
    style.IndentSpacing          = 18.f;
    style.ScrollbarSize          = 12.f;
    style.GrabMinSize            = 10.f;
    style.WindowBorderSize       = 1.f;
    style.ChildBorderSize        = 1.f;
    style.PopupBorderSize        = 1.f;
    style.FrameBorderSize        = 0.f;
    style.TabBorderSize          = 0.f;
    style.TabBarBorderSize       = 1.f;
    style.TabBarOverlineSize     = 2.f;
    style.DockingSeparatorSize   = 2.f;
    style.WindowRounding         = 6.f;
    style.ChildRounding          = 6.f;
    style.FrameRounding          = 5.f;
    style.PopupRounding          = 8.f;
    style.ScrollbarRounding      = 8.f;
    style.GrabRounding           = 5.f;
    style.TabRounding            = 6.f;
    style.WindowTitleAlign       = ImVec2(0.f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1.f;

    const ImVec4 deepest    = hex(0x090a0f);
    const ImVec4 page       = hex(0x0f1117);
    const ImVec4 panel      = hex(0x15171f);
    const ImVec4 raised     = hex(0x1c1f2a);
    const ImVec4 raisedHot  = hex(0x262a38);
    const ImVec4 border     = rgb(1.f, 1.f, 1.f, 0.06f);
    const ImVec4 borderHot  = rgb(1.f, 1.f, 1.f, 0.12f);
    const ImVec4 accent     = hex(0x6366f1);
    const ImVec4 accentHot  = hex(0x818cf8);
    const ImVec4 accentDim  = hex(0x6366f1, 0.35f);
    const ImVec4 text       = hex(0xffffff);
    const ImVec4 textDim    = hex(0x9ca3af);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = textDim;
    c[ImGuiCol_WindowBg]              = panel;
    c[ImGuiCol_ChildBg]               = rgb(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_PopupBg]               = hex(0x15171f, 0.98f);
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = rgb(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_FrameBg]               = deepest;
    c[ImGuiCol_FrameBgHovered]        = raised;
    c[ImGuiCol_FrameBgActive]         = raisedHot;
    c[ImGuiCol_TitleBg]               = deepest;
    c[ImGuiCol_TitleBgActive]         = deepest;
    c[ImGuiCol_TitleBgCollapsed]      = deepest;
    c[ImGuiCol_MenuBarBg]             = deepest;
    c[ImGuiCol_ScrollbarBg]           = rgb(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_ScrollbarGrab]         = rgb(1.f, 1.f, 1.f, 0.22f);
    c[ImGuiCol_ScrollbarGrabHovered]  = rgb(1.f, 1.f, 1.f, 0.38f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accentHot;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentHot;
    c[ImGuiCol_Button]                = raised;
    c[ImGuiCol_ButtonHovered]         = raisedHot;
    c[ImGuiCol_ButtonActive]          = accent;
    c[ImGuiCol_Header]                = accentDim;
    c[ImGuiCol_HeaderHovered]         = hex(0x6366f1, 0.20f);
    c[ImGuiCol_HeaderActive]          = hex(0x6366f1, 0.50f);
    c[ImGuiCol_Separator]             = borderHot;
    c[ImGuiCol_SeparatorHovered]      = accent;
    c[ImGuiCol_SeparatorActive]       = accentHot;
    c[ImGuiCol_ResizeGrip]            = rgb(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_ResizeGripHovered]     = accentDim;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_InputTextCursor]       = text;
    c[ImGuiCol_Tab]                   = deepest;
    c[ImGuiCol_TabHovered]            = raisedHot;
    c[ImGuiCol_TabSelected]           = panel;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = deepest;
    c[ImGuiCol_TabDimmedSelected]     = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = hex(0x6366f1, 0.45f);
    c[ImGuiCol_DockingPreview]        = hex(0x6366f1, 0.40f);
    c[ImGuiCol_DockingEmptyBg]        = page;
    c[ImGuiCol_TableHeaderBg]         = deepest;
    c[ImGuiCol_TableBorderStrong]     = borderHot;
    c[ImGuiCol_TableBorderLight]      = border;
    c[ImGuiCol_TableRowBg]            = rgb(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_TableRowBgAlt]         = rgb(1.f, 1.f, 1.f, 0.025f);
    c[ImGuiCol_TextSelectedBg]        = hex(0x6366f1, 0.35f);
    c[ImGuiCol_DragDropTarget]        = accentHot;
    c[ImGuiCol_NavCursor]             = accentHot;
    c[ImGuiCol_NavWindowingHighlight] = rgb(1.f, 1.f, 1.f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = rgb(0.f, 0.f, 0.f, 0.45f);
    c[ImGuiCol_ModalWindowDimBg]      = rgb(0.f, 0.f, 0.f, 0.55f);
}

void loadEditorFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    char windowsDirectory[MAX_PATH] = {};
    if (GetWindowsDirectoryA(windowsDirectory, MAX_PATH) > 0)
    {
        const std::filesystem::path font = std::filesystem::path(windowsDirectory) / "Fonts" / "segoeui.ttf";
        std::error_code ec;
        if (std::filesystem::is_regular_file(font, ec) && io.Fonts->AddFontFromFileTTF(font.string().c_str(), kFontSize))
            return;
    }
    io.Fonts->AddFontDefault();
}

}
