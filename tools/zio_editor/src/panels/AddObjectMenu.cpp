#include "panels/AddObjectMenu.h"

#include <imgui.h>

namespace zio_editor
{

void drawAddObjectItems(EditorContext& context)
{
    const bool enabled = context.project().isOpen();
    if (!enabled)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Nodo vacío"))
        context.addNode(NewNodeKind::Empty);
    if (ImGui::MenuItem("Cubo"))
        context.addNode(NewNodeKind::Cube);

    if (ImGui::BeginMenu("Malla (.obj)"))
    {
        const auto meshes = context.meshAssets();
        if (meshes.empty())
            ImGui::TextDisabled("No hay .obj en assets/models");
        for (const std::string& mesh : meshes)
            if (ImGui::MenuItem(mesh.c_str()))
                context.addNode(NewNodeKind::Mesh, mesh);
        ImGui::EndMenu();
    }

    if (!enabled)
        ImGui::EndDisabled();
}

}
