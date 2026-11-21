#include "panels/ContentBrowserPanel.h"

#include <imgui.h>

namespace zio_editor
{

void ContentBrowserPanel::draw(EditorContext& context)
{
    ImGui::Begin("Contenido");

    const ProjectManager& project = context.project();
    if (!project.isOpen())
    {
        boundRoot_.clear();
        ImGui::TextDisabled("Abre o crea un proyecto desde el menú Proyecto.");
        ImGui::End();
        return;
    }

    if (boundRoot_ != project.rootDir())
    {
        boundRoot_ = project.rootDir();
        model_.reset();
        selected_.clear();
    }

    const auto crumbs = model_.breadcrumbs();
    std::string gotoDir;
    for (std::size_t i = 0; i < crumbs.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(crumbs[i].first.c_str()))
            gotoDir = crumbs[i].second;
        ImGui::PopID();
    }
    if (!gotoDir.empty() && model_.goTo(project, gotoDir))
        selected_.clear();

    ImGui::Separator();

    std::string listError;
    const auto entries = model_.list(project, &listError);

    enum class Step
    {
        None,
        Up,
        Enter,
        OpenScene,
        AddMesh
    };
    Step step = Step::None;
    std::string stepTarget;

    if (model_.canGoUp())
    {
        if (ImGui::Selectable("[ .. ]", false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            step = Step::Up;
    }

    for (const auto& entry : entries)
    {
        const std::string label = entry.isDirectory ? "[" + entry.displayName + "]" : entry.name;
        if (entry.isScene)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.506f, 0.549f, 0.973f, 1.f));
        else if (entry.isMesh)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.f, 0.65f, 1.f));
        const bool clicked = ImGui::Selectable(label.c_str(), selected_ == entry.relativePath,
                                               ImGuiSelectableFlags_AllowDoubleClick);
        if (entry.isScene || entry.isMesh)
            ImGui::PopStyleColor();

        if (!clicked)
            continue;
        selected_ = entry.relativePath;
        if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            continue;
        if (entry.isDirectory)
        {
            step       = Step::Enter;
            stepTarget = entry.name;
        }
        else if (entry.isScene)
        {
            step       = Step::OpenScene;
            stepTarget = entry.relativePath;
        }
        else if (entry.isMesh)
        {
            step       = Step::AddMesh;
            stepTarget = entry.relativePath;
        }
    }

    if (!listError.empty())
        ImGui::TextColored(ImVec4(0.937f, 0.267f, 0.267f, 1.f), "%s", listError.c_str());

    switch (step)
    {
    case Step::Up:
        model_.goUp();
        selected_.clear();
        break;
    case Step::Enter:
        model_.enter(project, stepTarget);
        selected_.clear();
        break;
    case Step::OpenScene:
        context.request(Action{ActionKind::OpenScene, stepTarget, {}});
        break;
    case Step::AddMesh:
        context.addNode(NewNodeKind::Mesh, stepTarget);
        break;
    case Step::None:
        break;
    }

    ImGui::End();
}

}
