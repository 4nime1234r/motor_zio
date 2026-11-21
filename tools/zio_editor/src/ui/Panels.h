#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/EditorContext.h"
#include "core/EulerCache.h"
#include "panels/ContentBrowserModel.h"
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
    void pushSelection();
    void markSelected(zio::ui::TreeItem* item);

    EditorServices& services_;
    zio::ui::TreeView* tree_ = nullptr;
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
    zio::ui::LineEdit* name_    = nullptr;
    zio::ui::NumberEdit* pos_[3]   = {};
    zio::ui::NumberEdit* rot_[3]   = {};
    zio::ui::NumberEdit* scale_[3] = {};
    zio::ui::ColorButton* color_   = nullptr;
    zio::ui::NumberEdit* uv_[2]    = {};
    zio::ui::CheckBox* shadow_     = nullptr;
    bool syncing_ = false;
};

class FilesPanel
{
public:
    explicit FilesPanel(EditorServices& services) : services_(services) {}

    std::unique_ptr<zio::ui::Widget> build();
    void refresh(float dt);

private:
    struct FolderInfo
    {
        std::string name;
        std::string display;
        std::string relative;
        std::vector<FolderInfo> children;
    };

    void rescanFolders();
    void scan(FolderInfo& node, int depth) const;
    void fillFolderTree(zio::ui::TreeItem* parent, const FolderInfo& info, const std::vector<std::string>& collapsed);
    void rebuildGrid();
    void rebuildCrumbs();
    void navigate(const std::string& relative);
    void activate(int index);

    EditorServices& services_;
    ContentBrowserModel model_;
    std::filesystem::path boundRoot_;
    zio::ui::Widget* body_         = nullptr;
    zio::ui::Label* message_       = nullptr;
    zio::ui::HBox* crumbs_         = nullptr;
    zio::ui::TreeView* folders_    = nullptr;
    zio::ui::IconGrid* grid_       = nullptr;
    std::vector<ContentBrowserModel::Entry> entries_;
    std::deque<std::string> pathStore_;
    std::string folderSignature_;
    std::string gridSignature_;
    std::string crumbSignature_;
    float timer_    = 10.f;
    bool syncing_   = false;
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
