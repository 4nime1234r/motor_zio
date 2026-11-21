#include "zio/ui/Tree.h"

#include <algorithm>
#include <cmath>

#include "zio/ui/Containers.h"
#include "zio/ui/Context.h"

namespace zio::ui
{

namespace
{

constexpr float kIndent   = 18.f;
constexpr float kArrow    = 16.f;
constexpr float kLeftPad  = 6.f;
constexpr float kIconSize = 16.f;

}

TreeItem* TreeItem::add(std::string itemText)
{
    auto child     = std::make_unique<TreeItem>();
    child->text    = std::move(itemText);
    child->parent_ = this;
    TreeItem* raw  = child.get();
    children_.push_back(std::move(child));
    return raw;
}

TreeView::TreeView()
{
    setFocusable(true);
    root_.expanded = true;
}

float TreeView::rowHeight() const
{
    return (std::max)(20.f, font().lineHeight() + 6.f);
}

void TreeView::collect(TreeItem* parent, int depth, std::vector<Row>& out) const
{
    for (const auto& child : parent->children())
    {
        out.push_back({child.get(), depth});
        if (child->expanded && child->hasChildren())
            collect(child.get(), depth + 1, out);
    }
}

void TreeView::rebuildRows()
{
    rows_.clear();
    collect(&root_, 0, rows_);
    if (current_ && indexOf(current_) < 0)
        current_ = nullptr;
}

int TreeView::indexOf(const TreeItem* item) const
{
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].item == item)
            return static_cast<int>(i);
    return -1;
}

Vec2 TreeView::contentMinSize() const
{
    std::vector<Row> rows;
    collect(const_cast<TreeItem*>(&root_), 0, rows);
    Font& f     = font();
    float width = 0.f;
    for (const Row& row : rows)
    {
        float w = kLeftPad + static_cast<float>(row.depth) * kIndent + kArrow;
        if (row.item->iconColor || row.item->icon)
            w += kIconSize + 6.f;
        w += f.textWidth(row.item->text) + 12.f;
        width = (std::max)(width, w);
    }
    return {width, static_cast<float>(rows.size()) * rowHeight()};
}

void TreeView::layout()
{
    rebuildRows();
}

Rect TreeView::rowRect(std::size_t index) const
{
    return {rect().x, rect().y + static_cast<float>(index) * rowHeight(), rect().w, rowHeight()};
}

Rect TreeView::arrowRect(std::size_t index) const
{
    const Rect r = rowRect(index);
    return {r.x + kLeftPad + static_cast<float>(rows_[index].depth) * kIndent, r.y, kArrow, r.h};
}

int TreeView::rowAt(float y) const
{
    const float local = y - rect().y;
    if (local < 0.f)
        return -1;
    const int index = static_cast<int>(local / rowHeight());
    return index < static_cast<int>(rows_.size()) ? index : -1;
}

std::vector<TreeItem*> TreeView::selection() const
{
    std::vector<TreeItem*> result;
    for (const Row& row : rows_)
        if (row.item->selected)
            result.push_back(row.item);
    return result;
}

void TreeView::notifySelection()
{
    if (onSelectionChanged_)
        onSelectionChanged_();
}

void TreeView::clearSelection()
{
    for (const Row& row : rows_)
        row.item->selected = false;
    notifySelection();
}

void TreeView::select(TreeItem* item, bool additive)
{
    if (!additive)
        for (const Row& row : rows_)
            row.item->selected = false;
    if (item)
    {
        item->selected = additive ? !item->selected : true;
        current_       = item;
    }
    notifySelection();
}

void TreeView::selectRange(int from, int to)
{
    if (from < 0 || to < 0)
        return;
    const int lo = (std::min)(from, to);
    const int hi = (std::max)(from, to);
    for (const Row& row : rows_)
        row.item->selected = false;
    for (int i = lo; i <= hi; ++i)
        rows_[static_cast<std::size_t>(i)].item->selected = true;
    notifySelection();
}

void TreeView::draw(Painter& painter)
{
    const Theme& t = theme();
    Font& f        = font();
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        const Row& row = rows_[i];
        const Rect r   = rowRect(i);

        if (row.item->selected)
            painter.fillRect(r, focused() ? t.selection : rgb(0xffffff, 0.10f));
        else if (static_cast<int>(i) == hover_)
            painter.fillRect(r, rgb(0xffffff, 0.05f));

        if (focused() && row.item == current_)
            painter.fillRect({r.x, r.y, 2.f, r.h}, t.accent);

        float x = r.x + kLeftPad + static_cast<float>(row.depth) * kIndent;
        if (row.item->hasChildren())
        {
            const float cx = x + kArrow * 0.5f;
            const float cy = r.y + r.h * 0.5f;
            if (row.item->expanded)
                painter.triangle({cx - 4.f, cy - 2.f}, {cx + 4.f, cy - 2.f}, {cx, cy + 3.f}, t.textDim);
            else
                painter.triangle({cx - 2.f, cy - 4.f}, {cx + 3.f, cy}, {cx - 2.f, cy + 4.f}, t.textDim);
        }
        x += kArrow;

        if (row.item->icon)
        {
            painter.image(row.item->icon->texture(), {x, r.y + (r.h - kIconSize) * 0.5f, kIconSize, kIconSize},
                          row.item->iconColor.value_or(t.text));
            x += kIconSize + 6.f;
        }
        else if (row.item->iconColor)
        {
            painter.fillRoundRect({x, r.y + (r.h - kIconSize) * 0.5f, kIconSize, kIconSize}, *row.item->iconColor, 3.f);
            x += kIconSize + 6.f;
        }

        painter.text(f, {std::floor(x), std::floor(r.y + (r.h - f.lineHeight()) * 0.5f)}, row.item->text, t.text);
    }
}

void TreeView::onMouseLeave()
{
    hover_ = -1;
}

bool TreeView::onMouseMove(Vec2 position)
{
    hover_ = rowAt(position.y);
    return false;
}

void TreeView::ensureVisible(int index)
{
    if (index < 0)
        return;
    for (Widget* w = parent(); w; w = w->parent())
    {
        if (auto* scroll = dynamic_cast<ScrollContainer*>(w))
        {
            scroll->ensureVisible(rowRect(static_cast<std::size_t>(index)));
            return;
        }
    }
}

void TreeView::moveCurrent(int index, bool extend)
{
    if (rows_.empty())
        return;
    index          = std::clamp(index, 0, static_cast<int>(rows_.size()) - 1);
    const int from = indexOf(current_);
    if (extend && from >= 0)
    {
        selectRange(from, index);
        current_ = rows_[static_cast<std::size_t>(index)].item;
        current_->selected = true;
    }
    else
    {
        select(rows_[static_cast<std::size_t>(index)].item, false);
    }
    ensureVisible(index);
}

bool TreeView::onMouseDown(const MouseEvent& event)
{
    const int index = rowAt(event.position.y);

    if (event.button == events::MouseButton::Right)
    {
        TreeItem* item = index >= 0 ? rows_[static_cast<std::size_t>(index)].item : nullptr;
        if (item && !item->selected)
            select(item, false);
        else if (!item)
            clearSelection();
        if (onContextMenu_)
            onContextMenu_(item, event.position);
        return true;
    }
    if (event.button != events::MouseButton::Left)
        return false;

    if (index < 0)
    {
        clearSelection();
        return true;
    }

    TreeItem* item = rows_[static_cast<std::size_t>(index)].item;
    if (item->hasChildren() && arrowRect(static_cast<std::size_t>(index)).contains(event.position))
    {
        item->expanded = !item->expanded;
        rebuildRows();
        return true;
    }

    if (event.clicks >= 2)
    {
        if (item->hasChildren())
        {
            item->expanded = !item->expanded;
            rebuildRows();
        }
        if (onActivated_)
            onActivated_(item);
        return true;
    }

    if (event.modifiers.ctrl)
    {
        select(item, true);
    }
    else if (event.modifiers.shift && current_)
    {
        const int from = indexOf(current_);
        selectRange(from, index);
    }
    else
    {
        select(item, false);
    }
    return true;
}

bool TreeView::onKeyDown(const KeyEvent& event)
{
    using events::KeyCode;
    if (rows_.empty())
        return false;
    const int index = indexOf(current_);

    switch (event.code)
    {
    case KeyCode::Down:
        moveCurrent(index < 0 ? 0 : index + 1, event.modifiers.shift);
        return true;
    case KeyCode::Up:
        moveCurrent(index < 0 ? 0 : index - 1, event.modifiers.shift);
        return true;
    case KeyCode::Home:
        moveCurrent(0, event.modifiers.shift);
        return true;
    case KeyCode::End:
        moveCurrent(static_cast<int>(rows_.size()) - 1, event.modifiers.shift);
        return true;
    case KeyCode::Right:
        if (current_ && current_->hasChildren())
        {
            if (!current_->expanded)
            {
                current_->expanded = true;
                rebuildRows();
            }
            else
            {
                moveCurrent(index + 1, false);
            }
        }
        return true;
    case KeyCode::Left:
        if (current_)
        {
            if (current_->hasChildren() && current_->expanded)
            {
                current_->expanded = false;
                rebuildRows();
            }
            else if (current_->parent() && current_->parent() != &root_)
            {
                const int parentIndex = indexOf(current_->parent());
                if (parentIndex >= 0)
                    moveCurrent(parentIndex, false);
            }
        }
        return true;
    case KeyCode::Enter:
        if (current_ && onActivated_)
            onActivated_(current_);
        return true;
    default:
        return false;
    }
}

}
