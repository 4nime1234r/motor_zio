#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/EditorContext.h"
#include "core/EulerCache.h"
#include "core/AssetFiles.h"
#include "zio/scripting/Language.h"
#include "zio/ui/UI.h"

namespace zio_editor
{

class EditorServices
{
public:
    virtual ~EditorServices() = default;

    virtual EditorContext& context()                                                   = 0;
    virtual zio::ui::Context& ui()                                                     = 0;
    virtual std::vector<zio::ui::MenuItem> addObjectItems()                            = 0;
    virtual std::vector<zio::ui::MenuItem> editItems()                                 = 0;
    virtual void showPopup(std::vector<zio::ui::MenuItem> items, zio::ui::Vec2 position) = 0;
    virtual float frameMs() const                                                      = 0;
    virtual void log(const std::string& text, bool error = false)                      = 0;
    virtual void openInCodeEditor(const std::filesystem::path& path)                   = 0;
    virtual void scriptFileCreated(const std::string& relative)                        = 0;
};

class HierarchyPanel
{
public:
    explicit HierarchyPanel(EditorServices& services) : services_(services) {}

    std::unique_ptr<zio::ui::Widget> build();
    void refresh();

private:
    void rebuild();
    void addChildren(zio::ui::TreeItem* parent, zio::scene3d::Node3D& node, const std::unordered_map<const void*, bool>& expanded);
    bool matchesFilter(const zio::scene3d::Node3D& node) const;
    void pushSelection();
    void markSelected(zio::ui::TreeItem* item);

    EditorServices& services_;
    zio::ui::TreeView* tree_ = nullptr;
    zio::ui::LineEdit* filter_ = nullptr;
    zio::ui::Button* add_      = nullptr;
    std::string filterText_;
    std::string lastFilter_;
    std::uint64_t lastStamp_    = static_cast<std::uint64_t>(-1);
    std::uint64_t lastRevision_ = static_cast<std::uint64_t>(-1);
    std::vector<zio::scene3d::Node3D*> lastSelection_;
    bool syncing_ = false;
};

class InspectorPanel
{
public:
    explicit InspectorPanel(EditorServices& services) : services_(services) {}

    std::unique_ptr<zio::ui::Widget> build();
    void refresh();

private:
    zio::ui::NumberEdit* addVector(zio::ui::Widget* parent, const char* title, zio::ui::NumberEdit* fields[3], double step,
                                   std::function<void()> onChanged);
    void applyTransform();
    void applyRotation();

    EditorServices& services_;
    EulerCache eulers_;
    zio::ui::Label* title_      = nullptr;
    zio::ui::Label* hint_       = nullptr;
    zio::ui::Label* multi_      = nullptr;
    zio::ui::Widget* fields_    = nullptr;
    zio::ui::Widget* meshBox_   = nullptr;
    zio::ui::Widget* lightBox_  = nullptr;
    zio::ui::Button* lightType_ = nullptr;
    zio::ui::ColorButton* lightColor_ = nullptr;
    zio::ui::NumberEdit* lightIntensity_ = nullptr;
    zio::ui::NumberEdit* lightRange_     = nullptr;
    zio::ui::NumberEdit* lightInner_     = nullptr;
    zio::ui::NumberEdit* lightOuter_     = nullptr;
    zio::ui::CheckBox* lightShadow_      = nullptr;
    zio::ui::NumberEdit* lightShadowOpacity_ = nullptr;
    zio::ui::LineEdit* name_    = nullptr;
    zio::ui::NumberEdit* pos_[3]   = {};
    zio::ui::NumberEdit* rot_[3]   = {};
    zio::ui::NumberEdit* scale_[3] = {};
    zio::ui::ColorButton* color_   = nullptr;
    zio::ui::NumberEdit* uv_[2]    = {};
    zio::ui::CheckBox* shadow_     = nullptr;
    zio::ui::Widget* animBox_      = nullptr;
    zio::ui::Label* animName_      = nullptr;
    zio::ui::Button* animPlay_     = nullptr;
    zio::ui::CheckBox* animLoop_   = nullptr;
    zio::ui::NumberEdit* animSpeed_ = nullptr;
    int animIndex_ = 0;
    zio::ui::Widget* scriptBox_  = nullptr;
    zio::ui::VBox* scriptList_   = nullptr;
    std::string scriptSignature_;
    const zio::scene3d::Node3D* scriptNode_ = nullptr;
    void rebuildScripts(zio::scene3d::Node3D& node);
    std::vector<std::string> scriptCandidates(const zio::scene3d::Node3D& node) const;
    bool syncing_ = false;
};

class FilesPanel
{
public:
    explicit FilesPanel(EditorServices& services) : services_(services), alive_(std::make_shared<bool>(true)) {}
    ~FilesPanel() { *alive_ = false; }

    std::unique_ptr<zio::ui::Widget> build();
    void refresh(float dt);

private:
    void requestScan();
    void applyEntries(std::vector<AssetEntry> entries);
    void updateButtons();
    void activate(int index);
    void openEntry(const AssetEntry& entry);
    const AssetEntry* selectedEntry() const;
    void showCreate();
    void showCreateScript(zio::scripting::ScriptLanguage& language);
    void showDelete();
    void setStatus(const std::string& text, bool error);
    std::vector<zio::ui::MenuItem> menuItems();

    EditorServices& services_;
    std::shared_ptr<bool> alive_;
    std::filesystem::path boundRoot_;
    zio::ui::Widget* body_       = nullptr;
    zio::ui::Label* message_     = nullptr;
    zio::ui::Label* status_      = nullptr;
    zio::ui::IconGrid* grid_     = nullptr;
    void applyFilter();

    zio::ui::LineEdit* filter_ = nullptr;
    std::string filterText_;
    std::vector<AssetEntry> allEntries_;
    std::vector<AssetEntry> entries_;
    std::string signature_;
    std::string statusText_;
    bool statusError_ = false;
    float timer_      = 10.f;
    bool scanning_    = false;
    bool rescan_      = false;
};

class OutputPanel
{
public:
    explicit OutputPanel(EditorServices& services) : services_(services) {}

    std::unique_ptr<zio::ui::Widget> build();
    void add(const std::string& text, bool error);
    void refresh();

private:
    EditorServices& services_;
    zio::ui::ScrollContainer* scroll_ = nullptr;
    zio::ui::VBox* lines_             = nullptr;
    bool scrollPending_               = false;
};

class PerformancePanel
{
public:
    explicit PerformancePanel(EditorServices& services) : services_(services) {}

    std::unique_ptr<zio::ui::Widget> build();
    void refresh(float dt);

private:
    EditorServices& services_;
    zio::ui::Label* frame_  = nullptr;
    zio::ui::Label* memory_ = nullptr;
    zio::ui::Label* draw2d_ = nullptr;
    zio::ui::Label* jobs_   = nullptr;
    float timer_            = 10.f;
};

}
