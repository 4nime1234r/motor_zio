#include "core/EditorContext.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <system_error>

#include "zio/math/Quaternion.h"
#include "zio/scene3d/SceneIO3D.h"

namespace fs = std::filesystem;

namespace zio_editor
{

namespace
{

void setError(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

}

EditorContext::EditorContext()
{
    scene_.camera().position = {6.f, 5.f, 8.f};
}

zio::scene3d::MeshNode* EditorContext::selectedMesh() const
{
    return dynamic_cast<zio::scene3d::MeshNode*>(selected_);
}

bool EditorContext::contains(const zio::scene3d::Node3D* node) const
{
    for (const zio::scene3d::Node3D* n = node; n; n = n->parent())
        if (n == &scene_.root())
            return true;
    return false;
}

void EditorContext::setSelection(std::vector<zio::scene3d::Node3D*> nodes)
{
    selection_ = std::move(nodes);
    selected_  = selection_.empty() ? nullptr : selection_.back();
}

void EditorContext::clearSelection()
{
    setSelection({});
}

void EditorContext::selectNodes(std::vector<zio::scene3d::Node3D*> nodes)
{
    std::vector<zio::scene3d::Node3D*> valid;
    for (zio::scene3d::Node3D* node : nodes)
        if (node && contains(node))
            valid.push_back(node);
    setSelection(std::move(valid));
}

bool EditorContext::isSelected(const zio::scene3d::Node3D* node) const
{
    return std::find(selection_.begin(), selection_.end(), node) != selection_.end();
}

bool EditorContext::select(zio::scene3d::Node3D* node)
{
    if (!node)
    {
        clearSelection();
        return true;
    }
    if (!contains(node))
        return false;
    setSelection({node});
    return true;
}

bool EditorContext::toggleSelect(zio::scene3d::Node3D* node)
{
    if (!node || !contains(node))
        return false;
    std::vector<zio::scene3d::Node3D*> next = selection_;
    const auto it                           = std::find(next.begin(), next.end(), node);
    if (it != next.end())
        next.erase(it);
    else
        next.push_back(node);
    setSelection(std::move(next));
    return true;
}

std::vector<zio::scene3d::Node3D*> EditorContext::selectionRoots() const
{
    std::vector<zio::scene3d::Node3D*> roots;
    for (zio::scene3d::Node3D* node : selection_)
    {
        if (node == &scene_.root())
            continue;
        bool nested = false;
        for (const zio::scene3d::Node3D* ancestor = node->parent(); ancestor && !nested; ancestor = ancestor->parent())
            nested = isSelected(ancestor);
        if (!nested)
            roots.push_back(node);
    }
    return roots;
}

bool EditorContext::editable(std::string* error)
{
    if (!playing_)
        return true;
    setError(error, "No se puede editar mientras se juega");
    return false;
}

void EditorContext::resetScene()
{
    clearSelection();
    scene_.root().removeAllChildren();
    scene_.root().setName("");
    scene_.root().setPosition(zio::math::Vec3::zero());
    scene_.root().setRotation(zio::math::Quaternion::identity());
    scene_.root().setScale(zio::math::Vec3::one());
    scene_.root().setVisible(true);
    scene_.camera()          = zio::scene3d::Camera{};
    scene_.camera().position = {6.f, 5.f, 8.f};
    scene_.light()  = zio::scene3d::Light{};
    currentScene_.clear();
    dirty_ = false;
    ++revision_;
    resetHistory();
}

void EditorContext::resetHistory()
{
    undo_.clear();
    redo_.clear();
    current_.json      = zio::scene3d::serializeScene3D(scene_);
    current_.selection = selectionPaths();
    committedStamp_    = changeStamp_;
}

std::vector<std::vector<std::size_t>> EditorContext::selectionPaths() const
{
    std::vector<std::vector<std::size_t>> paths;
    for (const zio::scene3d::Node3D* selectedNode : selection_)
    {
        std::vector<std::size_t> path;
        for (const zio::scene3d::Node3D* node = selectedNode; node && node->parent(); node = node->parent())
        {
            const auto& siblings = node->parent()->children();
            for (std::size_t i = 0; i < siblings.size(); ++i)
                if (siblings[i].get() == node)
                {
                    path.insert(path.begin(), i);
                    break;
                }
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

bool EditorContext::restore(const Snapshot& snapshot)
{
    std::string error;
    if (!zio::scene3d::deserializeScene3D(scene_, snapshot.json, &error))
        return fail("No se pudo restaurar la escena: " + error);

    std::vector<zio::scene3d::Node3D*> restored;
    for (const auto& path : snapshot.selection)
    {
        zio::scene3d::Node3D* node = &scene_.root();
        bool found                 = !path.empty();
        for (std::size_t index : path)
        {
            if (index >= node->children().size())
            {
                found = false;
                break;
            }
            node = node->children()[index].get();
        }
        if (found)
            restored.push_back(node);
    }
    setSelection(std::move(restored));
    dirty_    = true;
    ++revision_;
    return true;
}

void EditorContext::commitHistory()
{
    if (playing_)
        return;
    if (changeStamp_ == committedStamp_)
    {
        current_.selection = selectionPaths();
        return;
    }
    committedStamp_ = changeStamp_;
    std::string json = zio::scene3d::serializeScene3D(scene_);
    if (json == current_.json)
        return;

    constexpr std::size_t kMaxHistory = 200;
    undo_.push_back(std::move(current_));
    if (undo_.size() > kMaxHistory)
        undo_.erase(undo_.begin());
    redo_.clear();
    current_.json      = std::move(json);
    current_.selection = selectionPaths();
}

bool EditorContext::undo()
{
    if (playing_)
        return false;
    commitHistory();
    if (undo_.empty())
        return false;
    Snapshot target = std::move(undo_.back());
    undo_.pop_back();
    current_.selection = selectionPaths();
    Snapshot now       = current_;
    if (!restore(target))
    {
        undo_.push_back(std::move(target));
        return false;
    }
    redo_.push_back(std::move(now));
    current_        = std::move(target);
    committedStamp_ = changeStamp_;
    lastError_.clear();
    return true;
}

bool EditorContext::redo()
{
    if (playing_)
        return false;
    commitHistory();
    if (redo_.empty())
        return false;
    Snapshot target = std::move(redo_.back());
    redo_.pop_back();
    current_.selection = selectionPaths();
    Snapshot now       = current_;
    if (!restore(target))
    {
        redo_.push_back(std::move(target));
        return false;
    }
    undo_.push_back(std::move(now));
    current_        = std::move(target);
    committedStamp_ = changeStamp_;
    lastError_.clear();
    return true;
}

void EditorContext::enterProjectDirectory()
{
    std::error_code ec;
    fs::current_path(project_.rootDir(), ec);
}

bool EditorContext::fail(std::string message)
{
    lastError_ = std::move(message);
    return false;
}

bool EditorContext::createProject(const fs::path& parentDir, std::string_view name, std::string* error)
{
    ProjectManager candidate;
    if (!candidate.createProject(parentDir, name, error))
        return false;
    project_ = std::move(candidate);
    resetScene();
    enterProjectDirectory();
    return true;
}

bool EditorContext::openProject(const fs::path& path, std::string* error)
{
    ProjectManager candidate;
    if (!candidate.loadProject(path, error))
        return false;
    project_ = std::move(candidate);
    resetScene();
    enterProjectDirectory();
    return true;
}

bool EditorContext::openScene(std::string_view projectRelativePath, std::string* error)
{
    if (!project_.isOpen())
    {
        setError(error, "No hay un proyecto abierto");
        return false;
    }
    const auto absolute = project_.resolve(projectRelativePath);
    if (!absolute)
    {
        setError(error, "Ruta inválida o fuera del proyecto: " + std::string(projectRelativePath));
        return false;
    }
    if (!ProjectManager::isSceneFile(*absolute))
    {
        setError(error, "No es una escena (se esperaba " + std::string(ProjectManager::kSceneExtension) + ")");
        return false;
    }

    std::string loadError;
    if (!zio::scene3d::loadScene3DFile(scene_, absolute->string(), &loadError))
    {
        setError(error, loadError);
        return false;
    }

    clearSelection();
    const auto relative = project_.toRelative(*absolute);
    currentScene_       = relative ? *relative : std::string(projectRelativePath);
    dirty_              = false;
    ++revision_;
    resetHistory();
    return true;
}

std::string EditorContext::uniqueName(const zio::scene3d::Node3D& parent, const std::string& base) const
{
    auto taken = [&](const std::string& name) {
        for (const auto& child : parent.children())
            if (child->name() == name)
                return true;
        return false;
    };
    if (!taken(base))
        return base;
    for (int i = 2;; ++i)
    {
        std::string candidate = base + " " + std::to_string(i);
        if (!taken(candidate))
            return candidate;
    }
}

std::vector<std::string> EditorContext::meshAssets() const
{
    return project_.listFiles("assets/models", ".obj");
}

zio::scene3d::Node3D* EditorContext::addNode(NewNodeKind kind, std::string_view meshPath, std::string* error)
{
    auto reject = [&](std::string message) -> zio::scene3d::Node3D* {
        setError(error, message);
        fail(std::move(message));
        return nullptr;
    };

    if (!project_.isOpen())
        return reject("No hay un proyecto abierto");
    if (std::string playError; !editable(&playError))
        return reject(playError);

    zio::scene3d::Node3D* parent = (selected_ && contains(selected_)) ? selected_ : &scene_.root();

    std::unique_ptr<zio::scene3d::Node3D> node;
    std::string baseName;
    switch (kind)
    {
    case NewNodeKind::Empty:
        node     = std::make_unique<zio::scene3d::Node3D>();
        baseName = "Nodo";
        break;
    case NewNodeKind::Cube:
    {
        auto mesh = std::make_unique<zio::scene3d::MeshNode>();
        std::string meshError;
        if (!mesh->setMeshSource("cube", &meshError))
            return reject("No se pudo crear el cubo: " + meshError);
        node     = std::move(mesh);
        baseName = "Cubo";
        break;
    }
    case NewNodeKind::Mesh:
    {
        const auto absolute = project_.resolve(meshPath);
        if (!absolute)
            return reject("Ruta inválida o fuera del proyecto: " + std::string(meshPath));
        if (!ProjectManager::isMeshFile(*absolute))
            return reject("No es una malla (se esperaba .obj): " + std::string(meshPath));
        std::error_code ec;
        if (!fs::is_regular_file(*absolute, ec))
            return reject("No existe la malla: " + std::string(meshPath));

        enterProjectDirectory();
        const auto relative = project_.toRelative(*absolute);
        auto mesh           = std::make_unique<zio::scene3d::MeshNode>();
        std::string meshError;
        if (!mesh->setMeshSource(relative ? *relative : std::string(meshPath), &meshError))
            return reject("No se pudo cargar la malla: " + meshError);
        node     = std::move(mesh);
        baseName = absolute->stem().string();
        break;
    }
    }

    node->setName(uniqueName(*parent, baseName));
    zio::scene3d::Node3D* added = parent->addChild(std::move(node));
    setSelection({added});
    markDirty();
    lastError_.clear();
    return added;
}

bool EditorContext::removeSelected(std::string* error)
{
    auto reject = [&](std::string message) {
        setError(error, message);
        return fail(std::move(message));
    };

    if (std::string playError; !editable(&playError))
        return reject(playError);
    if (selection_.empty())
        return reject("No hay ningún objeto seleccionado");
    const auto roots = selectionRoots();
    if (roots.empty())
        return reject("No se puede eliminar la raíz de la escena");

    for (zio::scene3d::Node3D* node : roots)
    {
        zio::scene3d::Node3D* parent = node->parent();
        if (!parent)
            continue;
        std::unique_ptr<zio::scene3d::Node3D> removed = parent->removeChild(node);
        removed.reset();
    }
    clearSelection();
    markDirty();
    lastError_.clear();
    return true;
}

bool EditorContext::copySelection()
{
    const auto roots = selectionRoots();
    if (roots.empty())
        return false;
    std::vector<std::string> copied;
    for (const zio::scene3d::Node3D* node : roots)
        copied.push_back(zio::scene3d::serializeNode3D(*node));
    clipboard_ = std::move(copied);
    return true;
}

bool EditorContext::paste(std::string* error)
{
    auto reject = [&](std::string message) {
        setError(error, message);
        return fail(std::move(message));
    };

    if (std::string playError; !editable(&playError))
        return reject(playError);
    if (clipboard_.empty())
        return reject("El portapapeles está vacío");

    zio::scene3d::Node3D* parent = (selected_ && contains(selected_)) ? selected_ : &scene_.root();

    std::vector<std::unique_ptr<zio::scene3d::Node3D>> built;
    for (const std::string& text : clipboard_)
    {
        std::string parseError;
        auto node = zio::scene3d::deserializeNode3D(text, &parseError);
        if (!node)
            return reject("No se pudo pegar: " + parseError);
        built.push_back(std::move(node));
    }

    std::vector<zio::scene3d::Node3D*> pasted;
    for (auto& node : built)
    {
        node->setName(uniqueName(*parent, node->name().empty() ? std::string("Nodo") : node->name()));
        pasted.push_back(parent->addChild(std::move(node)));
    }
    setSelection(std::move(pasted));
    markDirty();
    lastError_.clear();
    return true;
}

bool EditorContext::duplicateSelection(std::string* error)
{
    auto reject = [&](std::string message) {
        setError(error, message);
        return fail(std::move(message));
    };

    if (std::string playError; !editable(&playError))
        return reject(playError);
    const auto roots = selectionRoots();
    if (roots.empty())
        return reject("No hay ningún objeto seleccionado");

    std::vector<zio::scene3d::Node3D*> copies;
    for (zio::scene3d::Node3D* node : roots)
    {
        zio::scene3d::Node3D* parent = node->parent();
        if (!parent)
            continue;
        std::string parseError;
        auto copy = zio::scene3d::deserializeNode3D(zio::scene3d::serializeNode3D(*node), &parseError);
        if (!copy)
            return reject("No se pudo duplicar: " + parseError);
        copy->setName(uniqueName(*parent, node->name().empty() ? std::string("Nodo") : node->name()));
        copies.push_back(parent->addChild(std::move(copy)));
    }
    if (copies.empty())
        return false;
    setSelection(std::move(copies));
    markDirty();
    lastError_.clear();
    return true;
}

bool EditorContext::startPlay()
{
    if (playing_)
        return true;
    if (pending_)
        return fail("Hay una decisión pendiente sobre los cambios sin guardar");
    commitHistory();
    playSnapshot_ = zio::scene3d::serializeScene3D(scene_);
    playWasDirty_ = dirty_;
    clearSelection();
    playing_ = true;
    lastError_.clear();
    return true;
}

void EditorContext::stopPlay()
{
    if (!playing_)
        return;
    playing_ = false;
    clearSelection();
    std::string error;
    if (!zio::scene3d::deserializeScene3D(scene_, playSnapshot_, &error))
        fail("No se pudo restaurar la escena tras jugar: " + error);
    playSnapshot_.clear();
    dirty_ = playWasDirty_;
}

bool EditorContext::writeSceneFile(const fs::path& file, std::string* error) const
{
    std::error_code statusEc;
    const fs::file_status status = fs::status(file, statusEc);
    if (!statusEc && fs::exists(status) &&
        (status.permissions() & fs::perms::owner_write) == fs::perms::none)
    {
        setError(error, "El archivo es de solo lectura: " + file.string());
        return false;
    }

    const std::string text = zio::scene3d::serializeScene3D(scene_);
    const fs::path temp    = file.string() + ".tmp";

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            setError(error, "No se puede escribir en " + temp.string());
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out)
        {
            out.close();
            std::error_code ignore;
            fs::remove(temp, ignore);
            setError(error, "Fallo al escribir " + temp.string());
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp, file, ec);
    if (ec)
    {
        std::error_code ignore;
        fs::remove(temp, ignore);
        setError(error, "No se pudo reemplazar " + file.string() + ": " + ec.message());
        return false;
    }
    return true;
}

bool EditorContext::saveScene(std::string* error)
{
    std::string message;
    if (!project_.isOpen())
        message = "No hay un proyecto abierto";
    else if (currentScene_.empty())
        message = "La escena no tiene archivo; usa Guardar como";
    if (!message.empty())
    {
        setError(error, message);
        return fail(std::move(message));
    }

    const auto absolute = project_.resolve(currentScene_);
    if (!absolute)
    {
        message = "Ruta inválida o fuera del proyecto: " + currentScene_;
        setError(error, message);
        return fail(std::move(message));
    }
    if (!writeSceneFile(*absolute, &message))
    {
        message = "No se pudo guardar " + currentScene_ + ": " + message;
        setError(error, message);
        return fail(std::move(message));
    }

    dirty_ = false;
    lastError_.clear();
    return true;
}

bool EditorContext::saveSceneAs(std::string_view name, std::string* error)
{
    std::string message;
    if (!project_.isOpen())
    {
        message = "No hay un proyecto abierto";
    }
    else
    {
        std::string reason;
        if (!ProjectManager::isValidProjectName(name, &reason))
            message = "Nombre de escena no válido: " + reason;
    }
    if (!message.empty())
    {
        setError(error, message);
        return fail(std::move(message));
    }

    const std::string relative = "assets/scenes/" + std::string(name) + ProjectManager::kSceneExtension;
    const auto absolute        = project_.resolve(relative);
    std::error_code ec;
    if (!absolute)
    {
        message = "Ruta inválida: " + relative;
        setError(error, message);
        return fail(std::move(message));
    }
    if (fs::exists(*absolute, ec))
    {
        message = "Ya existe una escena con ese nombre: " + relative;
        setError(error, message);
        return fail(std::move(message));
    }
    if (!writeSceneFile(*absolute, &message))
    {
        message = "No se pudo guardar " + relative + ": " + message;
        setError(error, message);
        return fail(std::move(message));
    }

    currentScene_ = relative;
    dirty_        = false;
    lastError_.clear();
    return true;
}

bool EditorContext::perform(const Action& action)
{
    std::string error;
    switch (action.kind)
    {
    case ActionKind::OpenScene:
        if (!openScene(action.text, &error))
            return fail("No se pudo abrir " + action.text + ": " + error);
        break;
    case ActionKind::OpenProject:
        if (!openProject(action.path, &error))
            return fail("No se pudo abrir el proyecto: " + error);
        break;
    case ActionKind::CreateProject:
        if (!createProject(action.path, action.text, &error))
            return fail("No se pudo crear el proyecto: " + error);
        break;
    case ActionKind::CloseWindow:
        closeApproved_ = true;
        break;
    }
    lastError_.clear();
    return true;
}

bool EditorContext::request(Action action)
{
    stopPlay();
    if (pending_)
        return fail("Hay una decisión pendiente sobre los cambios sin guardar");
    if (!dirty_)
        return perform(action);
    pending_ = std::move(action);
    return true;
}

bool EditorContext::resolvePending(Decision decision)
{
    if (!pending_)
        return true;

    if (decision == Decision::Cancel)
    {
        pending_.reset();
        return true;
    }

    if (decision == Decision::Save)
    {
        std::string error;
        if (!saveScene(&error))
            return false;
    }

    const Action action = *pending_;
    pending_.reset();
    return perform(action);
}

}
