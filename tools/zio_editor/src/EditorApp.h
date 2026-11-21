#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "core/EditorContext.h"
#include "ui/Panels.h"
#include "ui/ToolButton.h"
#include "ui/ViewportController.h"
#include "update/Updater.h"
#include "zio/core/Application.h"
#include "zio/events/EventBus.h"
#include "zio/ui/UI.h"

namespace zio_editor
{

class EditorApp final : public zio::core::Application, public EditorServices
{
public:
    EditorApp();
    ~EditorApp() override;

    void setStartupProject(std::filesystem::path path) { startupProject_ = std::move(path); }

    EditorContext& context() override { return context_; }
    zio::ui::Context& ui() override { return *ui_; }
    std::vector<zio::ui::MenuItem> addObjectItems() override;
    std::vector<zio::ui::MenuItem> editItems() override;
    void showPopup(std::vector<zio::ui::MenuItem> items, zio::ui::Vec2 position) override;
    float frameMs() const override { return frameMs_; }
    void log(const std::string& text, bool error = false) override { output_.add(text, error); }

protected:
    bool onInit() override;
    void onBeforeTick(float dt) override;
    void onRender(float dt) override;
    void onShutdown() override;
    bool onCloseRequested() override;

private:
    void buildUi();
    void buildTopBar(zio::ui::Widget* page);
    void buildStatusBar(zio::ui::Widget* page);
    void refreshStatus();
    void refreshUpdate();
    void handleKey(const zio::events::KeyEvent& event);
    bool textFocused() const;
    void togglePlay();
    void saveCurrentScene();
    void rememberProject();
    void openInitialProject();
    void saveLayout();
    void previewAnimations(zio::scene3d::Node3D& node, float dt);

    std::vector<zio::ui::MenuItem> projectItems();
    std::vector<zio::ui::MenuItem> sceneItems();
    std::vector<zio::ui::MenuItem> viewItems();
    std::vector<zio::ui::MenuItem> helpItems();
    std::vector<zio::ui::MenuItem> sideItems(const std::string& id);

    void showNewProject();
    void showOpenProject();
    void showSaveAs();
    void showUnsaved();
    void showUpdate();
    void showInfo(const std::string& title, const std::string& message);
    void checkUpdates();
    std::string describePending() const;

    EditorContext context_;
    Updater updater_;
    std::unique_ptr<zio::ui::Context> ui_;
    std::unique_ptr<zio::ui::EngineHost> host_;
    ViewportController viewport_;
    HierarchyPanel hierarchy_;
    InspectorPanel inspector_;
    FilesPanel files_;
    OutputPanel output_;
    PerformancePanel performance_;

    zio::ui::DockSpace* dock_       = nullptr;
    ToolButton* play_               = nullptr;
    ToolButton* stop_               = nullptr;
    zio::ui::Label* statusScene_    = nullptr;
    zio::ui::Label* sceneTab_       = nullptr;
    std::string loggedProject_;
    std::string loggedScene_;
    std::string loggedError_;
    zio::ui::Label* statusError_    = nullptr;
    zio::ui::Label* statusRight_    = nullptr;
    zio::ui::Button* updateBadge_   = nullptr;
    bool manualCheck_               = false;
    UpdateState lastUpdateState_    = UpdateState::Idle;

    std::filesystem::path startupProject_;
    std::filesystem::path recentFile_;
    std::filesystem::path layoutFile_;
    std::filesystem::path rememberedProject_;
    std::string statusErrorText_;

    zio::events::ListenerId keyListener_ = zio::events::kInvalidListenerId;
    bool ctrl_  = false;
    bool shift_ = false;
    bool unsavedOpen_       = false;
    bool restartAfterClose_ = false;

    std::chrono::steady_clock::time_point lastTick_;
    float frameMs_  = 0.f;
    float realDt_   = 1.f / 60.f;
};

}
