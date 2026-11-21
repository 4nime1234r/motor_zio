#include "panels/SceneHierarchyPanel.h"

#include <imgui.h>

#include "panels/AddObjectMenu.h"

namespace zio_editor
{

void SceneHierarchyPanel::draw(EditorContext& context)
{
    ImGui::Begin("Jerarquía");
    drawNode(context, context.scene().root());

    if (ImGui::BeginPopupContextWindow("##jerarquia_vacio",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::IsWindowAppearing())
            context.clearSelection();
        drawAddObjectItems(context);
        ImGui::EndPopup();
    }
    ImGui::End();
}

void SceneHierarchyPanel::drawNode(EditorContext& context, zio::scene3d::Node3D& node)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (context.isSelected(&node))
        flags |= ImGuiTreeNodeFlags_Selected;
    const bool isLeaf = node.children().empty();
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const bool isMesh = dynamic_cast<const zio::scene3d::MeshNode*>(&node) != nullptr;
    const std::string label =
        node.name().empty() ? (node.parent() ? "(sin nombre)" : "Escena") : node.name();

    const bool open = ImGui::TreeNodeEx(&node, flags, "%s  [%s]", label.c_str(), isMesh ? "Malla" : "Nodo 3D");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        if (ImGui::GetIO().KeyCtrl)
            context.toggleSelect(&node);
        else
            context.select(&node);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !context.isSelected(&node))
        context.select(&node);
    if (ImGui::BeginPopupContextItem())
    {
        drawAddObjectItems(context);
        ImGui::EndPopup();
    }

    if (open && !isLeaf)
    {
        for (const auto& child : node.children())
            drawNode(context, *child);
        ImGui::TreePop();
    }
}

}
