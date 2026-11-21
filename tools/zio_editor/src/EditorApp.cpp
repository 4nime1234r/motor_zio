#include "EditorApp.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/RecentProject.h"
#include "zio/input/InputManager.h"
#include "zio/platform/Window.h"
#include "zio/render/Device.h"
#include "zio/renderer/BatchRenderer.h"

#ifndef ZIO_VERSION
#define ZIO_VERSION "0.0.0"
#endif

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

using zio::ui::MenuItem;

constexpr float kFrameClamp = 0.1f;

fs::path layoutSettingsPath()
{
    const char* appData = std::getenv("APPDATA");
    if (!appData || !*appData)
        return {};
    return fs::path(appData) / "ZioEditor" / "disposicion_ui.txt";
}

}

EditorApp::EditorApp()
    : viewport_(context_), hierarchy_(*this), inspector_(*this), files_(*this), performance_(*this)
{
}

EditorApp::~EditorApp() = default;

bool EditorApp::onInit()
{
    ui_ = std::make_unique<zio::ui::Context>();
    if (auto font = zio::ui::createSystemFont("Segoe UI", 17.f))
        ui_->theme().font = font;
    if (auto bold = zio::ui::createSystemFont("Segoe UI", 17.f, true))
        ui_->theme().fontBold = bold;
    host_ = std::make_unique<zio::ui::EngineHost>(*ui_);

    recentFile_ = recentProjectFile();
    layoutFile_ = layoutSettingsPath();

    buildUi();

    if (!layoutFile_.empty())
    {
        std::ifstream in(layoutFile_, std::ios::binary);
        if (in)
        {
            std::stringstream buffer;
            buffer << in.rdbuf();
            dock_->loadLayout(buffer.str());
        }
    }

    viewport_.setContextMenuHandler([this](zio::ui::Vec2 position) {
        std::vector<MenuItem> items;
        items.push_back(MenuItem::submenuItem("Añadir objeto", addObjectItems()));
        items.push_back(MenuItem::line());
        for (MenuItem& item : editItems())
            items.push_back(std::move(item));
        MenuItem remove = MenuItem::item("Eliminar", [this]() { context_.removeSelected(); }, "Supr");
        remove.enabled  = !context_.selectionRoots().empty();
        items.push_back(MenuItem::line());
        items.push_back(std::move(remove));
        showPopup(std::move(items), position);
    });

    keyListener_ = zio::events::EventBus::instance().onKey([this](const zio::events::KeyEvent& e) { handleKey(e); });

    if (Updater::enabled())
    {
        Updater::cleanupLeftovers();
        updater_.start();
    }

    openInitialProject();
    lastTick_ = std::chrono::steady_clock::now();
    return true;
}

void EditorApp::openInitialProject()
{
    std::string error;
    if (!startupProject_.empty())
    {
        if (!context_.openProject(startupProject_, &error))
            statusErrorText_ = error;
        return;
    }
    if (const auto recent = readRecentProject(recentFile_))
        if (!context_.openProject(*recent, &error))
            statusErrorText_ = "No se pudo reabrir el último proyecto: " + error;
}

void EditorApp::onShutdown()
{
    saveLayout();
    if (keyListener_ != zio::events::kInvalidListenerId)
        zio::events::EventBus::instance().unsubscribe(keyListener_);
    updater_.finish(restartAfterClose_);
    host_.reset();
    ui_.reset();
}

void EditorApp::saveLayout()
{
    if (layoutFile_.empty() || !dock_)
        return;
    std::error_code ec;
    fs::create_directories(layoutFile_.parent_path(), ec);
    std::ofstream out(layoutFile_, std::ios::binary | std::ios::trunc);
    if (out)
        out << dock_->saveLayout();
}

bool EditorApp::onCloseRequested()
{
    if (!context_.hasPending())
        context_.request(Action{ActionKind::CloseWindow, {}, {}});
    if (context_.closeApproved())
        return true;
    if (context_.hasPending() && !unsavedOpen_)
        showUnsaved();
    return false;
}

void EditorApp::rememberProject()
{
    if (!context_.project().isOpen() || context_.project().rootDir() == rememberedProject_)
        return;
    rememberedProject_ = context_.project().rootDir();
    writeRecentProject(recentFile_, rememberedProject_);
}

void EditorApp::togglePlay()
{
    if (context_.playing())
        context_.stopPlay();
    else if (context_.project().isOpen())
        context_.startPlay();
}

void EditorApp::saveCurrentScene()
{
    if (!context_.project().isOpen() || context_.playing())
        return;
    if (context_.canSave())
        context_.saveScene();
    else
        showSaveAs();
}

bool EditorApp::textFocused() const
{
    zio::ui::Widget* focused = ui_->focused();
    return dynamic_cast<zio::ui::LineEdit*>(focused) || dynamic_cast<zio::ui::TextEdit*>(focused) ||
           dynamic_cast<zio::ui::NumberEdit*>(focused);
}

void EditorApp::handleKey(const zio::events::KeyEvent& event)
{
    using zio::events::KeyCode;
    switch (event.code)
    {
    case KeyCode::LeftCtrl:
    case KeyCode::RightCtrl:
        ctrl_ = event.down;
        return;
    case KeyCode::LeftShift:
    case KeyCode::RightShift:
        shift_ = event.down;
        return;
    default:
        break;
    }
    if (!event.down)
        return;

    if (ctrl_ && event.code == KeyCode::S && !context_.hasPending() && !ui_->hasPopups())
    {
        saveCurrentScene();
        return;
    }
    if (event.code == KeyCode::F5 && !event.repeat && !context_.hasPending() && !ui_->hasPopups())
    {
        togglePlay();
        return;
    }
    if (ui_->hasPopups() || context_.hasPending() || textFocused())
        return;

    if (ctrl_)
    {
        switch (event.code)
        {
        case KeyCode::Z:
            shift_ ? context_.redo() : context_.undo();
            break;
        case KeyCode::Y:
            context_.redo();
            break;
        case KeyCode::C:
            context_.copySelection();
            break;
        case KeyCode::V:
            context_.paste();
            break;
        case KeyCode::D:
            context_.duplicateSelection();
            break;
        default:
            break;
        }
        return;
    }

    switch (event.code)
    {
    case KeyCode::Num1:
        viewport_.setMode(GizmoMode::Move);
        break;
    case KeyCode::Num2:
        viewport_.setMode(GizmoMode::Rotate);
        break;
    case KeyCode::Num3:
        viewport_.setMode(GizmoMode::Scale);
        break;
    case KeyCode::F:
        viewport_.focusSelection();
        break;
    case KeyCode::Escape:
        context_.clearSelection();
        break;
    case KeyCode::Delete:
        context_.removeSelected();
        break;
    default:
        break;
    }
}

std::vector<MenuItem> EditorApp::addObjectItems()
{
    const bool enabled = context_.project().isOpen() && !context_.playing();
    std::vector<MenuItem> items;

    MenuItem empty = MenuItem::item("Nodo vacío", [this]() { context_.addNode(NewNodeKind::Empty); });
    empty.enabled  = enabled;
    items.push_back(std::move(empty));

    MenuItem cube = MenuItem::item("Cubo", [this]() { context_.addNode(NewNodeKind::Cube); });
    cube.enabled  = enabled;
    items.push_back(std::move(cube));

    std::vector<MenuItem> meshes;
    for (const std::string& mesh : context_.meshAssets())
        meshes.push_back(MenuItem::item(mesh, [this, mesh]() { context_.addNode(NewNodeKind::Mesh, mesh); }));
    if (meshes.empty())
    {
        MenuItem none = MenuItem::item("No hay mallas en assets/models");
        none.enabled  = false;
        meshes.push_back(std::move(none));
    }
    MenuItem submenu = MenuItem::submenuItem("Malla (.obj, .gltf, .glb)", std::move(meshes));
    submenu.enabled  = enabled;
    items.push_back(std::move(submenu));
    return items;
}

std::vector<MenuItem> EditorApp::editItems()
{
    const bool hasSelection = !context_.selectionRoots().empty();
    std::vector<MenuItem> items;

    MenuItem copy = MenuItem::item("Copiar", [this]() { context_.copySelection(); }, "Ctrl+C");
    copy.enabled  = hasSelection;
    items.push_back(std::move(copy));

    MenuItem paste = MenuItem::item("Pegar", [this]() { context_.paste(); }, "Ctrl+V");
    paste.enabled  = context_.canPaste();
    items.push_back(std::move(paste));

    MenuItem duplicate = MenuItem::item("Duplicar", [this]() { context_.duplicateSelection(); }, "Ctrl+D");
    duplicate.enabled  = hasSelection;
    items.push_back(std::move(duplicate));

    items.push_back(MenuItem::line());

    MenuItem undo = MenuItem::item("Deshacer", [this]() { context_.undo(); }, "Ctrl+Z");
    undo.enabled  = context_.canUndo();
    items.push_back(std::move(undo));

    MenuItem redo = MenuItem::item("Rehacer", [this]() { context_.redo(); }, "Ctrl+Y");
    redo.enabled  = context_.canRedo();
    items.push_back(std::move(redo));
    return items;
}

void EditorApp::showPopup(std::vector<MenuItem> items, zio::ui::Vec2 position)
{
    ui_->closePopups();
    auto popup = std::make_unique<zio::ui::PopupMenu>(std::move(items));
    popup->setAnchor(position);
    ui_->openPopup(std::move(popup));
}

std::vector<MenuItem> EditorApp::projectItems()
{
    return {MenuItem::item("Nuevo proyecto...", [this]() { showNewProject(); }),
            MenuItem::item("Abrir proyecto...", [this]() { showOpenProject(); })};
}

std::vector<MenuItem> EditorApp::sceneItems()
{
    const bool enabled = context_.project().isOpen() && !context_.playing();
    MenuItem save      = MenuItem::item("Guardar", [this]() { saveCurrentScene(); }, "Ctrl+S");
    save.enabled       = enabled;
    MenuItem saveAs    = MenuItem::item("Guardar como...", [this]() { showSaveAs(); });
    saveAs.enabled     = enabled;
    return {std::move(save), std::move(saveAs)};
}

std::vector<MenuItem> EditorApp::sideItems(const std::string& id)
{
    std::vector<MenuItem> items;
    const std::pair<const char*, zio::ui::DockSide> sides[] = {{"Izquierda", zio::ui::DockSide::Left},
                                                                {"Derecha", zio::ui::DockSide::Right},
                                                                {"Abajo", zio::ui::DockSide::Bottom}};
    for (const auto& [name, side] : sides)
    {
        items.push_back(MenuItem::check(name, dock_->sideOf(id) == side, [this, id, side = side]() {
            dock_->movePanel(id, side);
            saveLayout();
        }));
    }
    return items;
}

std::vector<MenuItem> EditorApp::viewItems()
{
    std::vector<MenuItem> panels;
    panels.push_back(MenuItem::submenuItem("Jerarquía", sideItems("jerarquia")));
    panels.push_back(MenuItem::submenuItem("Archivos", sideItems("archivos")));
    panels.push_back(MenuItem::submenuItem("Inspector", sideItems("inspector")));
    panels.push_back(MenuItem::submenuItem("Rendimiento", sideItems("rendimiento")));
    return {MenuItem::submenuItem("Posición de los paneles", std::move(panels)), MenuItem::line(),
            MenuItem::item("Restablecer disposición", [this]() {
                dock_->movePanel("jerarquia", zio::ui::DockSide::Left);
                dock_->movePanel("archivos", zio::ui::DockSide::Left);
                dock_->movePanel("inspector", zio::ui::DockSide::Right);
                dock_->movePanel("rendimiento", zio::ui::DockSide::Bottom);
                dock_->setFractions(0.24f, 0.26f, 0.24f);
                saveLayout();
            })};
}

void EditorApp::buildTopBar(zio::ui::Widget* page)
{
    const zio::ui::Theme& theme = ui_->theme();
    auto* bar                   = page->add<zio::ui::Panel>();
    bar->setBackground(theme.deep);
    bar->setPadding(zio::ui::Edges::symmetric(6.f, 0.f));
    auto* row  = bar->add<zio::ui::HBox>();
    auto* menu = row->add<zio::ui::MenuBar>();
    menu->addMenu("Proyecto", [this]() { return projectItems(); });
    menu->addMenu("Escena", [this]() { return sceneItems(); });
    menu->addMenu("Editar", [this]() { return editItems(); });
    menu->addMenu("Objeto", [this]() { return addObjectItems(); });
    menu->addMenu("Ver", [this]() { return viewItems(); });

    row->add<zio::ui::Widget>()->setExpand(true, false);
    play_ = row->add<ToolButton>(ToolIcon::Play, 28.f);
    play_->setTooltip("Jugar (F5)");
    play_->setOnClick([this]() {
        if (!context_.playing())
            togglePlay();
    });
    stop_ = row->add<ToolButton>(ToolIcon::Stop, 28.f);
    stop_->setTooltip("Detener (F5)");
    stop_->setOnClick([this]() {
        if (context_.playing())
            togglePlay();
    });
    row->add<zio::ui::Widget>()->setExpand(true, false);
}

void EditorApp::buildStatusBar(zio::ui::Widget* page)
{
    const zio::ui::Theme& theme = ui_->theme();
    auto* bar                   = page->add<zio::ui::Panel>();
    bar->setBackground(theme.deep);
    bar->setPadding(zio::ui::Edges::symmetric(12.f, 2.f));
    auto* row = bar->add<zio::ui::HBox>();
    row->setSeparation(12.f);

    statusScene_ = row->add<zio::ui::Label>(std::string());
    statusScene_->setColor(theme.textDim);
    statusError_ = row->add<zio::ui::Label>(std::string());
    statusError_->setColor(theme.danger);

    updateText_ = row->add<zio::ui::Label>(std::string());
    updateText_->setColor(theme.accentHover);
    updateProgress_ = row->add<zio::ui::ProgressBar>();
    updateProgress_->setMinSize({160.f, 8.f});
    updateButton_ = row->add<zio::ui::Button>(std::string());
    updateButton_->setMinSize({0.f, 24.f});

    row->add<zio::ui::Widget>()->setExpand(true, false);
    statusRight_ = row->add<zio::ui::Label>(std::string());
    statusRight_->setColor(theme.textDim);
}

void EditorApp::buildUi()
{
    auto* page = ui_->root().add<zio::ui::VBox>();
    page->setSeparation(0.f);

    buildTopBar(page);

    dock_ = page->add<zio::ui::DockSpace>();
    dock_->setExpand(true, true);
    dock_->setCenter(viewport_.build());
    dock_->addPanel("jerarquia", "Jerarquía", hierarchy_.build(), zio::ui::DockSide::Left);
    dock_->addPanel("archivos", "Archivos", files_.build(), zio::ui::DockSide::Left);
    dock_->addPanel("inspector", "Inspector", inspector_.build(), zio::ui::DockSide::Right);
    dock_->addPanel("rendimiento", "Rendimiento", performance_.build(), zio::ui::DockSide::Bottom);
    dock_->setFractions(0.24f, 0.26f, 0.24f);

    buildStatusBar(page);
}

void EditorApp::refreshUpdate()
{
    const UpdateStatus update = updater_.status();
    const bool show           = Updater::enabled() && update.state != UpdateState::Idle && update.state != UpdateState::Checking;

    updateText_->setVisible(show);
    updateButton_->setVisible(show && update.state != UpdateState::Downloading);
    updateProgress_->setVisible(show && update.state == UpdateState::Downloading);
    if (!show)
        return;

    switch (update.state)
    {
    case UpdateState::Available:
        updateText_->setText("Actualización disponible: v" + update.version);
        updateButton_->setText("Descargar");
        updateButton_->setOnClick([this]() { updater_.download(); });
        break;
    case UpdateState::Downloading:
        updateText_->setText("Descargando v" + update.version);
        updateProgress_->setValue(update.progress);
        break;
    case UpdateState::Downloaded:
        updateText_->setText("Zio v" + update.version + " lista para instalar");
        updateButton_->setText("Reiniciar y actualizar");
        updateButton_->setOnClick([this]() {
            if (context_.hasPending())
                return;
            restartAfterClose_ = true;
            context_.request(Action{ActionKind::CloseWindow, {}, {}});
        });
        break;
    case UpdateState::Error:
        updateText_->setText(update.error);
        updateButton_->setText("Reintentar");
        updateButton_->setOnClick([this]() { updater_.retry(); });
        break;
    default:
        break;
    }
}

void EditorApp::refreshStatus()
{
    std::string text;
    if (context_.project().isOpen())
    {
        const std::string& scene = context_.currentScene();
        text                     = context_.project().name() + "  /  " + (scene.empty() ? "(sin escena)" : scene) +
               (context_.isDirty() ? " *" : "");
        if (context_.selection().size() > 1)
            text += "   " + std::to_string(context_.selection().size()) + " seleccionados";
    }
    else
    {
        text = "Sin proyecto: crea o abre uno desde el menú Proyecto";
    }
    statusScene_->setText(text);

    const std::string& error = context_.lastError().empty() ? statusErrorText_ : context_.lastError();
    statusError_->setText(error);

    char right[96];
    const float fps = frameMs_ > 0.f ? 1000.f / frameMs_ : 0.f;
    std::snprintf(right, sizeof(right), "%.0f FPS    Zio v%s", fps, ZIO_VERSION);
    statusRight_->setText(right);

    play_->setActive(context_.playing());
    stop_->setActive(context_.playing());
    play_->setEnabled(context_.project().isOpen());
    stop_->setEnabled(context_.project().isOpen());
    refreshUpdate();
}

void EditorApp::onBeforeTick(float dt)
{
    (void)dt;
    const auto now = std::chrono::steady_clock::now();
    realDt_        = std::min(std::chrono::duration<float>(now - lastTick_).count(), kFrameClamp);
    lastTick_      = now;
    frameMs_       = frameMs_ * 0.9f + realDt_ * 1000.f * 0.1f;

    if (context_.closeApproved())
    {
        requestExit();
        return;
    }
    if (context_.hasPending() && !unsavedOpen_)
        showUnsaved();
    rememberProject();

    hierarchy_.refresh();
    inspector_.refresh();
    files_.refresh(realDt_);
    performance_.refresh(realDt_);
    refreshStatus();

    if (!context_.playing())
        previewAnimations(context_.scene().root(), realDt_);

    viewport_.update(realDt_, ui_->hasPopups() || textFocused());

    auto& input = zio::input::InputManager::instance();
    if (!input.isMouseButtonDown(zio::events::MouseButton::Left) && !viewport_.dragging())
        context_.commitHistory();

    host_->frame(realDt_);
}

void EditorApp::previewAnimations(zio::scene3d::Node3D& node, float dt)
{
    if (auto* mesh = dynamic_cast<zio::scene3d::MeshNode*>(&node))
        if (mesh->isAnimationPlaying())
            mesh->update(dt);
    for (const auto& child : node.children())
        previewAnimations(*child, dt);
}

void EditorApp::onRender(float dt)
{
    (void)dt;
    auto& device = zio::render::Device::instance();
    device.beginFrame(zio::render::ClearColor{0.059f, 0.067f, 0.090f, 1.f});

    viewport_.render(realDt_);

    auto& window = zio::platform::Window::instance();
    zio::renderer::BatchRenderer::instance().begin(window.width(), window.height());
    host_->draw();
    zio::renderer::BatchRenderer::instance().end();

    device.endFrame();
}

std::string EditorApp::describePending() const
{
    if (!context_.hasPending())
        return {};
    const Action& action = context_.pending();
    switch (action.kind)
    {
    case ActionKind::OpenScene:
        return "Se abrirá la escena " + action.text + ".";
    case ActionKind::OpenProject:
        return "Se abrirá el proyecto " + action.path.string() + ".";
    case ActionKind::CreateProject:
        return "Se creará el proyecto " + action.text + ".";
    case ActionKind::CloseWindow:
        return "Se cerrará el editor.";
    }
    return {};
}

void EditorApp::showNewProject()
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>("Nuevo proyecto");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Carpeta padre"));
    auto* parent = content->add<LineEdit>();
    std::error_code ec;
    parent->setText(fs::current_path(ec).string());
    content->add<Label>(std::string("Nombre"));
    auto* name  = content->add<LineEdit>();
    auto* error = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);
    dialog->setContent(std::move(content));

    Dialog* raw = dialog.get();
    auto confirm = [this, raw, parent, name, error]() {
        const std::string parentText = parent->text();
        const std::string nameText   = name->text();
        std::string problem;
        if (!ProjectManager::checkNewProject(parentText, nameText, &problem))
        {
            error->setText(problem);
            return;
        }
        raw->close();
        context_.request(Action{ActionKind::CreateProject, nameText, parentText});
        statusErrorText_.clear();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Crear", confirm, true);
    dialog->setOnConfirm(confirm);
    for (LineEdit* edit : {parent, name})
    {
        edit->setOnSubmit([confirm](const std::string&) { confirm(); });
        edit->setOnCancel([raw]() { raw->cancel(); });
    }
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(name);
}

void EditorApp::showOpenProject()
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>("Abrir proyecto");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("project.zio o su carpeta"));
    auto* path = content->add<LineEdit>();
    if (const auto recent = readRecentProject(recentFile_))
        path->setText(recent->string());
    auto* error = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);
    dialog->setContent(std::move(content));

    Dialog* raw = dialog.get();
    auto confirm = [this, raw, path, error]() {
        const std::string text = path->text();
        std::string problem;
        ProjectManager probe;
        if (!probe.loadProject(text, &problem))
        {
            error->setText(problem);
            return;
        }
        raw->close();
        context_.request(Action{ActionKind::OpenProject, {}, text});
        statusErrorText_.clear();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Abrir", confirm, true);
    dialog->setOnConfirm(confirm);
    path->setOnSubmit([confirm](const std::string&) { confirm(); });
    path->setOnCancel([raw]() { raw->cancel(); });
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(path);
    path->selectAll();
}

void EditorApp::showSaveAs()
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>("Guardar escena como");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Nombre (en assets/scenes)"));
    auto* name  = content->add<LineEdit>();
    auto* error = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);
    dialog->setContent(std::move(content));

    Dialog* raw = dialog.get();
    auto confirm = [this, raw, name, error]() {
        std::string problem;
        if (context_.saveSceneAs(name->text(), &problem))
        {
            raw->close();
            return;
        }
        error->setText(problem);
        context_.clearError();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Guardar", confirm, true);
    dialog->setOnConfirm(confirm);
    name->setOnSubmit([confirm](const std::string&) { confirm(); });
    name->setOnCancel([raw]() { raw->cancel(); });
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(name);
}

void EditorApp::showUnsaved()
{
    using namespace zio::ui;
    unsavedOpen_ = true;
    auto dialog  = std::make_unique<Dialog>("Cambios sin guardar");
    auto content = std::make_unique<VBox>();
    const std::string& scene = context_.currentScene();
    content->add<Label>("La escena " + (scene.empty() ? std::string("(sin archivo)") : scene) + " tiene cambios sin guardar.");
    content->add<Label>(describePending())->setColor(ui_->theme().textDim);
    auto* error = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);
    const bool canSave = context_.canSave();
    if (!canSave)
        content->add<Label>(std::string("La escena no tiene archivo: usa Escena > Guardar como antes de continuar."))
            ->setColor(ui_->theme().textDim);
    dialog->setContent(std::move(content));
    dialog->setWidth(520.f);

    Dialog* raw = dialog.get();
    dialog->setOnClosed([this]() { unsavedOpen_ = false; });
    dialog->setOnCancel([this]() { context_.resolvePending(Decision::Cancel); });
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Descartar", [this, raw]() {
        raw->close();
        context_.resolvePending(Decision::Discard);
    });
    Button* save = dialog->addButton("Guardar", [this, raw, error]() {
        if (!context_.resolvePending(Decision::Save))
        {
            error->setText(context_.lastError());
            context_.clearError();
            return;
        }
        raw->close();
    }, true);
    save->setEnabled(canSave);
    ui_->openPopup(std::move(dialog));
}

}
