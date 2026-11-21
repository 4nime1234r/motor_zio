#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/EditorCamera.h"
#include "core/EditorContext.h"
#include "core/Gizmo.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/DiagnosticsPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/SceneHierarchyPanel.h"
#include "update/Updater.h"

namespace zio_editor
{

class EditorWindow;

class EditorApp
{
public:
    EditorApp();
    ~EditorApp();

    EditorApp(const EditorApp&)            = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    void openProjectAtStartup(const std::filesystem::path& path);
    void openRecentProject();
    void run();

private:
    enum class CameraDrag
    {
        None,
        Orbit,
        Pan
    };

    void drawUi(float frameTimeMs);
    void drawMenuBar();
    void drawUpdateStatus();
    void drawProjectPopups();
    void drawSceneSavePopups();
    void saveCurrentScene();
    void updateViewportCamera();
    void handleViewportInput();
    void handleViewportKeys();
    void drawViewportContextMenu();
    void focusSelection();
    void togglePlay();
    void drawEditItems();
    bool beginGizmoDrag(zio::scene3d::Node3D& node, const GizmoAxes& axes, int axis, const Ray& ray);
    void applyGizmoDrag(const Ray& ray, bool snap);
    void endGizmoDrag();
    bool dragging() const { return gizmoDrag_.active() || rotateDrag_.active(); }
    int activeAxis() const
    {
        return gizmoDrag_.active() ? gizmoDrag_.axis() : (rotateDrag_.active() ? rotateDrag_.axis() : -1);
    }
    void renderScene();
    void rememberProject();

    std::unique_ptr<EditorWindow> window_;
    EditorContext context_;
    EditorCamera editorCamera_;
    std::uint64_t seenRevision_ = static_cast<std::uint64_t>(-1);
    CameraDrag cameraDrag_      = CameraDrag::None;
    GizmoDrag gizmoDrag_;
    zio::scene3d::Node3D* dragNode_ = nullptr;
    int hoveredAxis_                = -1;
    RotateDrag rotateDrag_;
    GizmoMode gizmoMode_            = GizmoMode::Move;
    zio::math::Quaternion startRotation_;
    zio::math::Vec3 startScale_;
    zio::math::Vec3 dragOrigin_;
    zio::math::Vec3 dragDirection_;
    std::vector<std::pair<zio::scene3d::Node3D*, zio::math::Vec3>> groupStart_;
    float startLength_              = 1.f;
    float rightPressX_              = 0.f;
    float rightPressY_              = 0.f;
    bool openViewportMenu_          = false;

    SceneHierarchyPanel hierarchyPanel_;
    InspectorPanel inspectorPanel_;
    ContentBrowserPanel contentBrowserPanel_;
    DiagnosticsPanel diagnosticsPanel_;

    Updater updater_;
    bool restartAfterClose_ = false;

    std::string imguiIniPath_;
    std::filesystem::path recentFile_;
    std::filesystem::path rememberedProject_;
    char newProjectParent_[512] = {};
    char newProjectName_[128]   = {};
    char openProjectPath_[512]  = {};
    char saveAsName_[128]       = {};
    bool wantNewProjectPopup_   = false;
    bool wantOpenProjectPopup_  = false;
    bool wantSaveAsPopup_       = false;
    std::string popupError_;
    std::string modalError_;
    std::string statusError_;
};

}
