#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "project/ProjectManager.h"
#include "zio/scene3d/Node3D.h"
#include "zio/scene3d/Scene3D.h"

namespace zio_editor
{

enum class ActionKind
{
    OpenScene,
    OpenProject,
    CreateProject,
    CloseWindow
};

struct Action
{
    ActionKind kind = ActionKind::OpenScene;
    std::string text;
    std::filesystem::path path;
};

enum class NewNodeKind
{
    Empty,
    Cube,
    Mesh
};

enum class Decision
{
    Save,
    Discard,
    Cancel
};

class EditorContext
{
public:
    EditorContext();

    EditorContext(const EditorContext&)            = delete;
    EditorContext& operator=(const EditorContext&) = delete;

    zio::scene3d::Scene3D& scene() { return scene_; }
    const zio::scene3d::Scene3D& scene() const { return scene_; }

    ProjectManager& project() { return project_; }
    const ProjectManager& project() const { return project_; }

    zio::scene3d::Node3D* selected() const { return selected_; }
    zio::scene3d::MeshNode* selectedMesh() const;
    bool select(zio::scene3d::Node3D* node);
    bool toggleSelect(zio::scene3d::Node3D* node);
    void clearSelection();
    void selectNodes(std::vector<zio::scene3d::Node3D*> nodes);
    std::uint64_t changeStamp() const { return changeStamp_; }
    bool isSelected(const zio::scene3d::Node3D* node) const;
    const std::vector<zio::scene3d::Node3D*>& selection() const { return selection_; }
    std::vector<zio::scene3d::Node3D*> selectionRoots() const;

    bool createProject(const std::filesystem::path& parentDir, std::string_view name, std::string* error = nullptr);
    bool openProject(const std::filesystem::path& path, std::string* error = nullptr);
    bool openScene(std::string_view projectRelativePath, std::string* error = nullptr);

    zio::scene3d::Node3D* addNode(NewNodeKind kind, std::string_view meshPath = {}, std::string* error = nullptr);
    std::vector<std::string> meshAssets() const;

    bool removeSelected(std::string* error = nullptr);

    bool copySelection();
    bool paste(std::string* error = nullptr);
    bool duplicateSelection(std::string* error = nullptr);
    bool canPaste() const { return !clipboard_.empty(); }

    bool playing() const { return playing_; }
    bool startPlay();
    void stopPlay();

    bool saveScene(std::string* error = nullptr);
    bool saveSceneAs(std::string_view name, std::string* error = nullptr);
    bool canSave() const { return project_.isOpen() && !currentScene_.empty(); }

    const std::string& currentScene() const { return currentScene_; }
    std::uint64_t sceneRevision() const { return revision_; }

    bool isDirty() const { return dirty_; }
    void markDirty()
    {
        dirty_ = true;
        ++changeStamp_;
    }

    void commitHistory();
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    bool request(Action action);
    bool hasPending() const { return pending_.has_value(); }
    const Action& pending() const { return *pending_; }
    bool resolvePending(Decision decision);
    bool closeApproved() const { return closeApproved_; }

    const std::string& lastError() const { return lastError_; }
    void clearError() { lastError_.clear(); }

private:
    struct Snapshot
    {
        std::string json;
        std::vector<std::vector<std::size_t>> selection;
    };

    void resetScene();
    void resetHistory();
    std::vector<std::vector<std::size_t>> selectionPaths() const;
    void setSelection(std::vector<zio::scene3d::Node3D*> nodes);
    bool editable(std::string* error);
    bool restore(const Snapshot& snapshot);
    bool contains(const zio::scene3d::Node3D* node) const;
    void enterProjectDirectory();
    bool writeSceneFile(const std::filesystem::path& file, std::string* error) const;
    bool perform(const Action& action);
    bool fail(std::string message);
    std::string uniqueName(const zio::scene3d::Node3D& parent, const std::string& base) const;

    zio::scene3d::Scene3D scene_;
    ProjectManager project_;
    zio::scene3d::Node3D* selected_ = nullptr;
    std::vector<zio::scene3d::Node3D*> selection_;
    std::vector<std::string> clipboard_;
    bool playing_      = false;
    bool playWasDirty_ = false;
    std::string playSnapshot_;
    std::string currentScene_;
    bool dirty_              = false;
    std::uint64_t revision_  = 0;
    std::optional<Action> pending_;
    bool closeApproved_ = false;
    std::string lastError_;
    std::uint64_t changeStamp_     = 0;
    std::uint64_t committedStamp_  = 0;
    Snapshot current_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
};

}
