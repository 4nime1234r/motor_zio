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
#include <ctime>
#include <set>
#include <system_error>
#include <shellapi.h>

#include "ui/EditorIcons.h"
#include "zio/scripting/Script.h"
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

const Color kSceneColor  = rgb(0x818cf8);
const Color kMeshColor   = rgb(0x66d98c);
const Color kNodeColor   = rgb(0x9ca3af);

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

zio::ui::IconItem iconFor(const AssetEntry& entry)
{
    zio::ui::IconItem item;
    item.text = entry.relative.size() > 7 ? entry.relative.substr(7) : entry.relative;
    const std::string& ext = entry.extension;

    const char* icon = "file";
    if (ext == "zscene3d")
    {
        item.color = kSceneColor;
        icon       = "clapperboard";
    }
    else if (oneOf(ext, {"obj", "gltf", "glb", "fbx", "bin"}))
    {
        item.color = kMeshColor;
        icon       = "box";
    }
    else if (oneOf(ext, {"png", "jpg", "jpeg", "bmp", "tga"}))
    {
        item.color = rgb(0x4dd0e1);
        icon       = "file-image";
    }
    else if (oneOf(ext, {"wav", "ogg", "mp3", "flac"}))
    {
        item.color = rgb(0xf472b6);
        icon       = "file-music";
    }
    else if (oneOf(ext, {"cs", "cpp", "cc", "c", "h", "hpp", "lua"}))
    {
        item.color = rgb(0xc084fc);
        icon       = "file-code";
    }
    else if (oneOf(ext, {"txt", "md", "json", "xml", "ini", "cfg"}))
    {
        item.color = rgb(0x9ca3af);
        icon       = "file-text";
    }
    else
    {
        item.color = rgb(0x9ca3af);
        item.badge = upperCase(ext).substr(0, 4);
    }
    item.icon = EditorIcons::get(icon);
    return item;
}

zio::ui::VBox* addFoldout(zio::ui::Widget* parent, const std::string& title, bool open)
{
    auto* box    = parent->add<zio::ui::VBox>();
    auto* header = box->add<zio::ui::Button>(std::string(open ? "\u25BE  " : "\u25B8  ") + title);
    header->setFlat(true);
    header->setAlignLeft(true);
    auto* body = box->add<zio::ui::VBox>();
    body->setVisible(open);
    header->setOnClick([header, body, title]() {
        const bool show = !body->visible();
        body->setVisible(show);
        header->setText(std::string(show ? "\u25BE  " : "\u25B8  ") + title);
    });
    return body;
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
    auto column = std::make_unique<zio::ui::VBox>();
    column->setPadding(Edges::all(6.f));
    auto* top = column->add<zio::ui::HBox>();
    add_      = top->add<zio::ui::Button>(std::string("+"));
    add_->setTooltip("Añadir objeto");
    add_->setOnClick([this]() {
        const zio::ui::Rect r = add_->rect();
        services_.showPopup(services_.addObjectItems(), {r.x, r.bottom()});
    });
    filter_ = top->add<zio::ui::LineEdit>();
    filter_->setPlaceholder("Filtrar nodos");
    filter_->setExpand(true, false);
    filter_->setOnChanged([this](const std::string& text) {
        std::string lowered = text;
        for (char& c : lowered)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        filterText_ = lowered;
    });

    auto* scroll = column->add<zio::ui::ScrollContainer>(true, false);
    scroll->setExpand(true, true);
    tree_ = scroll->add<zio::ui::TreeView>();

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
    return column;
}

bool HierarchyPanel::matchesFilter(const zio::scene3d::Node3D& node) const
{
    if (filterText_.empty())
        return true;
    std::string name = node.name();
    for (char& c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.find(filterText_) != std::string::npos)
        return true;
    for (const auto& child : node.children())
        if (matchesFilter(*child))
            return true;
    return false;
}

void HierarchyPanel::addChildren(TreeItem* parent, zio::scene3d::Node3D& node,
                                 const std::unordered_map<const void*, bool>& expanded)
{
    for (const auto& child : node.children())
    {
        if (!matchesFilter(*child))
            continue;
        const bool isMesh = dynamic_cast<const zio::scene3d::MeshNode*>(child.get()) != nullptr;
        TreeItem* item    = parent->add(child->name().empty() ? "(sin nombre)" : child->name());
        item->userData    = child.get();
        item->iconColor   = isMesh ? kMeshColor : kNodeColor;
        item->icon        = EditorIcons::get(isMesh ? "box" : "axis-3d");
        const auto it     = expanded.find(child.get());
        item->expanded    = filterText_.empty() ? (it == expanded.end() ? true : it->second) : true;
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
    top->icon                  = EditorIcons::get("clapperboard");
    const auto it              = expanded.find(&root);
    top->expanded              = it == expanded.end() ? true : it->second;
    addChildren(top, root, expanded);
    tree_->rebuildRows();
    pushSelection();
}

void HierarchyPanel::refresh()
{
    auto& ctx = services_.context();
    if (ctx.changeStamp() != lastStamp_ || ctx.sceneRevision() != lastRevision_ || filterText_ != lastFilter_)
    {
        lastFilter_   = filterText_;
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

    auto* transform = addFoldout(fields_, "Transform", true);
    addVector(transform, "Posición", pos_, 0.05, makeCallback(pos_, 0));
    addVector(transform, "Rotación (°)", rot_, 1.0, makeCallback(rot_, 1));
    addVector(transform, "Escala", scale_, 0.05, makeCallback(scale_, 2));

    meshBox_       = fields_->add<zio::ui::VBox>();
    auto* meshBody = addFoldout(meshBox_, "Malla", true);
    meshBody->add<zio::ui::Label>(std::string("Color"));
    color_ = meshBody->add<zio::ui::ColorButton>();
    color_->setOnChanged([this](zio::ui::Color c) {
        if (syncing_)
            return;
        if (auto* mesh = services_.context().selectedMesh())
        {
            mesh->setColor({c.r, c.g, c.b, c.a});
            services_.context().markDirty();
        }
    });

    meshBody->add<zio::ui::Label>(std::string("Escala UV"));
    auto* uvRow = meshBody->add<zio::ui::HBox>();
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

    shadow_ = meshBody->add<zio::ui::CheckBox>(std::string("Proyecta sombra"));
    shadow_->setOnToggled([this](bool checked) {
        if (syncing_)
            return;
        if (auto* mesh = services_.context().selectedMesh())
        {
            mesh->setCastShadow(checked);
            services_.context().markDirty();
        }
    });

    animBox_       = meshBox_->add<zio::ui::VBox>();
    auto* animBody = addFoldout(animBox_, "Animación", true);
    auto* pick     = animBody->add<zio::ui::HBox>();
    auto* prev = pick->add<zio::ui::Button>(std::string("<"));
    animName_  = pick->add<zio::ui::Label>(std::string());
    animName_->setExpand(true, false);
    auto* next = pick->add<zio::ui::Button>(std::string(">"));

    const auto step = [this](int delta) {
        auto* mesh = services_.context().selectedMesh();
        if (!mesh || syncing_)
            return;
        const auto names = mesh->animationNames();
        if (names.empty())
            return;
        const int count = static_cast<int>(names.size());
        animIndex_      = ((animIndex_ + delta) % count + count) % count;
        if (mesh->isAnimationPlaying())
            mesh->playAnimation(names[static_cast<std::size_t>(animIndex_)], mesh->animationLoops(), 0.25f);
        services_.context().markDirty();
    };
    prev->setOnClick([step]() { step(-1); });
    next->setOnClick([step]() { step(1); });

    auto* controls = animBody->add<zio::ui::HBox>();
    animPlay_      = controls->add<zio::ui::Button>(std::string("Reproducir"));
    animPlay_->setOnClick([this]() {
        auto* mesh = services_.context().selectedMesh();
        if (!mesh || syncing_)
            return;
        if (mesh->isAnimationPlaying())
        {
            mesh->stopAnimation();
        }
        else
        {
            const auto names = mesh->animationNames();
            if (names.empty())
                return;
            animIndex_ = std::clamp(animIndex_, 0, static_cast<int>(names.size()) - 1);
            mesh->playAnimation(names[static_cast<std::size_t>(animIndex_)], animLoop_->checked());
        }
        services_.context().markDirty();
    });
    animLoop_ = controls->add<zio::ui::CheckBox>(std::string("Bucle"));
    animLoop_->setChecked(true);
    animLoop_->setOnToggled([this](bool checked) {
        auto* mesh = services_.context().selectedMesh();
        if (!mesh || syncing_)
            return;
        mesh->setAnimationLoop(checked);
        services_.context().markDirty();
    });

    animSpeed_ = animBody->add<zio::ui::NumberEdit>();
    animSpeed_->setStep(0.05);
    animSpeed_->setDecimals(2);
    animSpeed_->setLabel("Velocidad");
    animSpeed_->setValue(1.0);
    animSpeed_->setOnChanged([this](double value) {
        auto* mesh = services_.context().selectedMesh();
        if (!mesh || syncing_)
            return;
        mesh->setAnimationSpeed(static_cast<float>(value));
        services_.context().markDirty();
    });

    scriptBox_ = fields_->add<zio::ui::VBox>();
    auto* scriptBody = addFoldout(scriptBox_, "Scripts", true);
    scriptList_      = scriptBody->add<zio::ui::VBox>();
    auto* addScript  = scriptBody->add<zio::ui::Button>(std::string("Añadir script"));
    addScript->setOnClick([this, addScript]() {
        auto* node = services_.context().selected();
        if (!node || syncing_)
            return;
        std::vector<MenuItem> items;
        for (const std::string& name : scriptCandidates(*node))
            items.push_back(MenuItem::item(name, [this, name]() {
                if (auto* target = services_.context().selected())
                {
                    target->addScript(name);
                    services_.context().markDirty();
                    services_.log("Script " + name + " añadido a " + (target->name().empty() ? "(sin nombre)" : target->name()));
                }
            }));
        if (items.empty())
        {
            MenuItem none = MenuItem::item("No hay scripts: crea un .cs o un .cpp desde el Sistema de archivos");
            none.enabled  = false;
            items.push_back(std::move(none));
        }
        const zio::ui::Rect r = addScript->rect();
        services_.showPopup(std::move(items), {r.x, r.bottom()});
    });
    return scroll;
}

std::vector<std::string> InspectorPanel::scriptCandidates(const zio::scene3d::Node3D& node) const
{
    std::set<std::string> names;
    const ProjectManager& project = services_.context().project();
    if (project.isOpen())
        for (const char* extension : {".cpp", ".cs"})
            for (const std::string& file : project.listFiles("assets/scripts", extension))
            {
                std::string stem = fs::path(file).stem().string();
                if (!stem.empty())
                    names.insert(stem);
            }
    for (const std::string& name : zio::scripting::Registry::instance().names())
        names.insert(name);
    std::vector<std::string> out;
    for (const std::string& name : names)
        if (!node.hasScript(name))
            out.push_back(name);
    return out;
}

void InspectorPanel::rebuildScripts(zio::scene3d::Node3D& node)
{
    scriptList_->clearChildren();
    for (const std::string& name : node.scriptNames())
    {
        auto* row   = scriptList_->add<zio::ui::HBox>();
        auto* label = row->add<zio::ui::Label>(name);
        label->setExpand(true, false);
        if (!zio::scripting::Registry::instance().has(name))
        {
            label->setText(name + "  (se ejecuta en el juego)");
            label->setColor(zio::ui::rgb(0x9ca3af));
        }
        auto* remove = row->add<zio::ui::Button>(std::string("Quitar"));
        remove->setOnClick([this, name]() {
            if (auto* target = services_.context().selected())
            {
                target->removeScript(name);
                services_.context().markDirty();
            }
        });
    }
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

    {
        std::string signature;
        for (const std::string& name : node->scriptNames())
            signature += name + "|";
        if (signature != scriptSignature_ || scriptNode_ != node)
        {
            scriptSignature_ = signature;
            scriptNode_      = node;
            rebuildScripts(*node);
        }
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

        const bool skeleton = mesh->hasSkeleton();
        animBox_->setVisible(skeleton);
        if (skeleton)
        {
            const auto names = mesh->animationNames();
            if (mesh->isAnimationPlaying())
            {
                for (std::size_t i = 0; i < names.size(); ++i)
                    if (names[i] == mesh->currentAnimation())
                        animIndex_ = static_cast<int>(i);
            }
            animIndex_ = names.empty() ? 0 : std::clamp(animIndex_, 0, static_cast<int>(names.size()) - 1);
            animName_->setText(names.empty() ? std::string("(sin animaciones)")
                                             : names[static_cast<std::size_t>(animIndex_)]);
            animPlay_->setText(mesh->isAnimationPlaying() ? "Detener" : "Reproducir");
            animPlay_->setEnabled(!names.empty());
            if (mesh->isAnimationPlaying())
                animLoop_->setChecked(mesh->animationLoops());
            if (!animSpeed_->editing())
                animSpeed_->setValue(mesh->animationSpeed());
        }
    }
    syncing_ = false;
}

std::unique_ptr<zio::ui::Widget> FilesPanel::build()
{
    auto column = std::make_unique<zio::ui::VBox>();
    column->setPadding(Edges::all(8.f));

    filter_ = column->add<zio::ui::LineEdit>();
    filter_->setPlaceholder("Filtrar archivos");
    filter_->setOnChanged([this](const std::string& text) {
        std::string lowered = text;
        for (char& c : lowered)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        filterText_ = lowered;
        applyFilter();
    });
    status_ = column->add<zio::ui::Label>(std::string());

    message_ = column->add<zio::ui::Label>(std::string("Abre o crea un proyecto desde el menú Proyecto."));
    message_->setColor(zio::ui::rgb(0x9ca3af));

    auto scroll = std::make_unique<zio::ui::ScrollContainer>(true, false);
    grid_       = scroll->add<zio::ui::IconGrid>();
    grid_->setListMode(true);
    grid_->setOnActivated([this](int index) { activate(index); });
    grid_->setOnSelectionChanged([this]() { updateButtons(); });
    grid_->setOnContextMenu([this](int index, Vec2 position) {
        if (index >= 0)
            grid_->select(index);
        services_.showPopup(menuItems(), position);
    });
    body_ = column->addChild(std::move(scroll));
    body_->setExpand(true, true);
    return column;
}

std::vector<MenuItem> FilesPanel::menuItems()
{
    const bool has = selectedEntry() != nullptr;
    std::vector<MenuItem> items;
    items.push_back(MenuItem::item("Crear...", [this]() { showCreate(); }));
    if (has)
    {
        items.push_back(MenuItem::item("Editar", [this]() {
            if (const AssetEntry* entry = selectedEntry())
                openEntry(*entry);
        }));
        items.push_back(MenuItem::item("Eliminar...", [this]() { showDelete(); }));
    }
    return items;
}

const AssetEntry* FilesPanel::selectedEntry() const
{
    const int index = grid_ ? grid_->current() : -1;
    if (index < 0 || index >= static_cast<int>(entries_.size()))
        return nullptr;
    const auto selection = grid_->selection();
    if (selection.empty())
        return nullptr;
    return &entries_[static_cast<std::size_t>(index)];
}

void FilesPanel::setStatus(const std::string& text, bool error)
{
    statusText_  = text;
    statusError_ = error;
    if (!text.empty())
        services_.log(text, error);
    status_->setText(text);
    status_->setColor(error ? services_.ui().theme().danger : zio::ui::rgb(0x9ca3af));
}

void FilesPanel::updateButtons()
{
}

void FilesPanel::openEntry(const AssetEntry& entry)
{
    auto& ctx = services_.context();
    if (entry.extension == "zscene3d")
    {
        ctx.request(Action{ActionKind::OpenScene, entry.relative, {}});
        return;
    }
    const auto absolute = ctx.project().resolve(entry.relative);
    if (!absolute)
    {
        setStatus("Ruta inválida.", true);
        return;
    }
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", absolute->wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        setStatus("No hay un programa para abrir " + entry.name + ".", true);
    else
        setStatus("Abierto " + entry.name, false);
}

void FilesPanel::activate(int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size()))
        return;
    const AssetEntry& entry = entries_[static_cast<std::size_t>(index)];
    if (ProjectManager::isMeshFile(entry.name))
        services_.context().addNode(NewNodeKind::Mesh, entry.relative);
    else
        openEntry(entry);
}

void FilesPanel::showCreate()
{
    using namespace zio::ui;
    if (!services_.context().project().isOpen())
        return;
    auto dialog  = std::make_unique<Dialog>("Crear archivo");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Nombre con extensión (jugador.cs, nivel.zscene3d, notas.txt)"));
    auto* name  = content->add<LineEdit>();
    auto* error = content->add<Label>(std::string());
    error->setColor(services_.ui().theme().danger);
    dialog->setContent(std::move(content));
    dialog->setWidth(480.f);

    Dialog* raw  = dialog.get();
    auto confirm = [this, raw, name, error]() {
        std::string problem, relative;
        if (!AssetFiles::create(services_.context().project(), name->text(), &relative, &problem))
        {
            error->setText(problem);
            return;
        }
        raw->close();
        setStatus("Creado " + relative.substr(7), false);
        requestScan();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Crear", confirm, true);
    dialog->setOnConfirm(confirm);
    name->setOnSubmit([confirm](const std::string&) { confirm(); });
    name->setOnCancel([raw]() { raw->cancel(); });
    services_.ui().openPopup(std::move(dialog));
    services_.ui().setFocus(name);
}

void FilesPanel::showDelete()
{
    using namespace zio::ui;
    const AssetEntry* selected = selectedEntry();
    if (!selected)
        return;
    const AssetEntry entry = *selected;
    auto& ctx              = services_.context();
    if (entry.relative == ctx.currentScene() || entry.name == ctx.currentScene())
    {
        setStatus("Esa escena está abierta: abre otra antes de eliminarla.", true);
        return;
    }
    auto dialog  = std::make_unique<Dialog>("Eliminar archivo");
    auto content = std::make_unique<VBox>();
    content->add<Label>("¿Eliminar " + entry.relative.substr(7) + "?");
    content->add<Label>(std::string("No se puede deshacer."))->setColor(services_.ui().theme().textDim);
    auto* error = content->add<Label>(std::string());
    error->setColor(services_.ui().theme().danger);
    dialog->setContent(std::move(content));
    dialog->setWidth(440.f);

    Dialog* raw  = dialog.get();
    auto confirm = [this, raw, entry, error]() {
        std::string problem;
        if (!AssetFiles::remove(services_.context().project(), entry.relative, &problem))
        {
            error->setText(problem);
            return;
        }
        raw->close();
        setStatus("Eliminado " + entry.relative.substr(7), false);
        requestScan();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Eliminar", confirm, true);
    dialog->setOnConfirm(confirm);
    services_.ui().openPopup(std::move(dialog));
}

void FilesPanel::applyEntries(std::vector<AssetEntry> entries)
{
    std::string signature = filterText_ + "#";
    for (const AssetEntry& entry : entries)
        signature += entry.relative + "|";
    if (signature == signature_)
        return;
    allEntries_ = std::move(entries);
    applyFilter();
}

void FilesPanel::applyFilter()
{
    std::string signature = filterText_ + "#";
    for (const AssetEntry& entry : allEntries_)
        signature += entry.relative + "|";
    if (signature == signature_)
        return;
    signature_ = signature;

    std::string keep;
    if (const AssetEntry* selected = selectedEntry())
        keep = selected->relative;

    entries_.clear();
    for (const AssetEntry& entry : allEntries_)
    {
        std::string name = entry.relative;
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (filterText_.empty() || name.find(filterText_) != std::string::npos)
            entries_.push_back(entry);
    }
    std::vector<zio::ui::IconItem> items;
    for (const AssetEntry& entry : entries_)
        items.push_back(iconFor(entry));
    grid_->setItems(std::move(items));

    if (!keep.empty())
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].relative == keep)
                grid_->select(static_cast<int>(i));
    updateButtons();
}

void FilesPanel::requestScan()
{
    if (scanning_)
    {
        rescan_ = true;
        return;
    }
    const fs::path root = services_.context().project().rootDir();
    if (root.empty())
        return;
    scanning_ = true;
    auto result = std::make_shared<std::vector<AssetEntry>>();
    auto alive  = alive_;
    zio::jobs::JobSystem::instance().enqueue(
        [root, result]() { *result = AssetFiles::scan(root); },
        [this, result, alive, root]() {
            if (!*alive)
                return;
            scanning_ = false;
            if (root == boundRoot_)
                applyEntries(std::move(*result));
            if (rescan_)
            {
                rescan_ = false;
                requestScan();
            }
        });
}

void FilesPanel::refresh(float dt)
{
    const ProjectManager& project = services_.context().project();
    body_->setVisible(project.isOpen());
    message_->setVisible(!project.isOpen());
    status_->setVisible(project.isOpen());
    if (!project.isOpen())
    {
        boundRoot_.clear();
        signature_.clear();
        allEntries_.clear();
        entries_.clear();
        grid_->setItems({});
        updateButtons();
        return;
    }

    if (boundRoot_ != project.rootDir())
    {
        boundRoot_ = project.rootDir();
        signature_.clear();
        allEntries_.clear();
        entries_.clear();
        grid_->setItems({});
        setStatus(std::string(), false);
        timer_ = 10.f;
    }

    timer_ += dt;
    if (timer_ > 2.f)
    {
        timer_ = 0.f;
        requestScan();
    }
}

std::unique_ptr<zio::ui::Widget> OutputPanel::build()
{
    auto column = std::make_unique<zio::ui::VBox>();
    column->setPadding(Edges::all(8.f));
    auto* top   = column->add<zio::ui::HBox>();
    auto* clear = top->add<zio::ui::Button>(std::string("Limpiar"));
    clear->setOnClick([this]() { lines_->clearChildren(); });
    scroll_ = column->add<zio::ui::ScrollContainer>(true, false);
    scroll_->setExpand(true, true);
    lines_ = scroll_->add<zio::ui::VBox>();
    lines_->setSeparation(1.f);
    return column;
}

void OutputPanel::add(const std::string& text, bool error)
{
    if (!lines_)
        return;
    constexpr std::size_t kMaxLines = 300;
    while (lines_->children().size() >= kMaxLines)
        lines_->removeChild(lines_->children().front().get());

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S  ", &local);

    auto* label = lines_->add<zio::ui::Label>(std::string(stamp) + text);
    label->setColor(error ? services_.ui().theme().danger : zio::ui::rgb(0xc9cdd6));
    scrollPending_ = true;
}

void OutputPanel::refresh()
{
    if (!scrollPending_ || !scroll_)
        return;
    scrollPending_ = false;
    scroll_->setScroll({0.f, 1.0e7f});
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
