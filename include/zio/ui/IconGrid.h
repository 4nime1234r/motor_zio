#pragma once

#include <functional>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

struct IconItem
{
    std::string text;
    Color color = Color::white();
    std::string badge;
    bool folder     = false;
    void* userData  = nullptr;
};

class ZIO_API IconGrid final : public Widget
{
public:
    IconGrid();

    void setItems(std::vector<IconItem> items);
    const std::vector<IconItem>& items() const { return items_; }
    void setListMode(bool list) { listMode_ = list; }
    bool listMode() const { return listMode_; }

    int current() const { return current_; }
    std::vector<int> selection() const;
    void select(int index, bool additive = false);
    void clearSelection();

    void setOnSelectionChanged(std::function<void()> callback) { onSelectionChanged_ = std::move(callback); }
    void setOnActivated(std::function<void(int)> callback) { onActivated_ = std::move(callback); }
    void setOnContextMenu(std::function<void(int, Vec2)> callback) { onContextMenu_ = std::move(callback); }

    int columns() const;
    Rect itemRect(int index) const;
    int itemAt(Vec2 position) const;

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onKeyDown(const KeyEvent& event) override;
    void onMouseLeave() override;

private:
    float cellWidth() const { return 96.f; }
    float cellHeight() const;
    float rowHeight() const;
    void drawIcon(Painter& painter, const IconItem& item, const Rect& box);
    void moveCurrent(int index, bool extend);
    void notifySelection();
    void ensureVisible(int index);

    std::vector<IconItem> items_;
    std::vector<bool> selected_;
    bool listMode_ = false;
    int current_   = -1;
    int hover_     = -1;
    std::function<void()> onSelectionChanged_;
    std::function<void(int)> onActivated_;
    std::function<void(int, Vec2)> onContextMenu_;
};

}
