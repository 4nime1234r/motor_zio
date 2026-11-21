#include "zio/ui/IconGrid.h"

#include <algorithm>
#include <cmath>

#include "zio/ui/Containers.h"
#include "zio/ui/Context.h"

namespace zio::ui
{

namespace
{

constexpr float kIconSize     = 52.f;
constexpr float kListIconSize = 20.f;

std::string fit(Font& font, const std::string& text, float width)
{
    if (font.textWidth(text) <= width)
        return text;
    std::string cut = text;
    while (cut.size() > 1 && font.textWidth(cut + "...") > width)
        cut.pop_back();
    return cut + "...";
}

}

IconGrid::IconGrid()
{
    setFocusable(true);
}

void IconGrid::setItems(std::vector<IconItem> items)
{
    items_ = std::move(items);
    selected_.assign(items_.size(), false);
    current_ = -1;
    hover_   = -1;
}

float IconGrid::cellHeight() const
{
    return kIconSize + font().lineHeight() + 22.f;
}

float IconGrid::rowHeight() const
{
    return (std::max)(kListIconSize + 6.f, font().lineHeight() + 8.f);
}

int IconGrid::columns() const
{
    if (listMode_)
        return 1;
    return (std::max)(1, static_cast<int>(rect().w / cellWidth()));
}

Rect IconGrid::itemRect(int index) const
{
    if (listMode_)
        return {rect().x, rect().y + static_cast<float>(index) * rowHeight(), rect().w, rowHeight()};
    const int cols = columns();
    const int row  = index / cols;
    const int col  = index % cols;
    return {rect().x + static_cast<float>(col) * cellWidth(), rect().y + static_cast<float>(row) * cellHeight(),
            cellWidth(), cellHeight()};
}

int IconGrid::itemAt(Vec2 position) const
{
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (itemRect(static_cast<int>(i)).contains(position))
            return static_cast<int>(i);
    return -1;
}

Vec2 IconGrid::contentMinSize() const
{
    if (listMode_)
        return {120.f, static_cast<float>(items_.size()) * rowHeight()};
    const int cols = (std::max)(1, static_cast<int>(rect().w / cellWidth()));
    const int rows = (static_cast<int>(items_.size()) + cols - 1) / cols;
    return {cellWidth(), static_cast<float>(rows) * cellHeight()};
}

std::vector<int> IconGrid::selection() const
{
    std::vector<int> result;
    for (std::size_t i = 0; i < selected_.size(); ++i)
        if (selected_[i])
            result.push_back(static_cast<int>(i));
    return result;
}

void IconGrid::notifySelection()
{
    if (onSelectionChanged_)
        onSelectionChanged_();
}

void IconGrid::clearSelection()
{
    std::fill(selected_.begin(), selected_.end(), false);
    notifySelection();
}

void IconGrid::select(int index, bool additive)
{
    if (!additive)
        std::fill(selected_.begin(), selected_.end(), false);
    if (index >= 0 && index < static_cast<int>(items_.size()))
    {
        selected_[static_cast<std::size_t>(index)] = additive ? !selected_[static_cast<std::size_t>(index)] : true;
        current_                                   = index;
    }
    notifySelection();
}

void IconGrid::drawIcon(Painter& painter, const IconItem& item, const Rect& box)
{
    const Color base = item.color;
    if (item.folder)
    {
        const float tab = box.w * 0.42f;
        painter.fillRoundRect({box.x, box.y + box.h * 0.12f, tab, box.h * 0.3f}, base, box.w > 30.f ? 4.f : 2.f);
        painter.fillRoundRect({box.x, box.y + box.h * 0.26f, box.w, box.h * 0.66f}, base, box.w > 30.f ? 5.f : 2.f);
        return;
    }

    const float inset = box.w * 0.14f;
    const Rect page   = {box.x + inset, box.y, box.w - 2.f * inset, box.h};
    const Color panel = theme().panel;
    const Color fill  = {panel.r + (base.r - panel.r) * 0.14f, panel.g + (base.g - panel.g) * 0.14f,
                         panel.b + (base.b - panel.b) * 0.14f, 1.f};
    painter.roundRectWithBorder(page, fill, base, box.w > 30.f ? 6.f : 2.f, box.w > 30.f ? 1.6f : 1.2f);
    const float fold = box.w * 0.24f;
    painter.triangle({page.right() - fold, page.y}, {page.right(), page.y + fold}, {page.right() - fold, page.y + fold}, base);

    if (!item.badge.empty() && box.w > 30.f)
    {
        Font& f       = font();
        const float x = page.x + (page.w - f.textWidth(item.badge)) * 0.5f;
        const float y = box.y + box.h * 0.56f - f.lineHeight() * 0.5f;
        painter.text(f, {std::floor(x), std::floor(y)}, item.badge, base);
    }
}

void IconGrid::draw(Painter& painter)
{
    const Theme& t = theme();
    Font& f        = font();

    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const int index = static_cast<int>(i);
        const Rect r    = itemRect(index);
        const IconItem& item = items_[i];

        if (selected_[i])
            painter.fillRoundRect(r.shrink(2.f), focused() ? t.selection : rgb(0xffffff, 0.10f), 6.f);
        else if (index == hover_)
            painter.fillRoundRect(r.shrink(2.f), rgb(0xffffff, 0.05f), 6.f);
        if (focused() && index == current_)
        {
            const Rect ring = r.shrink(2.f);
            painter.fillRect({ring.x, ring.y, ring.w, 1.f}, t.accent);
            painter.fillRect({ring.x, ring.bottom() - 1.f, ring.w, 1.f}, t.accent);
            painter.fillRect({ring.x, ring.y, 1.f, ring.h}, t.accent);
            painter.fillRect({ring.right() - 1.f, ring.y, 1.f, ring.h}, t.accent);
        }

        if (listMode_)
        {
            drawIcon(painter, item, {r.x + 8.f, r.y + (r.h - kListIconSize) * 0.5f, kListIconSize, kListIconSize});
            painter.text(f, {std::floor(r.x + 8.f + kListIconSize + 8.f), std::floor(r.y + (r.h - f.lineHeight()) * 0.5f)},
                         item.text, t.text);
        }
        else
        {
            drawIcon(painter, item, {r.x + (r.w - kIconSize) * 0.5f, r.y + 6.f, kIconSize, kIconSize});
            const std::string label = fit(f, item.text, r.w - 12.f);
            painter.text(f, {std::floor(r.x + (r.w - f.textWidth(label)) * 0.5f), std::floor(r.y + kIconSize + 12.f)}, label,
                         t.text);
        }
    }
}

void IconGrid::onMouseLeave()
{
    hover_ = -1;
}

bool IconGrid::onMouseMove(Vec2 position)
{
    hover_ = itemAt(position);
    return false;
}

void IconGrid::ensureVisible(int index)
{
    if (index < 0)
        return;
    for (Widget* w = parent(); w; w = w->parent())
    {
        if (auto* scroll = dynamic_cast<ScrollContainer*>(w))
        {
            scroll->ensureVisible(itemRect(index));
            return;
        }
    }
}

void IconGrid::moveCurrent(int index, bool extend)
{
    if (items_.empty())
        return;
    index = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    if (extend && current_ >= 0)
    {
        const int lo = (std::min)(current_, index);
        const int hi = (std::max)(current_, index);
        std::fill(selected_.begin(), selected_.end(), false);
        for (int i = lo; i <= hi; ++i)
            selected_[static_cast<std::size_t>(i)] = true;
        notifySelection();
    }
    else
    {
        select(index, false);
    }
    current_ = index;
    ensureVisible(index);
}

bool IconGrid::onMouseDown(const MouseEvent& event)
{
    const int index = itemAt(event.position);

    if (event.button == events::MouseButton::Right)
    {
        if (index >= 0 && !selected_[static_cast<std::size_t>(index)])
            select(index, false);
        else if (index < 0)
            clearSelection();
        if (onContextMenu_)
            onContextMenu_(index, event.position);
        return true;
    }
    if (event.button != events::MouseButton::Left)
        return false;

    if (index < 0)
    {
        clearSelection();
        return true;
    }
    if (event.clicks >= 2)
    {
        if (onActivated_)
            onActivated_(index);
        return true;
    }
    if (event.modifiers.ctrl)
        select(index, true);
    else if (event.modifiers.shift && current_ >= 0)
        moveCurrent(index, true);
    else
        select(index, false);
    return true;
}

bool IconGrid::onKeyDown(const KeyEvent& event)
{
    using events::KeyCode;
    if (items_.empty())
        return false;
    const int cols  = columns();
    const int index = current_ < 0 ? 0 : current_;
    switch (event.code)
    {
    case KeyCode::Left:
        moveCurrent(current_ < 0 ? 0 : index - 1, event.modifiers.shift);
        return true;
    case KeyCode::Right:
        moveCurrent(current_ < 0 ? 0 : index + 1, event.modifiers.shift);
        return true;
    case KeyCode::Up:
        moveCurrent(current_ < 0 ? 0 : index - cols, event.modifiers.shift);
        return true;
    case KeyCode::Down:
        moveCurrent(current_ < 0 ? 0 : index + cols, event.modifiers.shift);
        return true;
    case KeyCode::Home:
        moveCurrent(0, event.modifiers.shift);
        return true;
    case KeyCode::End:
        moveCurrent(static_cast<int>(items_.size()) - 1, event.modifiers.shift);
        return true;
    case KeyCode::Enter:
        if (current_ >= 0 && onActivated_)
            onActivated_(current_);
        return true;
    default:
        return false;
    }
}

}
