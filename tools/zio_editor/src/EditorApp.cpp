#include "EditorApp.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "core/GameExport.h"
#include "core/RecentProject.h"
#include "ui/EditorIcons.h"
#include "zio/jobs/JobSystem.h"
#include "zio/scripting/DotNet.h"
#include "zio/pack/Package.h"
#include "zio/scripting/Language.h"
#include "update/ReleaseNotes.h"
#include "zio/input/InputManager.h"
#include "zio/physics3d/World3D.h"
#include "zio/scene3d/SceneIO3D.h"
#include "zio/platform/FileDialog.h"
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

enum class Browse
{
    OpenFile,
    Folder
};

zio::ui::LineEdit* addPathField(zio::ui::VBox* content, Browse kind, std::string title,
                                std::vector<zio::platform::FileFilter> filters = {})
{
    using namespace zio::ui;
    auto* row  = content->add<HBox>();
    auto* edit = row->add<LineEdit>();
    edit->setExpand(true, false);
    auto* button = row->add<Button>(std::string("Examinar..."));
    button->setOnClick([edit, kind, title, filters]() {
        zio::platform::FileDialogOptions options;
        options.title            = title;
        options.initialDirectory = edit->text();
        options.filters          = filters;
        const auto chosen = kind == Browse::Folder ? zio::platform::pickFolderDialog(options)
                                                   : zio::platform::openFileDialog(options);
        if (chosen)
            edit->setText(*chosen);
    });
    return edit;
}

using zio::ui::MenuItem;

constexpr float kFrameClamp = 0.1f;

fs::path layoutSettingsPath()
{
    const char* appData = std::getenv("APPDATA");
    if (!appData || !*appData)
        return {};
    return fs::path(appData) / "ZioEditor" / "disposicion_ui_v3.txt";
}

}

EditorApp::EditorApp()
    : viewport_(context_), hierarchy_(*this), inspector_(*this), files_(*this), output_(*this), performance_(*this)
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
    EditorIcons::init();

    recentFile_ = recentProjectFile();
    zio::scripting::Languages::registerBuiltin();
    codeEditorFile_ = recentFile_.parent_path() / "editor_codigo.txt";
    {
        std::ifstream in(codeEditorFile_, std::ios::binary);
        std::getline(in, codeEditor_);
        while (!codeEditor_.empty() && (codeEditor_.back() == '\r' || codeEditor_.back() == ' '))
            codeEditor_.pop_back();
    }
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

    log(std::string("Editor de Zio v") + ZIO_VERSION);
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
    EditorIcons::shutdown();
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
    failedAt_.clear();
    installOffered_.clear();
    loadProjectScripts();
}

void EditorApp::loadProjectScripts()
{
    const fs::path root = context_.project().rootDir();
    for (const auto& language : zio::scripting::Languages::instance().all())
    {
        language->unload();
        if (!language->hasSources(root))
            continue;
        language->prepareProject(root);
        std::error_code ec;
        if (language->needsBuild(root) || !fs::exists(language->outputPath(root), ec))
            continue;
        std::string error;
        if (language->load(root, &error))
            log("Scripts de " + language->displayName() + " cargados: " + std::to_string(language->loadedNames().size()));
        else
            log(error, true);
    }
}

void EditorApp::flushBuildLines()
{
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(buildMutex_);
        lines.swap(buildLines_);
    }
    for (std::string& line : lines)
    {
        const std::size_t bracket = line.rfind(" [");
        if (bracket != std::string::npos && line.find(".csproj]") != std::string::npos)
            line.erase(bracket);
        const std::string root = context_.project().rootDir().string() + "\\";
        for (std::size_t at = line.find(root); at != std::string::npos; at = line.find(root))
            line.erase(at, root.size());
        if (line.find("workload") != std::string::npos || line.find("Tiempo transcurrido") != std::string::npos ||
            line.find("Advertencia(s)") != std::string::npos || !seenBuildLines_.insert(line).second)
            continue;
        log(line, line.find(" error ") != std::string::npos || line.find("ERROR") != std::string::npos);
    }
}

void EditorApp::offerInstall(zio::scripting::ScriptLanguage* language, const std::string& problem)
{
    using namespace zio::ui;
    if (ui_->hasPopups())
        return;
    auto dialog  = std::make_unique<Dialog>("Falta una herramienta");
    auto content = std::make_unique<VBox>();
    content->add<Label>(problem);
    content->add<Label>("Para compilar scripts de " + language->displayName() + " hace falta instalarla.");
    content->add<Label>(std::string("Puedes instalarla ahora con un clic. Al terminar, el editor la usa solo."));
    dialog->setContent(std::move(content));
    dialog->setWidth(520.f);
    Dialog* raw = dialog.get();
    dialog->addButton("Ahora no", [raw]() { raw->cancel(); });
    dialog->addButton(
        "Instalar",
        [this, raw, language]() {
            std::string error;
            if (language->installTools(&error))
                log("Se abrió el instalador de " + language->displayName() + ". Cuando termine, compila de nuevo.");
            else
                log(error, true);
            installOffered_.erase(language->id());
            raw->close();
        },
        true);
    ui_->openPopup(std::move(dialog));
}

void EditorApp::buildScripts(bool thenPlay, bool automatic, bool force)
{
    if (!context_.project().isOpen() || context_.playing())
        return;
    if (building_)
    {
        if (thenPlay)
            playAfterBuild_ = true;
        return;
    }
    const fs::path root = context_.project().rootDir();
    std::vector<BuildJob> jobs;
    for (const auto& language : zio::scripting::Languages::instance().all())
    {
        const std::string id = language->id();
        if (!language->hasSources(root) || (!force && !language->needsBuild(root)))
            continue;
        if (automatic)
        {
            if (installOffered_.count(id))
                continue;
            const auto failed = failedAt_.find(id);
            if (failed != failedAt_.end() && failed->second == language->newestSource(root))
                continue;
        }
        std::string problem;
        if (!language->toolsAvailable(&problem))
        {
            if (!automatic || installOffered_.insert(id).second)
            {
                log(problem, true);
                offerInstall(language.get(), problem);
            }
            continue;
        }
        installOffered_.erase(id);
        jobs.push_back({language.get(), false, {}});
    }
    if (jobs.empty())
    {
        if (thenPlay)
            context_.startPlay();
        return;
    }
    building_       = true;
    playAfterBuild_ = thenPlay;
    seenBuildLines_.clear();
    jobs_ = std::move(jobs);
    std::string names;
    for (const BuildJob& job : jobs_)
        names += (names.empty() ? "" : ", ") + job.language->displayName();
    log("Compilando scripts de " + names + "...");
    zio::jobs::JobSystem::instance().enqueue(
        [this, root]() {
            for (BuildJob& job : jobs_)
                job.ok = job.language->build(
                    root,
                    [this](const std::string& line) {
                        if (line.rfind("-- ", 0) == 0 || line.find("MSBuild") != std::string::npos ||
                            line.find("MSB8029") != std::string::npos || line.find("Creando biblioteca") != std::string::npos ||
                            line.find("Creating library") != std::string::npos)
                            return;
                        std::lock_guard<std::mutex> lock(buildMutex_);
                        buildLines_.push_back(line);
                    },
                    &job.error);
        },
        [this, root]() {
            flushBuildLines();
            building_ = false;
            context_.scene().root().releaseScripts();
            bool allOk = true;
            for (BuildJob& job : jobs_)
            {
                const std::string id = job.language->id();
                if (!job.ok)
                {
                    allOk         = false;
                    failedAt_[id] = job.language->newestSource(root);
                    log(job.error, true);
                    continue;
                }
                failedAt_.erase(id);
                std::string error;
                if (job.language->load(root, &error))
                    log("Scripts de " + job.language->displayName() +
                        " compilados y cargados: " + std::to_string(job.language->loadedNames().size()));
                else
                {
                    allOk = false;
                    log(error, true);
                }
            }
            jobs_.clear();
            const bool play = playAfterBuild_;
            playAfterBuild_ = false;
            if (play)
            {
                if (allOk && !context_.playing())
                    context_.startPlay();
                else if (!allOk)
                    log("Corrige los errores de los scripts para poder jugar.", true);
            }
        });
}

void EditorApp::pollScripts(float dt)
{
    pollTimer_ += dt;
    if (pollTimer_ < 1.f)
        return;
    pollTimer_ = 0.f;
    if (!context_.project().isOpen() || building_ || context_.playing() || ui_->hasPopups())
        return;
    buildScripts(false, true, false);
}

void EditorApp::scriptFileCreated(const std::string& relative)
{
    const std::string extension = fs::path(relative).extension().string();
    if (extension.size() < 2)
        return;
    if (auto* language = zio::scripting::Languages::instance().forExtension(extension.substr(1)))
        language->prepareProject(context_.project().rootDir());
}

namespace
{

std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

bool onPath(const wchar_t* name)
{
    wchar_t buffer[MAX_PATH];
    return SearchPathW(nullptr, name, nullptr, MAX_PATH, buffer, nullptr) != 0;
}

bool launch(const std::wstring& file, const std::wstring& arguments, bool hidden)
{
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", file.c_str(), arguments.empty() ? nullptr : arguments.c_str(),
                                           nullptr, hidden ? SW_HIDE : SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

}

void EditorApp::openInCodeEditor(const fs::path& path)
{
    const fs::path root = context_.project().isOpen() ? context_.project().rootDir() : fs::path();
    std::error_code ec;
    const bool isFolder = fs::is_directory(path, ec);
    std::wstring arguments;
    if (!isFolder && !root.empty())
        arguments = L"\"" + root.wstring() + L"\" ";
    arguments += L"\"" + path.wstring() + L"\"";

    bool opened = false;
    if (!codeEditor_.empty())
        opened = launch(widen(codeEditor_), L"\"" + path.wstring() + L"\"", false);
    else if (onPath(L"code.cmd"))
        opened = launch(L"cmd.exe", L"/c code " + arguments, true);
    if (!opened)
        opened = launch(path.wstring(), {}, false);
    if (!opened)
        log("No se pudo abrir un editor de código para " + path.filename().string() + ".", true);
}

void EditorApp::showCodeEditorDialog()
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>("Editor de código");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Programa para abrir los scripts (por ejemplo code, rider64, notepad++)."));
    content->add<Label>(std::string("Déjalo vacío para usar VS Code si está instalado, o el programa predeterminado."));
    auto* command = addPathField(content.get(), Browse::OpenFile, "Programa para abrir los scripts",
                                 {{"Programas", "*.exe;*.cmd;*.bat"}, {"Todos los archivos", "*.*"}});
    command->setText(codeEditor_);
    dialog->setContent(std::move(content));
    dialog->setWidth(520.f);
    Dialog* raw  = dialog.get();
    auto confirm = [this, raw, command]() {
        codeEditor_ = command->text();
        std::error_code ec;
        fs::create_directories(codeEditorFile_.parent_path(), ec);
        std::ofstream out(codeEditorFile_, std::ios::binary | std::ios::trunc);
        out << codeEditor_;
        log(codeEditor_.empty() ? "Editor de código: automático" : "Editor de código: " + codeEditor_);
        raw->close();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Guardar", confirm, true);
    dialog->setOnConfirm(confirm);
    command->setOnSubmit([confirm](const std::string&) { confirm(); });
    command->setOnCancel([raw]() { raw->cancel(); });
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(command);
}

void EditorApp::togglePlay()
{
    if (context_.playing())
        context_.stopPlay();
    else if (context_.project().isOpen())
        buildScripts(true, false, false);
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

    MenuItem pointLight = MenuItem::item("Luz puntual", [this]() { context_.addNode(NewNodeKind::PointLight); });
    pointLight.enabled  = enabled;
    items.push_back(std::move(pointLight));

    MenuItem spotLight = MenuItem::item("Luz focal", [this]() { context_.addNode(NewNodeKind::SpotLight); });
    spotLight.enabled  = enabled;
    items.push_back(std::move(spotLight));

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

    std::vector<MenuItem> prefabs;
    for (const std::string& prefab : context_.prefabAssets())
        prefabs.push_back(MenuItem::item(prefab, [this, prefab]() { context_.addNode(NewNodeKind::Prefab, prefab); }));
    if (prefabs.empty())
    {
        MenuItem none = MenuItem::item("No hay escenas en assets/scenes");
        none.enabled  = false;
        prefabs.push_back(std::move(none));
    }
    MenuItem prefabMenu = MenuItem::submenuItem("Prefab (escena reutilizable)", std::move(prefabs));
    prefabMenu.enabled  = enabled;
    items.push_back(std::move(prefabMenu));
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
    const bool open  = context_.project().isOpen();
    MenuItem compile = MenuItem::item("Compilar scripts", [this]() { buildScripts(false, false, true); });
    compile.enabled  = open && !building_;
    MenuItem openCode =
        MenuItem::item("Abrir proyecto en el editor de código", [this]() { openInCodeEditor(context_.project().rootDir()); });
    openCode.enabled = open;
    MenuItem gameInfoItem = MenuItem::item("Información del juego...", [this]() { showGameInfo(); });
    gameInfoItem.enabled  = open;
    MenuItem exportItem   = MenuItem::item("Exportar juego...", [this]() { showExport(); });
    exportItem.enabled    = open && !building_ && !context_.playing();
    return {MenuItem::item("Nuevo proyecto...", [this]() { showNewProject(); }),
            MenuItem::item("Abrir proyecto...", [this]() { showOpenProject(); }),
            MenuItem::line(),
            std::move(compile),
            std::move(openCode),
            MenuItem::item("Elegir editor de código...", [this]() { showCodeEditorDialog(); }),
            MenuItem::line(),
            std::move(gameInfoItem),
            std::move(exportItem),
            MenuItem::item("Importar juego...", [this]() { showImport(); }),
            MenuItem::item("Jugar un juego exportado...", [this]() { showPlayPackage(); })};
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
    panels.push_back(MenuItem::submenuItem("Escena", sideItems("jerarquia")));
    panels.push_back(MenuItem::submenuItem("Sistema de archivos", sideItems("archivos")));
    panels.push_back(MenuItem::submenuItem("Inspector", sideItems("inspector")));
    panels.push_back(MenuItem::submenuItem("Salida", sideItems("salida")));
    panels.push_back(MenuItem::submenuItem("Rendimiento", sideItems("rendimiento")));
    return {MenuItem::submenuItem("Posición de los paneles", std::move(panels)), MenuItem::line(),
            MenuItem::item("Restablecer disposición", [this]() {
                dock_->movePanel("jerarquia", zio::ui::DockSide::Left);
                dock_->movePanel("archivos", zio::ui::DockSide::Left);
                dock_->movePanel("inspector", zio::ui::DockSide::Right);
                dock_->movePanel("salida", zio::ui::DockSide::Bottom);
                dock_->movePanel("rendimiento", zio::ui::DockSide::Bottom);
                dock_->setFractions(0.2f, 0.24f, 0.26f);
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
    menu->addMenu("Ayuda", [this]() { return helpItems(); });

    row->add<zio::ui::Widget>()->setExpand(true, false);
    auto* mode = row->add<zio::ui::Button>(std::string("3D"));
    mode->setFlat(true);
    mode->setPrimary(true);
    mode->setToggle(true);
    mode->setToggled(true);
    mode->setOnToggled([mode](bool) { mode->setToggled(true); });
    row->add<zio::ui::Widget>()->setExpand(true, false);
    updateBadge_ = row->add<zio::ui::Button>(std::string());
    updateBadge_->setPrimary(true);
    updateBadge_->setVisible(false);
    updateBadge_->setOnClick([this]() { showUpdate(); });
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
    auto center = std::make_unique<zio::ui::VBox>();
    center->setSeparation(0.f);
    auto* tabBar = center->add<zio::ui::Panel>();
    tabBar->setBackground(ui_->theme().deep);
    tabBar->setPadding(zio::ui::Edges::symmetric(12.f, 5.f));
    sceneTab_ = tabBar->add<zio::ui::Label>(std::string("[sin guardar]"));
    sceneTab_->setBold(true);
    auto* viewportWidget = center->addChild(viewport_.build());
    viewportWidget->setExpand(true, true);
    dock_->setCenter(std::move(center));
    dock_->addPanel("jerarquia", "Escena", hierarchy_.build(), zio::ui::DockSide::Left);
    dock_->addPanel("archivos", "Sistema de archivos", files_.build(), zio::ui::DockSide::Left);
    dock_->addPanel("inspector", "Inspector", inspector_.build(), zio::ui::DockSide::Right);
    dock_->addPanel("salida", "Salida", output_.build(), zio::ui::DockSide::Bottom);
    dock_->addPanel("rendimiento", "Rendimiento", performance_.build(), zio::ui::DockSide::Bottom);
    dock_->setFractions(0.2f, 0.24f, 0.26f);

    buildStatusBar(page);
}

void EditorApp::refreshUpdate()
{
    const UpdateStatus update = updater_.status();
    const UpdateState state   = update.state;

    const bool badge = state == UpdateState::Available || state == UpdateState::Downloading ||
                       state == UpdateState::Downloaded;
    updateBadge_->setVisible(badge);
    if (badge)
    {
        if (state == UpdateState::Available)
            updateBadge_->setText("Actualización disponible: v" + update.version);
        else if (state == UpdateState::Downloading)
            updateBadge_->setText("Descargando v" + update.version + "  " +
                                  std::to_string(static_cast<int>(update.progress * 100.f)) + "%");
        else
            updateBadge_->setText("Reiniciar para actualizar a v" + update.version);
    }

    if (state != lastUpdateState_)
    {
        lastUpdateState_ = state;
        if (state == UpdateState::Available)
            log("Actualización disponible: v" + update.version + ". Pulsa el aviso de arriba para ver las novedades.");
        else if (state == UpdateState::Downloaded)
            log("Actualización v" + update.version + " lista: reinicia el editor para instalarla.");
        else if (state == UpdateState::Error)
            log(update.error, true);
    }

    if (manualCheck_ && state != UpdateState::Checking)
    {
        manualCheck_ = false;
        if (state == UpdateState::Available)
            showUpdate();
        else if (state == UpdateState::Error)
            showInfo("Buscar actualizaciones", update.error);
        else
            showInfo("Buscar actualizaciones", std::string("Zio está al día. Tienes la versión v") + ZIO_VERSION + ", la más reciente.");
    }
}

void EditorApp::checkUpdates()
{
    if (!Updater::enabled())
    {
        showInfo("Buscar actualizaciones",
                 "Esta compilación no tiene activadas las actualizaciones. Usa la versión publicada en GitHub para recibirlas.");
        return;
    }
    const UpdateState state = updater_.status().state;
    if (state == UpdateState::Available || state == UpdateState::Downloading || state == UpdateState::Downloaded)
    {
        showUpdate();
        return;
    }
    if (updater_.checkNow())
    {
        manualCheck_ = true;
        log("Buscando actualizaciones...");
    }
    else
    {
        showInfo("Buscar actualizaciones", "Todavía se está iniciando la comprobación; inténtalo en unos segundos.");
    }
}

std::vector<MenuItem> EditorApp::helpItems()
{
    const UpdateState state = updater_.status().state;
    MenuItem check          = MenuItem::item("Buscar actualizaciones...", [this]() { checkUpdates(); });
    MenuItem news           = MenuItem::item("Novedades de la actualización...", [this]() { showUpdate(); });
    news.enabled            = state == UpdateState::Available || state == UpdateState::Downloading ||
                   state == UpdateState::Downloaded;
    MenuItem version = MenuItem::item(std::string("Zio v") + ZIO_VERSION);
    version.enabled  = false;
    return {std::move(check), std::move(news), MenuItem::line(), std::move(version)};
}

void EditorApp::showInfo(const std::string& title, const std::string& message)
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>(title);
    auto content = std::make_unique<VBox>();
    std::string text = message;
    for (const NoteLine& line : formatReleaseNotes(text, 70))
        content->add<Label>(line.text);
    dialog->setContent(std::move(content));
    dialog->setWidth(520.f);
    Dialog* raw = dialog.get();
    dialog->addButton("Cerrar", [raw]() { raw->cancel(); }, true);
    ui_->openPopup(std::move(dialog));
}

void EditorApp::showUpdate()
{
    using namespace zio::ui;
    const UpdateStatus update = updater_.status();
    if (update.state != UpdateState::Available && update.state != UpdateState::Downloading &&
        update.state != UpdateState::Downloaded)
        return;

    auto dialog  = std::make_unique<Dialog>("Actualización de Zio");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Versión instalada: v") + ZIO_VERSION + "     Nueva versión: v" + update.version)
        ->setBold(true);
    content->add<Label>(std::string("Novedades"))->setColor(ui_->theme().textDim);

    auto* scroll = content->add<ScrollContainer>(true, false);
    scroll->setMinSize({560.f, 240.f});
    auto* lines = scroll->add<VBox>();
    lines->setSeparation(2.f);
    const auto notes = formatReleaseNotes(update.notes, 66);
    if (notes.empty())
        lines->add<Label>(std::string("Esta versión no trae notas publicadas."))->setColor(ui_->theme().textDim);
    for (const NoteLine& line : notes)
    {
        auto* label = lines->add<Label>(line.text.empty() ? std::string(" ") : line.text);
        if (line.heading)
        {
            label->setBold(true);
            label->setColor(ui_->theme().accentHover);
        }
    }
    if (update.state == UpdateState::Downloading)
        content->add<Label>("Descargando... " + std::to_string(static_cast<int>(update.progress * 100.f)) + "%")
            ->setColor(ui_->theme().textDim);
    dialog->setContent(std::move(content));
    dialog->setWidth(620.f);

    Dialog* raw = dialog.get();
    if (update.state == UpdateState::Available)
    {
        dialog->addButton("Más tarde", [raw]() { raw->cancel(); });
        dialog->addButton("Descargar", [this, raw]() {
            raw->close();
            updater_.download();
            log("Descargando la actualización...");
        }, true);
    }
    else if (update.state == UpdateState::Downloaded)
    {
        dialog->addButton("Más tarde", [raw]() { raw->cancel(); });
        dialog->addButton("Reiniciar y actualizar", [this, raw]() {
            raw->close();
            if (context_.hasPending())
                return;
            restartAfterClose_ = true;
            context_.request(Action{ActionKind::CloseWindow, {}, {}});
        }, true);
    }
    else
    {
        dialog->addButton("Cerrar", [raw]() { raw->cancel(); }, true);
    }
    ui_->openPopup(std::move(dialog));
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

    {
        const std::string sceneName = context_.project().isOpen() ? context_.currentScene() : std::string();
        sceneTab_->setText((sceneName.empty() ? std::string("[sin guardar]") : sceneName) +
                           (context_.isDirty() ? " (*)" : ""));
        const std::string projectName = context_.project().isOpen() ? context_.project().name() : std::string();
        if (projectName != loggedProject_)
        {
            loggedProject_ = projectName;
            if (!projectName.empty())
                log("Proyecto abierto: " + projectName);
        }
        if (sceneName != loggedScene_)
        {
            loggedScene_ = sceneName;
            if (!sceneName.empty())
                log("Escena: " + sceneName);
        }
        if (context_.lastError() != loggedError_)
        {
            loggedError_ = context_.lastError();
            if (!loggedError_.empty())
                log(loggedError_, true);
        }
    }

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
    output_.refresh();
    flushBuildLines();
    pollScripts(realDt_);
    for (const std::string& line : zio::scripting::DotNetRuntime::takeLog())
        log(line, line.rfind("[error]", 0) == 0);
    refreshStatus();

    if (!context_.playing())
        previewAnimations(context_.scene().root(), realDt_);

    viewport_.update(realDt_, ui_->hasPopups() || textFocused());

    const std::string sceneRequest = zio::scripting::DotNetRuntime::takeSceneRequest();
    if (!sceneRequest.empty() && context_.playing())
    {
        if (const auto absolute = context_.project().resolve(sceneRequest))
        {
            std::string sceneError;
            zio::physics3d::World3D::instance().clear();
            if (!zio::scene3d::loadScene3DFile(context_.scene(), absolute->string(), &sceneError))
                log("No se pudo cargar la escena " + sceneRequest + ": " + sceneError, true);
        }
        else
        {
            log("Ruta de escena inválida: " + sceneRequest, true);
        }
    }

    auto& input = zio::input::InputManager::instance();
    if (!input.isMouseButtonDown(zio::events::MouseButton::Left) && !viewport_.dragging())
        context_.commitHistory();

    host_->frame(realDt_);
}

void EditorApp::previewAnimations(zio::scene3d::Node3D& node, float dt)
{
    if (auto* mesh = dynamic_cast<zio::scene3d::MeshNode*>(&node))
        if (mesh->isAnimationPlaying() || mesh->isMorphAnimationPlaying())
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

std::string EditorApp::currentSceneRelative() const
{
    const std::string& scene = context_.currentScene();
    if (scene.empty())
        return {};
    std::error_code ec;
    if (const auto absolute = context_.project().resolve(scene); absolute && fs::exists(*absolute, ec))
        return scene;
    const std::string guess = "assets/scenes/" + scene;
    if (const auto absolute = context_.project().resolve(guess); absolute && fs::exists(*absolute, ec))
        return guess;
    return {};
}

void EditorApp::showGameInfo()
{
    using namespace zio::ui;
    if (!context_.project().isOpen())
        return;
    const GameInfo current = context_.project().gameInfo();
    auto dialog            = std::make_unique<Dialog>("Información del juego");
    auto content           = std::make_unique<VBox>();
    const auto field       = [&](const char* label, const std::string& value) {
        content->add<Label>(std::string(label));
        auto* edit = content->add<LineEdit>();
        edit->setText(value);
        return edit;
    };
    auto* title       = field("Nombre del juego", current.title.empty() ? context_.project().name() : current.title);
    auto* author      = field("Autor o dueño", current.author);
    auto* version     = field("Versión", current.version);
    auto* description = field("Descripción", current.description);
    auto* copyright   = field("Copyright (por ejemplo © 2026 Tu nombre)", current.copyright);
    auto* website     = field("Sitio web", current.website);
    auto* license     = field("Licencia del juego", current.license);
    auto* scene       = field("Escena inicial (assets/scenes/...)",
                              current.entryScene.empty() ? currentSceneRelative() : current.entryScene);
    content->add<Label>(std::string("Icono del juego (archivo .ico del proyecto, vacío = icono de Zio)"));
    auto* icon = addPathField(content.get(), Browse::OpenFile, "Icono del juego",
                              {{"Icono (.ico)", "*.ico"}, {"Todos los archivos", "*.*"}});
    icon->setText(current.icon);
    auto* allow       = content->add<CheckBox>(std::string("Permitir abrir este juego en el editor (importar)"));
    allow->setChecked(current.allowImport);
    auto* error = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);
    dialog->setContent(std::move(content));
    dialog->setWidth(560.f);

    Dialog* raw  = dialog.get();
    auto confirm = [this, raw, current, title, author, version, description, copyright, website, license, scene, icon, allow, error]() {
        if (title->text().empty() || version->text().empty())
        {
            error->setText("El nombre y la versión no pueden estar vacíos.");
            return;
        }
        GameInfo info     = current;
        info.title        = title->text();
        info.author       = author->text();
        info.version      = version->text();
        info.description  = description->text();
        info.copyright    = copyright->text();
        info.website      = website->text();
        info.license      = license->text();
        info.entryScene   = scene->text();
        info.icon         = icon->text();
        info.allowImport  = allow->checked();
        if (info.id.empty())
            info.id = zio::pack::newPackageId();
        std::string problem;
        if (!context_.project().saveGameInfo(info, &problem))
        {
            error->setText(problem);
            return;
        }
        log("Información del juego guardada.");
        raw->close();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Guardar", confirm, true);
    dialog->setOnConfirm(confirm);
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(title);
}

void EditorApp::showExport()
{
    using namespace zio::ui;
    if (!context_.project().isOpen() || context_.playing())
        return;
    const GameInfo info = context_.project().gameInfo();
    if (info.author.empty())
    {
        showInfo("Faltan datos del juego", "Escribe el autor del juego en Proyecto > Información del juego antes de exportar.");
        return;
    }
    const std::string scene = info.entryScene.empty() ? currentSceneRelative() : info.entryScene;
    if (scene.empty())
    {
        showInfo("Falta la escena inicial",
                 "Guarda una escena y márcala como inicial en Proyecto > Información del juego antes de exportar.");
        return;
    }

    auto dialog  = std::make_unique<Dialog>("Exportar juego");
    auto content = std::make_unique<VBox>();
    const std::string title = info.title.empty() ? context_.project().name() : info.title;
    content->add<Label>(title + "  por  " + info.author + "   (v" + info.version + ")")->setBold(true);
    content->add<Label>(std::string("Carpeta de salida"));
    auto* output = addPathField(content.get(), Browse::Folder, "Carpeta de salida");
    output->setText((context_.project().rootDir() / "export").string());
    content->add<Label>(std::string("Nombre del archivo del juego"));
    auto* fileName = content->add<LineEdit>();
    fileName->setText(sanitizeExecutableName(title));
    content->add<Label>(std::string("Protección"));
    auto* protection = content->add<OptionButton>();
    protection->setItems({"Contraseña (recomendado)", "Archivo de clave aparte", "Sin contraseña (solo ofusca, no protege)"});
    protection->setSelected(0);
    auto* passwordLabel = content->add<Label>(std::string("Contraseña (mínimo 8 caracteres)"));
    auto* password      = content->add<LineEdit>();
    password->setPassword(true);
    auto* repeatLabel = content->add<Label>(std::string("Repite la contraseña"));
    auto* repeat      = content->add<LineEdit>();
    repeat->setPassword(true);
    auto* note = content->add<Label>(std::string());
    note->setColor(ui_->theme().textDim);
    auto* sign = content->add<CheckBox>(std::string("Firmar con mi clave de autor (demuestra que el juego es tuyo)"));
    sign->setChecked(true);
    auto* sources = content->add<CheckBox>(std::string("Incluir el código fuente de los scripts"));
    auto* error   = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);

    const auto refresh = [passwordLabel, password, repeatLabel, repeat, note, protection]() {
        const int mode = protection->selected();
        passwordLabel->setVisible(mode == 0);
        password->setVisible(mode == 0);
        repeatLabel->setVisible(mode == 0);
        repeat->setVisible(mode == 0);
        note->setText(mode == 0   ? "El juego se cifra con XChaCha20-Poly1305 y la clave sale de tu contraseña con Argon2id."
                      : mode == 1 ? "Se crea un archivo .zkey con la clave. No lo distribuyas con el juego."
                                  : "Cualquiera con el juego puede extraerlo. Sirve solo para evitar abrirlo por accidente.");
    };
    protection->setOnSelected([refresh](int) { refresh(); });
    refresh();
    dialog->setContent(std::move(content));
    dialog->setWidth(600.f);

    Dialog* raw  = dialog.get();
    auto confirm = [this, raw, info, scene, title, output, fileName, protection, password, repeat, sign, sources, error]() {
        ExportRequest request;
        request.projectRoot    = context_.project().rootDir();
        request.projectName    = context_.project().name();
        request.info           = info;
        request.info.entryScene = scene;
        request.outputDir      = output->text();
        request.executableName = fileName->text();
        request.sign           = sign->checked();
        request.includeSources = sources->checked();
        switch (protection->selected())
        {
        case 0:
            request.protection = zio::pack::Protection::Password;
            request.password   = password->text();
            if (request.password.size() < 8)
            {
                error->setText("La contraseña debe tener al menos 8 caracteres.");
                return;
            }
            if (request.password != repeat->text())
            {
                error->setText("Las contraseñas no coinciden.");
                return;
            }
            break;
        case 1:
            request.protection = zio::pack::Protection::KeyFile;
            break;
        default:
            request.protection = zio::pack::Protection::Embedded;
            break;
        }
        if (request.outputDir.empty() || sanitizeExecutableName(request.executableName).empty())
        {
            error->setText("Escribe la carpeta de salida y un nombre de archivo válido.");
            return;
        }
        raw->close();
        startExport(std::move(request));
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Exportar", confirm, true);
    dialog->setOnConfirm(confirm);
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(fileName);
}

void EditorApp::startExport(ExportRequest request)
{
    if (building_)
    {
        log("Espera a que termine la compilación en curso.", true);
        return;
    }
    const fs::path root = context_.project().rootDir();
    for (const auto& language : zio::scripting::Languages::instance().all())
    {
        if (!language->hasSources(root))
            continue;
        std::string problem;
        if (!language->toolsAvailable(&problem))
        {
            log(problem, true);
            offerInstall(language.get(), problem);
            showInfo("No se puede exportar", "Faltan herramientas para compilar los scripts de " + language->displayName() + ".");
            return;
        }
    }

    GameInfo stored = request.info;
    if (stored.id.empty())
        stored.id = zio::pack::newPackageId();
    std::string problem;
    if (!context_.project().saveGameInfo(stored, &problem))
    {
        log(problem, true);
        return;
    }
    request.info = stored;

    building_      = true;
    exportRequest_ = std::move(request);
    seenBuildLines_.clear();
    log("Exportando el juego a " + exportRequest_.outputDir.string() + "...");
    zio::jobs::JobSystem::instance().enqueue(
        [this]() {
            std::string problem;
            exportOk_ = exportGame(
                exportRequest_,
                [this](const std::string& line) {
                    std::lock_guard<std::mutex> lock(buildMutex_);
                    buildLines_.push_back(line);
                },
                exportResult_, &problem);
            exportError_ = problem;
        },
        [this]() {
            flushBuildLines();
            building_ = false;
            std::fill(exportRequest_.password.begin(), exportRequest_.password.end(), '\0');
            exportRequest_.password.clear();
            if (!exportOk_)
            {
                log(exportError_, true);
                showInfo("No se pudo exportar", exportError_);
                return;
            }
            std::string summary = "Juego listo en " + exportRequest_.outputDir.string() + "\n";
            summary += "Ejecutable: " + exportResult_.executable.filename().string() + "\n";
            summary += "Paquete cifrado: " + exportResult_.package.filename().string() + "\n";
            summary += "Distribuye los dos archivos juntos.\n";
            if (exportResult_.signedPackage)
                summary += "Firmado por ti. Huella: " + exportResult_.fingerprint + "\n";
            if (!exportResult_.keyFile.empty())
                summary += "Archivo de clave: " + exportResult_.keyFile.filename().string() +
                           ". Guárdalo aparte; sin él nadie puede abrir el juego. Para jugar: " +
                           exportResult_.executable.filename().string() + " --clave " + exportResult_.keyFile.filename().string() + "\n";
            log("Juego exportado: " + exportResult_.package.string());
            showInfo("Juego exportado", summary);
        });
}

void EditorApp::showImport()
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>("Importar juego");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Archivo del juego (.zgame)"));
    auto* package = addPathField(content.get(), Browse::OpenFile, "Archivo del juego",
                                 {{"Juego de Zio", "*.zgame"}, {"Todos los archivos", "*.*"}});
    content->add<Label>(std::string("Contraseña (si la tiene)"));
    auto* password = content->add<LineEdit>();
    password->setPassword(true);
    content->add<Label>(std::string("Archivo de clave (.zkey, si lo tiene)"));
    auto* keyFile = addPathField(content.get(), Browse::OpenFile, "Archivo de clave",
                                 {{"Clave de Zio", "*.zkey"}, {"Todos los archivos", "*.*"}});
    content->add<Label>(std::string("Carpeta donde crear el proyecto"));
    auto* parent = addPathField(content.get(), Browse::Folder, "Carpeta del proyecto");
    std::error_code ec;
    parent->setText(fs::current_path(ec).string());
    content->add<Label>(std::string("Nombre del proyecto (vacío: el del juego)"));
    auto* name = content->add<LineEdit>();
    auto* error = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);
    dialog->setContent(std::move(content));
    dialog->setWidth(560.f);

    Dialog* raw  = dialog.get();
    auto confirm = [this, raw, package, password, keyFile, parent, name, error]() {
        ImportRequest request;
        request.package           = package->text();
        request.destinationParent = parent->text();
        request.credentials.password = password->text();
        if (!keyFile->text().empty())
        {
            std::ifstream in(keyFile->text(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            request.credentials.key = zio::pack::keyFromText(text.str());
            if (!request.credentials.key)
            {
                error->setText("El archivo de clave no es válido.");
                return;
            }
        }
        zio::pack::PackageHeader header;
        std::string problem;
        if (!zio::pack::readHeader(request.package, header, &problem))
        {
            error->setText(problem);
            return;
        }
        request.projectName = name->text().empty() ? sanitizeExecutableName(header.info.name) : name->text();
        error->setText("Descifrando...");
        fs::path root;
        if (!importGame(request, root, header, &problem))
        {
            error->setText(problem);
            return;
        }
        raw->close();
        log("Juego \"" + header.info.name + "\" de " + header.info.author + " importado en " + root.string());
        context_.request(Action{ActionKind::OpenProject, {}, root.string()});
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Importar", confirm, true);
    dialog->setOnConfirm(confirm);
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(package);
}

void EditorApp::showPlayPackage()
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>("Jugar un juego exportado");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Archivo del juego (.zgame)"));
    auto* package = addPathField(content.get(), Browse::OpenFile, "Archivo del juego",
                                 {{"Juego de Zio", "*.zgame"}, {"Todos los archivos", "*.*"}});
    content->add<Label>(std::string("Archivo de clave (.zkey, si lo tiene)"));
    auto* keyFile = addPathField(content.get(), Browse::OpenFile, "Archivo de clave",
                                 {{"Clave de Zio", "*.zkey"}, {"Todos los archivos", "*.*"}});
    auto* error   = content->add<Label>(std::string());
    error->setColor(ui_->theme().danger);
    dialog->setContent(std::move(content));
    dialog->setWidth(560.f);

    Dialog* raw  = dialog.get();
    auto confirm = [this, raw, package, keyFile, error]() {
        const fs::path file = package->text();
        std::error_code ec;
        if (!fs::exists(file, ec))
        {
            error->setText("No existe ese archivo.");
            return;
        }
        const fs::path sibling = file.parent_path() / (file.stem().string() + ".exe");
        const fs::path program = fs::exists(sibling, ec) ? sibling : editorExecutablePath();
        std::wstring arguments = L"--jugar \"" + file.wstring() + L"\"";
        if (!keyFile->text().empty())
            arguments += L" --clave \"" + fs::path(keyFile->text()).wstring() + L"\"";
        const HINSTANCE result =
            ShellExecuteW(nullptr, L"open", program.c_str(), arguments.c_str(), file.parent_path().c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            error->setText("No se pudo iniciar el juego.");
            return;
        }
        raw->close();
    };
    dialog->addButton("Cancelar", [raw]() { raw->cancel(); });
    dialog->addButton("Jugar", confirm, true);
    dialog->setOnConfirm(confirm);
    ui_->openPopup(std::move(dialog));
    ui_->setFocus(package);
}

void EditorApp::showNewProject()
{
    using namespace zio::ui;
    auto dialog  = std::make_unique<Dialog>("Nuevo proyecto");
    auto content = std::make_unique<VBox>();
    content->add<Label>(std::string("Carpeta padre"));
    auto* parent = addPathField(content.get(), Browse::Folder, "Carpeta padre del proyecto");
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
    auto* path = addPathField(content.get(), Browse::OpenFile, "Abrir proyecto",
                              {{"Proyecto de Zio", "project.zio"}, {"Todos los archivos", "*.*"}});
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
