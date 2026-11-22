#pragma once

#include <functional>
#include "zio/ui/Icon.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

class ZIO_API TreeItem
{
public:
    TreeItem()                           = default;
    TreeItem(const TreeItem&)            = delete;
    TreeItem& operator=(const TreeItem&) = delete;

    std::string text;
    std::optional<Color> iconColor;
    std::shared_ptr<Icon> icon;
    bool expanded  = true;
    bool selected  = false;
    void* userData = nullptr;

    TreeItem* parent() const { return parent_; }
    const std::vector<std::unique_ptr<TreeItem>>& children() const { return children_; }
    bool hasChildren() const { return !children_.empty(); }

    TreeItem* add(std::string itemText);
    void clear() { children_.clear(); }

private:
    TreeItem* parent_ = nullptr;
    std::vector<std::unique_ptr<TreeItem>> children_;
};

class ZIO_API TreeView final : public Widget
{
public:
    TreeView();

    TreeItem* root() { return &root_; }
    TreeItem* current() const { return current_; }
    std::vector<TreeItem*> selection() const;
    void select(TreeItem* item, bool additive = false);
    void clearSelection();

    void setOnSelectionChanged(std::function<void()> callback) { onSelectionChanged_ = std::move(callback); }
    void setOnActivated(std::function<void(TreeItem*)> callback) { onActivated_ = std::move(callback); }
    void setOnContextMenu(std::function<void(TreeItem*, Vec2)> callback) { onContextMenu_ = std::move(callback); }

    float rowHeight() const;
    std::size_t visibleRowCount() const { return rows_.size(); }
    TreeItem* rowItem(std::size_t index) const { return index < rows_.size() ? rows_[index].item : nullptr; }
    Rect rowRect(std::size_t index) const;
    Rect arrowRect(std::size_t index) const;
    void rebuildRows();

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onKeyDown(const KeyEvent& event) override;
    void onMouseLeave() override;

private:
    struct Row
    {
        TreeItem* item = nullptr;
        int depth      = 0;
    };

    void collect(TreeItem* parent, int depth, std::vector<Row>& out) const;
    int rowAt(float y) const;
    int indexOf(const TreeItem* item) const;
    void selectRange(int from, int to);
    void notifySelection();
    void moveCurrent(int index, bool extend);
    void ensureVisible(int index);

    TreeItem root_;
    std::vector<Row> rows_;
    TreeItem* current_ = nullptr;
    int hover_         = -1;
    std::function<void()> onSelectionChanged_;
    std::function<void(TreeItem*)> onActivated_;
    std::function<void(TreeItem*, Vec2)> onContextMenu_;
};

}
