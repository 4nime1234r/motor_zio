#include "panels/InspectorPanel.h"

#include <imgui.h>

namespace zio_editor
{

void InspectorPanel::draw(EditorContext& context)
{
    ImGui::Begin("Inspector");

    zio::scene3d::Node3D* node = context.selected();
    if (!node)
    {
        ImGui::TextDisabled("Selecciona un nodo en la Jerarquía.");
        ImGui::End();
        return;
    }

    zio::scene3d::MeshNode* mesh = context.selectedMesh();
    const std::string& name      = node->name();
    ImGui::Text("%s", name.empty() ? (node->parent() ? "(sin nombre)" : "Escena") : name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", mesh ? "Malla" : "Nodo 3D");
    if (context.selection().size() > 1)
        ImGui::TextDisabled("(+%d más seleccionados; se edita el último)",
                            static_cast<int>(context.selection().size()) - 1);
    ImGui::Separator();
    ImGui::BeginDisabled(context.playing());

    zio::math::Vec3 position = node->position();
    if (ImGui::DragFloat3("Posición", &position.x, 0.05f))
    {
        node->setPosition(position);
        context.markDirty();
    }

    zio::math::Vec3& euler = eulerCache_.anglesFor(*node);
    if (ImGui::DragFloat3("Rotación (°)", &euler.x, 0.5f))
    {
        eulerCache_.apply(*node);
        context.markDirty();
    }

    zio::math::Vec3 scale = node->scale();
    if (ImGui::DragFloat3("Escala", &scale.x, 0.01f))
    {
        node->setScale(scale);
        context.markDirty();
    }

    if (mesh)
    {
        ImGui::Separator();

        const zio::renderer::Color color = mesh->color();
        float rgba[4]                    = {color.r, color.g, color.b, color.a};
        if (ImGui::ColorEdit4("Color", rgba))
        {
            mesh->setColor({rgba[0], rgba[1], rgba[2], rgba[3]});
            context.markDirty();
        }

        zio::math::Vec2 uvScale = mesh->uvScale();
        if (ImGui::DragFloat2("Escala UV", &uvScale.x, 0.01f))
        {
            mesh->setUvScale(uvScale);
            context.markDirty();
        }

        bool castShadow = mesh->castShadow();
        if (ImGui::Checkbox("Proyecta sombra", &castShadow))
        {
            mesh->setCastShadow(castShadow);
            context.markDirty();
        }
    }

    ImGui::EndDisabled();
    ImGui::End();
}

}
