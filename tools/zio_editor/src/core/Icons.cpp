#include "core/Icons.h"

#include <imgui.h>

#include <cmath>

namespace zio_editor
{

namespace
{

constexpr float kPi = 3.14159265f;

void arrowHead(ImDrawList* list, ImVec2 tip, ImVec2 direction, float size, ImU32 color)
{
    const ImVec2 side(-direction.y, direction.x);
    const ImVec2 base(tip.x - direction.x * size, tip.y - direction.y * size);
    list->AddTriangleFilled(tip, ImVec2(base.x + side.x * size * 0.6f, base.y + side.y * size * 0.6f),
                            ImVec2(base.x - side.x * size * 0.6f, base.y - side.y * size * 0.6f), color);
}

void drawMove(ImDrawList* list, ImVec2 c, float r, ImU32 color, float t)
{
    const float head = r * 0.38f;
    const ImVec2 dirs[4] = {{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}};
    for (const ImVec2& d : dirs)
    {
        const ImVec2 tip(c.x + d.x * r, c.y + d.y * r);
        list->AddLine(c, ImVec2(tip.x - d.x * head * 0.5f, tip.y - d.y * head * 0.5f), color, t);
        arrowHead(list, tip, d, head, color);
    }
}

void drawRotate(ImDrawList* list, ImVec2 c, float r, ImU32 color, float t)
{
    const float radius = r * 0.82f;
    const float start  = -kPi * 0.15f;
    const float end    = kPi * 1.45f;
    list->PathArcTo(c, radius, start, end, 24);
    list->PathStroke(color, ImDrawFlags_None, t);
    const ImVec2 tip(c.x + std::cos(end) * radius, c.y + std::sin(end) * radius);
    const ImVec2 tangent(-std::sin(end), std::cos(end));
    arrowHead(list, ImVec2(tip.x + tangent.x * r * 0.18f, tip.y + tangent.y * r * 0.18f), tangent, r * 0.46f, color);
}

void drawScale(ImDrawList* list, ImVec2 c, float r, ImU32 color, float t)
{
    const float box = r * 0.62f;
    list->AddRect(ImVec2(c.x - r, c.y - box + r * 0.35f), ImVec2(c.x - r + box * 1.5f, c.y + r),
                  color, 2.f, ImDrawFlags_None, t);
    const ImVec2 from(c.x - r * 0.1f, c.y + r * 0.1f);
    const ImVec2 tip(c.x + r, c.y - r);
    list->AddLine(from, tip, color, t);
    arrowHead(list, tip, ImVec2(0.7071f, -0.7071f), r * 0.5f, color);
}

void drawFocus(ImDrawList* list, ImVec2 c, float r, ImU32 color, float t)
{
    list->AddCircle(c, r * 0.55f, color, 20, t);
    const float gap = r * 0.9f;
    list->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x - gap * 0.5f, c.y), color, t);
    list->AddLine(ImVec2(c.x + gap * 0.5f, c.y), ImVec2(c.x + r, c.y), color, t);
    list->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y - gap * 0.5f), color, t);
    list->AddLine(ImVec2(c.x, c.y + gap * 0.5f), ImVec2(c.x, c.y + r), color, t);
}

void drawPlay(ImDrawList* list, ImVec2 c, float r, ImU32 color)
{
    list->AddTriangleFilled(ImVec2(c.x - r * 0.65f, c.y - r), ImVec2(c.x - r * 0.65f, c.y + r),
                            ImVec2(c.x + r * 0.95f, c.y), color);
}

void drawStop(ImDrawList* list, ImVec2 c, float r, ImU32 color)
{
    list->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f), ImVec2(c.x + r * 0.8f, c.y + r * 0.8f), color, 2.f);
}

}

bool iconButton(const char* id, Icon icon, bool active, const char* tooltip, float size)
{
    if (size <= 0.f)
        size = ImGui::GetFrameHeight();

    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool pressed    = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered    = ImGui::IsItemHovered();
    const bool enabled    = !(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled);

    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 min = position;
    const ImVec2 max(position.x + size, position.y + size);
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

    ImVec4 tint = ImGui::GetStyle().Colors[ImGuiCol_Text];
    if (icon == Icon::Play)
        tint = ImVec4(0.38f, 0.88f, 0.55f, 1.f);
    else if (icon == Icon::Stop)
        tint = active ? ImVec4(0.937f, 0.267f, 0.267f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    else if (active)
        tint = ImVec4(0.506f, 0.549f, 0.973f, 1.f);
    if (!enabled)
        tint.w *= 0.4f;

    if (active || hovered)
    {
        const ImVec4 fill = active ? ImVec4(0.388f, 0.400f, 0.945f, 0.24f) : ImVec4(1.f, 1.f, 1.f, 0.07f);
        list->AddRectFilled(min, max, ImGui::GetColorU32(fill), 6.f);
    }

    const ImU32 color = ImGui::GetColorU32(tint);
    const float r     = size * 0.28f;
    const float t     = std::fmax(1.5f, size * 0.07f);
    switch (icon)
    {
    case Icon::Move:
        drawMove(list, center, r, color, t);
        break;
    case Icon::Rotate:
        drawRotate(list, center, r, color, t);
        break;
    case Icon::Scale:
        drawScale(list, center, r, color, t);
        break;
    case Icon::Focus:
        drawFocus(list, center, r, color, t);
        break;
    case Icon::Play:
        drawPlay(list, center, r, color);
        break;
    case Icon::Stop:
        drawStop(list, center, r, color);
        break;
    }

    if (tooltip && hovered)
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

}
