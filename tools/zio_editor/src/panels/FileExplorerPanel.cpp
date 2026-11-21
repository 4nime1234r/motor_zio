#include "panels/FileExplorerPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <system_error>

#include "core/LayoutSettings.h"

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

enum class FileKind
{
    Folder,
    Scene,
    Model,
    Texture,
    Audio,
    Script,
    Other
};

constexpr float kCellWidth      = 96.f;
constexpr float kGridIconSize   = 52.f;
constexpr float kListIconSize   = 20.f;
constexpr float kWideThreshold  = 620.f;
constexpr int kMaxTreeDepth     = 8;

std::string lowerCase(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string upperCase(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string extensionOf(const std::string& name)
{
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return {};
    return lowerCase(name.substr(dot + 1));
}

bool oneOf(const std::string& ext, std::initializer_list<const char*> list)
{
    for (const char* item : list)
        if (ext == item)
            return true;
    return false;
}

FileKind classify(const ContentBrowserModel::Entry& entry, const std::string& ext)
{
    if (entry.isDirectory)
        return FileKind::Folder;
    if (entry.isScene)
        return FileKind::Scene;
    if (entry.isMesh || oneOf(ext, {"gltf", "glb", "fbx"}))
        return FileKind::Model;
    if (oneOf(ext, {"png", "jpg", "jpeg", "bmp", "tga"}))
        return FileKind::Texture;
    if (oneOf(ext, {"wav", "ogg", "mp3", "flac"}))
        return FileKind::Audio;
    if (oneOf(ext, {"cs", "cpp", "cc", "c", "h", "hpp", "lua"}))
        return FileKind::Script;
    return FileKind::Other;
}

ImVec4 kindColor(FileKind kind)
{
    switch (kind)
    {
    case FileKind::Folder:
        return ImVec4(0.98f, 0.78f, 0.35f, 1.f);
    case FileKind::Scene:
        return ImVec4(0.506f, 0.549f, 0.973f, 1.f);
    case FileKind::Model:
        return ImVec4(0.40f, 0.85f, 0.55f, 1.f);
    case FileKind::Texture:
        return ImVec4(0.30f, 0.78f, 0.85f, 1.f);
    case FileKind::Audio:
        return ImVec4(0.95f, 0.50f, 0.70f, 1.f);
    case FileKind::Script:
        return ImVec4(0.75f, 0.55f, 0.98f, 1.f);
    case FileKind::Other:
        break;
    }
    return ImVec4(0.62f, 0.64f, 0.72f, 1.f);
}

std::string kindLabel(FileKind kind, const std::string& ext)
{
    switch (kind)
    {
    case FileKind::Scene:
        return "ESC";
    case FileKind::Model:
        return "3D";
    case FileKind::Texture:
        return "IMG";
    case FileKind::Audio:
        return "SND";
    case FileKind::Script:
        if (ext == "cs")
            return "C#";
        if (ext == "cpp" || ext == "cc")
            return "C++";
        return upperCase(ext);
    default:
        break;
    }
    std::string label = upperCase(ext);
    if (label.size() > 4)
        label.resize(4);
    return label;
}

void drawIcon(ImDrawList* list, ImVec2 origin, float size, FileKind kind, const std::string& ext)
{
    const ImVec4 base = kindColor(kind);
    const ImU32 line  = ImGui::GetColorU32(base);
    const ImU32 fill  = ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, 0.16f));
    const ImVec2 min  = origin;
    const ImVec2 max  = ImVec2(origin.x + size, origin.y + size);
    const bool small  = size < 30.f;

    if (kind == FileKind::Folder)
    {
        const float tabWidth = size * 0.42f;
        const float tabTop   = min.y + size * 0.12f;
        const float bodyTop  = min.y + size * 0.26f;
        const float round    = small ? 2.f : 5.f;
        list->AddRectFilled(ImVec2(min.x, tabTop), ImVec2(min.x + tabWidth, bodyTop + round), line, round);
        list->AddRectFilled(ImVec2(min.x, bodyTop), ImVec2(max.x, max.y - size * 0.08f), line, round);
        if (!small)
            list->AddRectFilled(ImVec2(min.x, bodyTop + 5.f), ImVec2(max.x, max.y - size * 0.08f),
                                ImGui::GetColorU32(ImVec4(base.x * 0.85f, base.y * 0.85f, base.z * 0.85f, 1.f)), round);
        return;
    }

    const float inset = size * 0.14f;
    const ImVec2 pageMin(min.x + inset, min.y);
    const ImVec2 pageMax(max.x - inset, max.y);
    const float fold  = size * 0.24f;
    const float round = small ? 2.f : 6.f;

    list->AddRectFilled(pageMin, pageMax, fill, round);
    list->AddRect(pageMin, pageMax, line, round, ImDrawFlags_None, small ? 1.2f : 1.6f);
    list->AddTriangleFilled(ImVec2(pageMax.x - fold, pageMin.y), ImVec2(pageMax.x, pageMin.y + fold),
                            ImVec2(pageMax.x - fold, pageMin.y + fold), line);

    if (small)
        return;

    const std::string label = kindLabel(kind, ext);
    if (!label.empty())
    {
        const float fontSize = ImGui::GetFontSize() * 0.95f;
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000.f, 0.f, label.c_str());
        list->AddText(ImGui::GetFont(), fontSize,
                      ImVec2((pageMin.x + pageMax.x - textSize.x) * 0.5f, min.y + size * 0.56f - textSize.y * 0.5f),
                      line, label.c_str());
    }
}

std::string fitLabel(const std::string& text, float width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    std::string cut = text;
    while (cut.size() > 1 && ImGui::CalcTextSize((cut + "...").c_str()).x > width)
        cut.pop_back();
    return cut + "...";
}

}

void FileExplorerPanel::scan(const ProjectManager& project, FolderNode& node, int depth) const
{
    if (depth >= kMaxTreeDepth)
        return;
    const auto absolute = project.resolve(node.relativePath);
    if (!absolute)
        return;

    std::error_code ec;
    fs::directory_iterator it(*absolute, ec);
    if (ec)
        return;
    for (const fs::directory_iterator end; it != end; it.increment(ec))
    {
        if (ec)
            break;
        std::error_code entryEc;
        if (!it->is_directory(entryEc))
            continue;
        FolderNode child;
        child.name = it->path().filename().string();
        if (!child.name.empty() && child.name.front() == '.')
            continue;
        child.relativePath = node.relativePath + "/" + child.name;
        child.display      = ContentBrowserModel::displayName(node.relativePath, child.name);
        scan(project, child, depth + 1);
        node.children.push_back(std::move(child));
    }
    std::sort(node.children.begin(), node.children.end(), [](const FolderNode& a, const FolderNode& b) {
        return lowerCase(a.display) < lowerCase(b.display);
    });
}

void FileExplorerPanel::rescan(const ProjectManager& project)
{
    tree_              = FolderNode{};
    tree_.name         = ContentBrowserModel::kRoot;
    tree_.display      = ContentBrowserModel::kRoot;
    tree_.relativePath = ContentBrowserModel::kRoot;
    scan(project, tree_, 0);
    lastScan_ = ImGui::GetTime();
}

void FileExplorerPanel::drawTree(const ProjectManager& project, const FolderNode& node)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (node.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (model_.currentDir() == node.relativePath)
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushStyleColor(ImGuiCol_Text, kindColor(FileKind::Folder));
    const bool open = ImGui::TreeNodeEx(node.relativePath.c_str(), flags, "%s", node.display.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        model_.goTo(project, node.relativePath);
        selected_.clear();
    }
    if (open)
    {
        for (const FolderNode& child : node.children)
            drawTree(project, child);
        ImGui::TreePop();
    }
}

void FileExplorerPanel::drawFiles(EditorContext& context)
{
    const ProjectManager& project = context.project();

    if (ImGui::SmallButton("Subir") && model_.canGoUp())
    {
        model_.goUp();
        selected_.clear();
    }
    ImGui::SameLine();
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

    const char* modeLabel = listView_ ? "Iconos" : "Lista";
    const float modeWidth = ImGui::CalcTextSize(modeLabel).x + ImGui::GetStyle().FramePadding.x * 2.f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - modeWidth -
                                                         ImGui::GetStyle().WindowPadding.x));
    if (ImGui::SmallButton(modeLabel))
        listView_ = !listView_;

    ImGui::Separator();

    std::string listError;
    const auto entries = model_.list(project, &listError);
    if (!listError.empty())
        ImGui::TextColored(ImVec4(0.937f, 0.267f, 0.267f, 1.f), "%s", listError.c_str());
    else if (entries.empty())
        ImGui::TextDisabled("Esta carpeta está vacía.");

    enum class Step
    {
        None,
        Enter,
        OpenScene,
        AddMesh
    };
    Step step = Step::None;
    std::string stepTarget;

    auto activate = [&](const ContentBrowserModel::Entry& entry) {
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
    };

    ImDrawList* list = ImGui::GetWindowDrawList();

    if (listView_)
    {
        const float rowHeight = std::max(ImGui::GetFrameHeight(), kListIconSize + 4.f);
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry   = entries[i];
            const std::string ext = extensionOf(entry.name);
            const FileKind kind = classify(entry, ext);

            ImGui::PushID(static_cast<int>(i));
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##fila", selected_ == entry.relativePath,
                                                   ImGuiSelectableFlags_AllowDoubleClick,
                                                   ImVec2(0.f, rowHeight));
            drawIcon(list, ImVec2(row.x + 4.f, row.y + (rowHeight - kListIconSize) * 0.5f), kListIconSize, kind, ext);
            list->AddText(ImVec2(row.x + 4.f + kListIconSize + 8.f, row.y + (rowHeight - ImGui::GetFontSize()) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_Text), entry.displayName.c_str());
            ImGui::PopID();

            if (clicked)
            {
                selected_ = entry.relativePath;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    activate(entry);
            }
        }
    }
    else
    {
        const float avail      = ImGui::GetContentRegionAvail().x;
        const int columns      = std::max(1, static_cast<int>(avail / kCellWidth));
        const float cellHeight = kGridIconSize + ImGui::GetTextLineHeight() + 20.f;

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry   = entries[i];
            const std::string ext = extensionOf(entry.name);
            const FileKind kind = classify(entry, ext);

            ImGui::PushID(static_cast<int>(i));
            const ImVec2 cell  = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##entrada", selected_ == entry.relativePath,
                                                   ImGuiSelectableFlags_AllowDoubleClick,
                                                   ImVec2(kCellWidth - 6.f, cellHeight));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", entry.displayName.c_str());

            drawIcon(list, ImVec2(cell.x + (kCellWidth - 6.f - kGridIconSize) * 0.5f, cell.y + 5.f), kGridIconSize,
                     kind, ext);

            const std::string label = fitLabel(entry.displayName, kCellWidth - 14.f);
            const ImVec2 textSize   = ImGui::CalcTextSize(label.c_str());
            list->AddText(ImVec2(cell.x + (kCellWidth - 6.f - textSize.x) * 0.5f, cell.y + kGridIconSize + 10.f),
                          ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            ImGui::PopID();

            if (clicked)
            {
                selected_ = entry.relativePath;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    activate(entry);
            }

            if ((static_cast<int>(i) + 1) % columns != 0 && i + 1 < entries.size())
                ImGui::SameLine();
        }
    }

    switch (step)
    {
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
}

void FileExplorerPanel::draw(EditorContext& context)
{
    ImGui::Begin(panelTitle(PanelFiles));

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
        lastScan_ = -10.0;
    }
    if (ImGui::GetTime() - lastScan_ > 1.5)
        rescan(project);

    const bool wide = ImGui::GetContentRegionAvail().x >= kWideThreshold;
    if (wide)
    {
        ImGui::BeginChild("##carpetas", ImVec2(treeWidth_, 0.f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        drawTree(project, tree_);
        treeWidth_ = ImGui::GetWindowWidth();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##archivos", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
        drawFiles(context);
        ImGui::EndChild();
    }
    else
    {
        const float maxTree = std::max(60.f, ImGui::GetContentRegionAvail().y - 80.f);
        treeHeight_         = std::clamp(treeHeight_, 60.f, maxTree);
        ImGui::BeginChild("##carpetas", ImVec2(0.f, treeHeight_), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        drawTree(project, tree_);
        treeHeight_ = ImGui::GetWindowHeight();
        ImGui::EndChild();

        ImGui::BeginChild("##archivos", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
        drawFiles(context);
        ImGui::EndChild();
    }

    ImGui::End();
}

}
