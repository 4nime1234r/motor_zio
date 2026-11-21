#include "ui/Panels.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <system_error>

#include "zio/jobs/JobSystem.h"
#include "zio/renderer/BatchRenderer.h"

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

using zio::ui::Color;
using zio::ui::Edges;
using zio::ui::MenuItem;
using zio::ui::TreeItem;
using zio::ui::Vec2;
using zio::ui::rgb;

const Color kFolderColor = rgb(0xfacc15);
const Color kSceneColor  = rgb(0x818cf8);
const Color kMeshColor   = rgb(0x66d98c);
const Color kNodeColor   = rgb(0x9ca3af);

class AdaptiveSplit final : public zio::ui::Widget
{
public:
    AdaptiveSplit(std::unique_ptr<zio::ui::Widget> first, std::unique_ptr<zio::ui::Widget> second)
        : firstRaw_(first.get()), secondRaw_(second.get()), firstOwned_(std::move(first)), secondOwned_(std::move(second))
    {
    }

    zio::ui::Vec2 contentMinSize() const override { return {0.f, 0.f}; }

    void layout() override
    {
        const bool wide = rect().w >= 620.f;
        if (!split_ || wide != wide_)
            rebuild(wide);
        split_->setRect(rect());
    }

private:
    void rebuild(bool wide)
    {
        if (split_)
        {
            firstOwned_  = split_->removeChild(firstRaw_);
            secondOwned_ = split_->removeChild(secondRaw_);
            removeChild(split_);
            split_ = nullptr;
        }
        auto split = std::make_unique<zio::ui::SplitContainer>(!wide);
        split->setRatio(wide ? 0.3f : 0.42f);
        split->addChild(std::move(firstOwned_));
        split->addChild(std::move(secondOwned_));
        split_ = static_cast<zio::ui::SplitContainer*>(addChild(std::move(split)));
        wide_  = wide;
    }

    zio::ui::Widget* firstRaw_;
    zio::ui::Widget* secondRaw_;
    std::unique_ptr<zio::ui::Widget> firstOwned_;
    std::unique_ptr<zio::ui::Widget> secondOwned_;
    zio::ui::SplitContainer* split_ = nullptr;
    bool wide_                      = false;
};

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

zio::ui::IconItem iconFor(const ContentBrowserModel::Entry& entry)
{
    zio::ui::IconItem item;
    item.text = entry.displayName;
    const std::string ext = extensionOf(entry.name);

    if (entry.isDirectory)
    {
        item.color  = kFolderColor;
        item.folder = true;
    }
    else if (entry.isScene)
    {
        item.color = kSceneColor;
        item.badge = "ESC";
    }
    else if (entry.isMesh || oneOf(ext, {"gltf", "glb", "fbx"}))
    {
        item.color = kMeshColor;
        item.badge = "3D";
    }
    else if (oneOf(ext, {"png", "jpg", "jpeg", "bmp", "tga"}))
    {
        item.color = rgb(0x4dd0e1);
        item.badge = "IMG";
    }
    else if (oneOf(ext, {"wav", "ogg", "mp3", "flac"}))
    {
        item.color = rgb(0xf472b6);
        item.badge = "SND";
    }
    else if (oneOf(ext, {"cs", "cpp", "cc", "c", "h", "hpp", "lua"}))
    {
        item.color = rgb(0xc084fc);
        item.badge = ext == "cs" ? "C#" : ((ext == "cpp" || ext == "cc") ? "C++" : upperCase(ext));
    }
    else
    {
        item.color = rgb(0x9ca3af);
        item.badge = upperCase(ext).substr(0, 4);
    }
    return item;
}

void setComponent(zio::math::Vec3& v, int axis, float value)
{
    if (axis == 0)
        v.x = value;
    else if (axis == 1)
        v.y = value;
    else
        v.z = value;
}

}

std::unique_ptr<zio::ui::Widget> HierarchyPanel::build()
{
    auto scroll = std::make_unique<zio::ui::ScrollContainer>(true, false);
    tree_       = scroll->add<zio::ui::TreeView>();

    tree_->setOnSelectionChanged([this]() {
        if (syncing_)
            return;
        std::vector<zio::scene3d::Node3D*> nodes;
        for (TreeItem* item : tree_->selection())
            nodes.push_back(static_cast<zio::scene3d::Node3D*>(item->userData));
        services_.context().selectNodes(std::move(nodes));
        lastSelection_ = services_.context().selection();
    });
    tree_->setOnContextMenu([this](TreeItem*, Vec2 position) {
        std::vector<MenuItem> items;
        items.push_back(MenuItem::submenuItem("Añadir objeto", services_.addObjectItems()));
        items.push_back(MenuItem::line());
        for (MenuItem& item : services_.editItems())
            items.push_back(std::move(item));
        items.push_back(MenuItem::line());
        MenuItem remove = MenuItem::item("Eliminar", [this]() { services_.context().removeSelected(); }, "Supr");
        remove.enabled  = !services_.context().selectionRoots().empty();
        items.push_back(std::move(remove));
        services_.showPopup(std::move(items), position);
    });
    return scroll;
}

void HierarchyPanel::addChildren(TreeItem* parent, zio::scene3d::Node3D& node,
                                 const std::unordered_map<const void*, bool>& expanded)
{
    for (const auto& child : node.children())
    {
        const bool isMesh = dynamic_cast<const zio::scene3d::MeshNode*>(child.get()) != nullptr;
        TreeItem* item    = parent->add(child->name().empty() ? "(sin nombre)" : child->name());
        item->userData    = child.get();
        item->iconColor   = isMesh ? kMeshColor : kNodeColor;
        const auto it     = expanded.find(child.get());
        item->expanded    = it == expanded.end() ? true : it->second;
        addChildren(item, *child, expanded);
    }
}

void HierarchyPanel::markSelected(TreeItem* item)
{
    auto& ctx = services_.context();
    item->selected = item->userData && ctx.isSelected(static_cast<zio::scene3d::Node3D*>(item->userData));
    for (const auto& child : item->children())
        markSelected(child.get());
}

void HierarchyPanel::pushSelection()
{
    syncing_ = true;
    markSelected(tree_->root());
    lastSelection_ = services_.context().selection();
    syncing_       = false;
}

void HierarchyPanel::rebuild()
{
    auto& ctx = services_.context();
    std::unordered_map<const void*, bool> expanded;
    std::vector<TreeItem*> stack = {tree_->root()};
    while (!stack.empty())
    {
        TreeItem* item = stack.back();
        stack.pop_back();
        for (const auto& child : item->children())
        {
            if (child->userData)
                expanded[child->userData] = child->expanded;
            stack.push_back(child.get());
        }
    }

    tree_->root()->clear();
    zio::scene3d::Node3D& root = ctx.scene().root();
    TreeItem* top              = tree_->root()->add(root.name().empty() ? "Escena" : root.name());
    top->userData              = &root;
    top->iconColor             = kSceneColor;
    const auto it              = expanded.find(&root);
    top->expanded              = it == expanded.end() ? true : it->second;
    addChildren(top, root, expanded);
    tree_->rebuildRows();
    pushSelection();
}

void HierarchyPanel::refresh()
{
    auto& ctx = services_.context();
    if (ctx.changeStamp() != lastStamp_ || ctx.sceneRevision() != lastRevision_)
    {
        lastStamp_    = ctx.changeStamp();
        lastRevision_ = ctx.sceneRevision();
        rebuild();
        return;
    }
    if (ctx.selection() != lastSelection_)
        pushSelection();
}

zio::ui::NumberEdit* InspectorPanel::addVector(zio::ui::Widget* parent, const char* title, zio::ui::NumberEdit* fields[3],
                                               double step, std::function<void()> onChanged)
{
    parent->add<zio::ui::Label>(std::string(title));
    auto* row = parent->add<zio::ui::HBox>();
    const std::pair<const char*, Color> axes[] = {{"X", rgb(0xef4444)}, {"Y", rgb(0x4ade80)}, {"Z", rgb(0x60a5fa)}};
    for (int i = 0; i < 3; ++i)
    {
        auto* field = row->add<zio::ui::NumberEdit>();
        field->setStep(step);
        field->setDecimals(3);
        field->setLabel(axes[i].first, axes[i].second);
        field->setExpand(true, false);
        field->setOnChanged([onChanged](double) { onChanged(); });
        fields[i] = field;
    }
    return fields[0];
}

std::unique_ptr<zio::ui::Widget> InspectorPanel::build()
{
    auto scroll = std::make_unique<zio::ui::ScrollContainer>(true, false);
    auto* panel = scroll->add<zio::ui::Panel>();
    panel->setPadding(Edges::all(10.f));
    auto* column = panel->add<zio::ui::VBox>();

    title_ = column->add<zio::ui::Label>(std::string());
    title_->setBold(true);
    multi_ = column->add<zio::ui::Label>(std::string());
    multi_->setColor(zio::ui::rgb(0x9ca3af));
    hint_ = column->add<zio::ui::Label>(std::string("Selecciona un nodo en la Jerarquía."));
    hint_->setColor(zio::ui::rgb(0x9ca3af));

    fields_ = column->add<zio::ui::VBox>();
    fields_->add<zio::ui::Separator>();
    fields_->add<zio::ui::Label>(std::string("Nombre"));
    name_ = fields_->add<zio::ui::LineEdit>();
    name_->setOnChanged([this](const std::string& text) {
        if (syncing_)
            return;
        if (auto* node = services_.context().selected())
        {
            node->setName(text);
            services_.context().markDirty();
        }
    });

    auto makeCallback = [this](zio::ui::NumberEdit** fields, int kind) {
        return [this, fields, kind]() {
            if (syncing_)
                return;
            auto* node = services_.context().selected();
            if (!node)
                return;
            if (kind == 1)
            {
                zio::math::Vec3& angles = eulers_.anglesFor(*node);
                angles = {static_cast<float>(fields[0]->value()), static_cast<float>(fields[1]->value()),
                          static_cast<float>(fields[2]->value())};
                eulers_.apply(*node);
            }
            else
            {
                zio::math::Vec3 v = kind == 0 ? node->position() : node->scale();
                for (int i = 0; i < 3; ++i)
                    if (!fields[i]->editing())
                        setComponent(v, i, static_cast<float>(fields[i]->value()));
                if (kind == 0)
                    node->setPosition(v);
                else
                    node->setScale(v);
            }
            services_.context().markDirty();
        };
    };

    addVector(fields_, "Posición", pos_, 0.05, makeCallback(pos_, 0));
    addVector(fields_, "Rotación (°)", rot_, 1.0, makeCallback(rot_, 1));
    addVector(fields_, "Escala", scale_, 0.05, makeCallback(scale_, 2));

    meshBox_ = fields_->add<zio::ui::VBox>();
    meshBox_->add<zio::ui::Separator>();
    meshBox_->add<zio::ui::Label>(std::string("Color"));
    color_ = meshBox_->add<zio::ui::ColorButton>();
    color_->setOnChanged([this](zio::ui::Color c) {
        if (syncing_)
            return;
        if (auto* mesh = services_.context().selectedMesh())
        {
            mesh->setColor({c.r, c.g, c.b, c.a});
            services_.context().markDirty();
        }
    });

    meshBox_->add<zio::ui::Label>(std::string("Escala UV"));
    auto* uvRow = meshBox_->add<zio::ui::HBox>();
    for (int i = 0; i < 2; ++i)
    {
        auto* field = uvRow->add<zio::ui::NumberEdit>();
        field->setStep(0.05);
        field->setDecimals(3);
        field->setLabel(i == 0 ? "U" : "V");
        field->setExpand(true, false);
        field->setOnChanged([this](double) {
            if (syncing_)
                return;
            if (auto* mesh = services_.context().selectedMesh())
            {
                mesh->setUvScale({static_cast<float>(uv_[0]->value()), static_cast<float>(uv_[1]->value())});
                services_.context().markDirty();
            }
        });
        uv_[i] = field;
    }

    shadow_ = meshBox_->add<zio::ui::CheckBox>(std::string("Proyecta sombra"));
    shadow_->setOnToggled([this](bool checked) {
        if (syncing_)
            return;
        if (auto* mesh = services_.context().selectedMesh())
        {
            mesh->setCastShadow(checked);
            services_.context().markDirty();
        }
    });
    return scroll;
}

void InspectorPanel::refresh()
{
    auto& ctx   = services_.context();
    auto* node  = ctx.selected();
    auto* mesh  = ctx.selectedMesh();

    hint_->setVisible(node == nullptr);
    title_->setVisible(node != nullptr);
    fields_->setVisible(node != nullptr);
    multi_->setVisible(node != nullptr && ctx.selection().size() > 1);
    meshBox_->setVisible(mesh != nullptr);
    fields_->setEnabled(!ctx.playing());
    if (!node)
        return;

    syncing_ = true;
    const std::string label = node->name().empty() ? (node->parent() ? "(sin nombre)" : "Escena") : node->name();
    title_->setText(label + (mesh ? "   [Malla]" : "   [Nodo 3D]"));
    if (ctx.selection().size() > 1)
        multi_->setText("(+" + std::to_string(ctx.selection().size() - 1) + " más seleccionados; se edita el último)");

    if (!name_->focused())
        name_->setText(node->name());

    const zio::math::Vec3 p = node->position();
    const zio::math::Vec3 s = node->scale();
    const zio::math::Vec3& e = eulers_.anglesFor(*node);
    const float pv[3] = {p.x, p.y, p.z};
    const float sv[3] = {s.x, s.y, s.z};
    const float ev[3] = {e.x, e.y, e.z};
    for (int i = 0; i < 3; ++i)
    {
        if (!pos_[i]->editing())
            pos_[i]->setValue(pv[i]);
        if (!rot_[i]->editing())
            rot_[i]->setValue(ev[i]);
        if (!scale_[i]->editing())
            scale_[i]->setValue(sv[i]);
    }

    if (mesh)
    {
        if (!services_.ui().hasPopups())
        {
            const auto c = mesh->color();
            color_->setColor({c.r, c.g, c.b, c.a});
        }
        const zio::math::Vec2 uv = mesh->uvScale();
        if (!uv_[0]->editing())
            uv_[0]->setValue(uv.x);
        if (!uv_[1]->editing())
            uv_[1]->setValue(uv.y);
        shadow_->setChecked(mesh->castShadow());
    }
    syncing_ = false;
}

std::unique_ptr<zio::ui::Widget> FilesPanel::build()
{
    auto column = std::make_unique<zio::ui::VBox>();
    column->setPadding(Edges::all(8.f));

    auto* top = column->add<zio::ui::HBox>();
    auto* up  = top->add<zio::ui::Button>(std::string("Subir"));
    up->setOnClick([this]() {
        if (model_.canGoUp())
        {
            model_.goUp();
            gridSignature_.clear();
            crumbSignature_.clear();
            timer_ = 10.f;
        }
    });
    crumbs_ = top->add<zio::ui::HBox>();
    crumbs_->setExpand(true, false);
    crumbs_->setSeparation(2.f);
    auto* mode = top->add<zio::ui::Button>(std::string("Lista"));
    mode->setToggle(true);
    mode->setTooltip("Cambia entre iconos y lista");
    mode->setOnToggled([this](bool list) {
        if (grid_)
            grid_->setListMode(list);
    });

    message_ = column->add<zio::ui::Label>(std::string("Abre o crea un proyecto desde el menú Proyecto."));
    message_->setColor(zio::ui::rgb(0x9ca3af));

    auto folderScroll = std::make_unique<zio::ui::ScrollContainer>(true, false);
    folders_          = folderScroll->add<zio::ui::TreeView>();
    folders_->setOnSelectionChanged([this]() {
        if (syncing_)
            return;
        const auto selection = folders_->selection();
        if (!selection.empty() && selection.front()->userData)
            navigate(*static_cast<std::string*>(selection.front()->userData));
    });

    auto gridScroll = std::make_unique<zio::ui::ScrollContainer>(true, false);
    grid_           = gridScroll->add<zio::ui::IconGrid>();
    grid_->setOnActivated([this](int index) { activate(index); });

    auto* split = column->add<AdaptiveSplit>(std::move(folderScroll), std::move(gridScroll));
    split->setExpand(true, true);
    body_ = split;
    return column;
}

void FilesPanel::navigate(const std::string& relative)
{
    if (model_.goTo(services_.context().project(), relative))
    {
        gridSignature_.clear();
        crumbSignature_.clear();
        timer_ = 10.f;
    }
}

void FilesPanel::activate(int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size()))
        return;
    const auto& entry = entries_[static_cast<std::size_t>(index)];
    auto& ctx         = services_.context();
    if (entry.isDirectory)
        navigate(entry.relativePath);
    else if (entry.isScene)
        ctx.request(Action{ActionKind::OpenScene, entry.relativePath, {}});
    else if (entry.isMesh)
        ctx.addNode(NewNodeKind::Mesh, entry.relativePath);
}

void FilesPanel::scan(FolderInfo& node, int depth) const
{
    if (depth >= 8)
        return;
    const auto absolute = services_.context().project().resolve(node.relative);
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
        FolderInfo child;
        child.name = it->path().filename().string();
        if (child.name.empty() || child.name.front() == '.')
            continue;
        child.relative = node.relative + "/" + child.name;
        child.display  = ContentBrowserModel::displayName(node.relative, child.name);
        scan(child, depth + 1);
        node.children.push_back(std::move(child));
    }
    std::sort(node.children.begin(), node.children.end(),
              [](const FolderInfo& a, const FolderInfo& b) { return lowerCase(a.display) < lowerCase(b.display); });
}

void FilesPanel::fillFolderTree(TreeItem* parent, const FolderInfo& info, const std::vector<std::string>& collapsed)
{
    pathStore_.push_back(info.relative);
    TreeItem* item  = parent->add(info.display);
    item->userData  = &pathStore_.back();
    item->iconColor = kFolderColor;
    item->expanded  = std::find(collapsed.begin(), collapsed.end(), info.relative) == collapsed.end();
    for (const FolderInfo& child : info.children)
        fillFolderTree(item, child, collapsed);
}

void FilesPanel::rescanFolders()
{
    FolderInfo root;
    root.name     = ContentBrowserModel::kRoot;
    root.display  = ContentBrowserModel::kRoot;
    root.relative = ContentBrowserModel::kRoot;
    scan(root, 0);

    std::string signature;
    std::vector<const FolderInfo*> stack = {&root};
    while (!stack.empty())
    {
        const FolderInfo* info = stack.back();
        stack.pop_back();
        signature += info->relative + "|";
        for (const FolderInfo& child : info->children)
            stack.push_back(&child);
    }

    if (signature != folderSignature_)
    {
        folderSignature_ = signature;
        std::vector<std::string> collapsed;
        std::vector<TreeItem*> items = {folders_->root()};
        while (!items.empty())
        {
            TreeItem* item = items.back();
            items.pop_back();
            for (const auto& child : item->children())
            {
                if (!child->expanded && child->userData)
                    collapsed.push_back(*static_cast<std::string*>(child->userData));
                items.push_back(child.get());
            }
        }
        folders_->root()->clear();
        pathStore_.clear();
        fillFolderTree(folders_->root(), root, collapsed);
        folders_->rebuildRows();
    }

    syncing_ = true;
    std::vector<TreeItem*> items = {folders_->root()};
    while (!items.empty())
    {
        TreeItem* item = items.back();
        items.pop_back();
        for (const auto& child : item->children())
        {
            child->selected = child->userData && *static_cast<std::string*>(child->userData) == model_.currentDir();
            items.push_back(child.get());
        }
    }
    syncing_ = false;
}

void FilesPanel::rebuildGrid()
{
    std::string error;
    entries_ = model_.list(services_.context().project(), &error);

    std::string signature = model_.currentDir() + "|";
    for (const auto& entry : entries_)
        signature += entry.name + (entry.isDirectory ? "/" : "") + "|";
    if (signature == gridSignature_)
        return;
    gridSignature_ = signature;

    std::vector<zio::ui::IconItem> items;
    for (const auto& entry : entries_)
        items.push_back(iconFor(entry));
    grid_->setItems(std::move(items));
}

void FilesPanel::rebuildCrumbs()
{
    const auto crumbs = model_.breadcrumbs();
    std::string signature;
    for (const auto& crumb : crumbs)
        signature += crumb.second + "|";
    if (signature == crumbSignature_)
        return;
    crumbSignature_ = signature;

    crumbs_->clearChildren();
    for (std::size_t i = 0; i < crumbs.size(); ++i)
    {
        if (i > 0)
            crumbs_->add<zio::ui::Label>(std::string("/"))->setColor(zio::ui::rgb(0x9ca3af));
        auto* button = crumbs_->add<zio::ui::Button>(crumbs[i].first);
        button->setFlat(true);
        const std::string target = crumbs[i].second;
        button->setOnClick([this, target]() { navigate(target); });
    }
}

void FilesPanel::refresh(float dt)
{
    const ProjectManager& project = services_.context().project();
    body_->setVisible(project.isOpen());
    message_->setVisible(!project.isOpen());
    crumbs_->setVisible(project.isOpen());
    if (!project.isOpen())
    {
        boundRoot_.clear();
        return;
    }

    if (boundRoot_ != project.rootDir())
    {
        boundRoot_ = project.rootDir();
        model_.reset();
        folderSignature_.clear();
        gridSignature_.clear();
        crumbSignature_.clear();
        timer_ = 10.f;
    }

    timer_ += dt;
    if (timer_ > 1.5f)
    {
        timer_ = 0.f;
        rescanFolders();
        rebuildGrid();
        rebuildCrumbs();
    }
}

std::unique_ptr<zio::ui::Widget> PerformancePanel::build()
{
    auto panel = std::make_unique<zio::ui::Panel>();
    panel->setPadding(Edges::all(12.f));
    auto* column = panel->add<zio::ui::VBox>();
    frame_  = column->add<zio::ui::Label>(std::string());
    memory_ = column->add<zio::ui::Label>(std::string());
    draw2d_ = column->add<zio::ui::Label>(std::string());
    jobs_   = column->add<zio::ui::Label>(std::string());
    return panel;
}

void PerformancePanel::refresh(float dt)
{
    timer_ += dt;
    if (timer_ < 0.25f)
        return;
    timer_ = 0.f;

    char buffer[160];
    const float ms  = services_.frameMs();
    const float fps = ms > 0.f ? 1000.f / ms : 0.f;
    std::snprintf(buffer, sizeof(buffer), "Fotograma: %.2f ms (%.0f FPS)", ms, fps);
    frame_->setText(buffer);

    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    {
        std::snprintf(buffer, sizeof(buffer), "Memoria en uso: %.1f MB",
                      static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0));
        memory_->setText(buffer);
    }

    const auto stats = zio::renderer::BatchRenderer::instance().lastFrameStats();
    std::snprintf(buffer, sizeof(buffer), "Dibujo 2D: %u llamadas, %u rectángulos", stats.drawCalls, stats.quadCount);
    draw2d_->setText(buffer);

    auto& jobs = zio::jobs::JobSystem::instance();
    std::snprintf(buffer, sizeof(buffer), "Tareas: %u hilos, %zu en cola, %zu pendientes", jobs.workerCount(),
                  jobs.queuedTasks(), jobs.pendingCompletions());
    jobs_->setText(buffer);
}

}
