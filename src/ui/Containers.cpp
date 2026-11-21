#include "zio/ui/Containers.h"

#include <algorithm>
#include <cmath>

#include "zio/ui/Context.h"

namespace zio::ui
{

float BoxContainer::separation() const
{
    return separation_ >= 0.f ? separation_ : theme().spacing;
}

Vec2 BoxContainer::contentMinSize() const
{
    float main  = 0.f;
    float cross = 0.f;
    int count   = 0;
    for (const auto& child : children())
    {
        if (!child->visible())
            continue;
        const Vec2 m = child->minSize();
        main += vertical_ ? m.y : m.x;
        cross = (std::max)(cross, vertical_ ? m.x : m.y);
        ++count;
    }
    if (count > 1)
        main += separation() * static_cast<float>(count - 1);

    Vec2 size = vertical_ ? Vec2{cross, main} : Vec2{main, cross};
    size.x += padding_.horizontal();
    size.y += padding_.vertical();
    return size;
}

void BoxContainer::layout()
{
    const Rect inner = rect().shrink(padding_.left, padding_.top, padding_.right, padding_.bottom);

    std::vector<Widget*> items;
    for (const auto& child : children())
        if (child->visible())
            items.push_back(child.get());
    if (items.empty())
        return;

    const float sep   = separation();
    const float avail = vertical_ ? inner.h : inner.w;
    float used        = sep * static_cast<float>(items.size() - 1);
    float stretchSum  = 0.f;
    for (Widget* item : items)
    {
        const Vec2 m = item->minSize();
        used += vertical_ ? m.y : m.x;
        if (vertical_ ? item->expandV() : item->expandH())
            stretchSum += item->stretch();
    }
    const float extra = (std::max)(0.f, avail - used);

    float position = vertical_ ? inner.y : inner.x;
    for (Widget* item : items)
    {
        const Vec2 m = item->minSize();
        float main   = vertical_ ? m.y : m.x;
        if ((vertical_ ? item->expandV() : item->expandH()) && stretchSum > 0.f)
            main += extra * item->stretch() / stretchSum;

        const Rect r = vertical_ ? Rect{inner.x, position, inner.w, main} : Rect{position, inner.y, main, inner.h};
        item->setRect(r);
        position += main + sep;
    }
}

Vec2 Panel::contentMinSize() const
{
    Vec2 size = {0.f, 0.f};
    for (const auto& child : children())
    {
        if (!child->visible())
            continue;
        const Vec2 m = child->minSize();
        size.x       = (std::max)(size.x, m.x);
        size.y       = (std::max)(size.y, m.y);
    }
    size.x += padding_.horizontal();
    size.y += padding_.vertical();
    return size;
}

void Panel::layout()
{
    const Rect inner = rect().shrink(padding_.left, padding_.top, padding_.right, padding_.bottom);
    for (const auto& child : children())
        if (child->visible())
            child->setRect(inner);
}

void Panel::draw(Painter& painter)
{
    if (transparent_)
        return;
    const Color fill = background_ ? *background_ : theme().panel;
    if (border_)
        painter.roundRectWithBorder(rect(), fill, *border_, radius_, borderThickness_);
    else if (radius_ > 0.f)
        painter.fillRoundRect(rect(), fill, radius_);
    else
        painter.fillRect(rect(), fill);
}

ScrollContainer::ScrollContainer(bool vertical, bool horizontal) : vertical_(vertical), horizontal_(horizontal)
{
    setClipChildren(true);
}

void ScrollContainer::setScroll(Vec2 scroll)
{
    scroll_ = scroll;
    clampScroll();
}

void ScrollContainer::ensureVisible(const Rect& target)
{
    if (target.y < viewport_.y)
        scroll_.y -= viewport_.y - target.y;
    else if (target.bottom() > viewport_.bottom())
        scroll_.y += target.bottom() - viewport_.bottom();
    if (target.x < viewport_.x)
        scroll_.x -= viewport_.x - target.x;
    else if (target.right() > viewport_.right())
        scroll_.x += target.right() - viewport_.right();
    clampScroll();
}

void ScrollContainer::clampScroll()
{
    const float maxX = (std::max)(0.f, contentSize_.x - viewport_.w);
    const float maxY = (std::max)(0.f, contentSize_.y - viewport_.h);
    scroll_.x        = std::clamp(scroll_.x, 0.f, maxX);
    scroll_.y        = std::clamp(scroll_.y, 0.f, maxY);
}

void ScrollContainer::layout()
{
    Widget* child = children().empty() ? nullptr : children().front().get();
    const Vec2 cm = child ? child->minSize() : Vec2{0.f, 0.f};
    const float bar = theme().scrollbarWidth;

    showV_ = false;
    showH_ = false;
    for (int i = 0; i < 2; ++i)
    {
        const float viewW = rect().w - (showV_ ? bar : 0.f);
        const float viewH = rect().h - (showH_ ? bar : 0.f);
        showV_            = vertical_ && cm.y > viewH;
        showH_            = horizontal_ && cm.x > viewW;
    }

    viewport_ = {rect().x, rect().y, rect().w - (showV_ ? bar : 0.f), rect().h - (showH_ ? bar : 0.f)};
    contentSize_ = {horizontal_ ? (std::max)(cm.x, viewport_.w) : viewport_.w,
                    vertical_ ? (std::max)(cm.y, viewport_.h) : viewport_.h};
    clampScroll();

    if (child)
        child->setRect({viewport_.x - scroll_.x, viewport_.y - scroll_.y, contentSize_.x, contentSize_.y});
}

Rect ScrollContainer::verticalTrack() const
{
    return {rect().right() - theme().scrollbarWidth, rect().y, theme().scrollbarWidth, viewport_.h};
}

Rect ScrollContainer::horizontalTrack() const
{
    return {rect().x, rect().bottom() - theme().scrollbarWidth, viewport_.w, theme().scrollbarWidth};
}

Rect ScrollContainer::verticalThumb() const
{
    const Rect track  = verticalTrack();
    const float range = contentSize_.y - viewport_.h;
    const float length = (std::max)(24.f, track.h * viewport_.h / (std::max)(1.f, contentSize_.y));
    const float travel = (std::max)(0.f, track.h - length);
    const float offset = range > 0.f ? travel * scroll_.y / range : 0.f;
    return {track.x + 2.f, track.y + offset, track.w - 4.f, length};
}

Rect ScrollContainer::horizontalThumb() const
{
    const Rect track  = horizontalTrack();
    const float range = contentSize_.x - viewport_.w;
    const float length = (std::max)(24.f, track.w * viewport_.w / (std::max)(1.f, contentSize_.x));
    const float travel = (std::max)(0.f, track.w - length);
    const float offset = range > 0.f ? travel * scroll_.x / range : 0.f;
    return {track.x + offset, track.y + 2.f, length, track.h - 4.f};
}

void ScrollContainer::drawForeground(Painter& painter)
{
    const Color idle = rgb(0xffffff, 0.22f);
    const Color hot  = rgb(0xffffff, 0.38f);
    if (showV_)
    {
        const Rect thumb = verticalThumb();
        painter.fillRoundRect(thumb, dragging_ == 1 ? theme().accentHover : (hovered() ? hot : idle), thumb.w * 0.5f);
    }
    if (showH_)
    {
        const Rect thumb = horizontalThumb();
        painter.fillRoundRect(thumb, dragging_ == 2 ? theme().accentHover : (hovered() ? hot : idle), thumb.h * 0.5f);
    }
}

bool ScrollContainer::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;

    if (showV_ && verticalTrack().contains(event.position))
    {
        const Rect thumb = verticalThumb();
        if (thumb.contains(event.position))
        {
            dragging_   = 1;
            dragOffset_ = event.position.y - thumb.y;
        }
        else
        {
            scroll_.y += event.position.y < thumb.y ? -viewport_.h : viewport_.h;
            clampScroll();
        }
        return true;
    }
    if (showH_ && horizontalTrack().contains(event.position))
    {
        const Rect thumb = horizontalThumb();
        if (thumb.contains(event.position))
        {
            dragging_   = 2;
            dragOffset_ = event.position.x - thumb.x;
        }
        else
        {
            scroll_.x += event.position.x < thumb.x ? -viewport_.w : viewport_.w;
            clampScroll();
        }
        return true;
    }
    return false;
}

bool ScrollContainer::onMouseUp(const MouseEvent& event)
{
    (void)event;
    const bool was = dragging_ != 0;
    dragging_      = 0;
    return was;
}

bool ScrollContainer::onMouseMove(Vec2 position)
{
    if (dragging_ == 1)
    {
        const Rect track   = verticalTrack();
        const Rect thumb   = verticalThumb();
        const float travel = (std::max)(1.f, track.h - thumb.h);
        const float ratio  = (position.y - dragOffset_ - track.y) / travel;
        scroll_.y          = ratio * (contentSize_.y - viewport_.h);
        clampScroll();
        return true;
    }
    if (dragging_ == 2)
    {
        const Rect track   = horizontalTrack();
        const Rect thumb   = horizontalThumb();
        const float travel = (std::max)(1.f, track.w - thumb.w);
        const float ratio  = (position.x - dragOffset_ - track.x) / travel;
        scroll_.x          = ratio * (contentSize_.x - viewport_.w);
        clampScroll();
        return true;
    }
    return false;
}

bool ScrollContainer::onMouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers)
{
    (void)position;
    constexpr float kStep = 54.f;
    bool handled          = false;
    if (showV_ && !modifiers.shift)
    {
        scroll_.y -= delta.y * kStep;
        handled = true;
    }
    else if (showH_)
    {
        scroll_.x -= (modifiers.shift ? delta.y : delta.x) * kStep;
        handled = true;
    }
    clampScroll();
    return handled;
}

Vec2 SplitContainer::contentMinSize() const
{
    if (children().size() < 2)
        return {0.f, 0.f};
    const Vec2 a      = children()[0]->minSize();
    const Vec2 b      = children()[1]->minSize();
    const float divider = theme().splitterSize;
    return vertical_ ? Vec2{(std::max)(a.x, b.x), a.y + b.y + divider} : Vec2{a.x + b.x + divider, (std::max)(a.y, b.y)};
}

void SplitContainer::layout()
{
    if (children().size() < 2)
        return;
    Widget* a = children()[0].get();
    Widget* b = children()[1].get();

    const float divider = theme().splitterSize;
    const float total   = vertical_ ? rect().h : rect().w;
    const float avail   = (std::max)(0.f, total - divider);
    const float aMin    = vertical_ ? a->minSize().y : a->minSize().x;
    const float bMin    = vertical_ ? b->minSize().y : b->minSize().x;

    float first = avail * ratio_;
    if (avail < aMin + bMin)
        first = (aMin + bMin) > 0.f ? avail * aMin / (aMin + bMin) : avail * 0.5f;
    else
        first = std::clamp(first, aMin, avail - bMin);
    firstSize_ = first;

    if (vertical_)
    {
        a->setRect({rect().x, rect().y, rect().w, first});
        b->setRect({rect().x, rect().y + first + divider, rect().w, avail - first});
    }
    else
    {
        a->setRect({rect().x, rect().y, first, rect().h});
        b->setRect({rect().x + first + divider, rect().y, avail - first, rect().h});
    }
}

Rect SplitContainer::dividerRect() const
{
    const float divider = theme().splitterSize;
    return vertical_ ? Rect{rect().x, rect().y + firstSize_, rect().w, divider}
                     : Rect{rect().x + firstSize_, rect().y, divider, rect().h};
}

void SplitContainer::draw(Painter& painter)
{
    const Rect d = dividerRect();
    const bool hot = dragging_ || (hovered() && d.contains(lastPosition_));
    painter.fillRect(d, theme().page);
    const Color line = hot ? theme().accent : theme().borderStrong;
    if (vertical_)
        painter.fillRect({d.x, d.y + d.h * 0.5f - 0.5f, d.w, 1.f}, line);
    else
        painter.fillRect({d.x + d.w * 0.5f - 0.5f, d.y, 1.f, d.h}, line);
}

bool SplitContainer::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    if (!dividerRect().contains(event.position))
        return false;
    dragging_ = true;
    return true;
}

bool SplitContainer::onMouseUp(const MouseEvent& event)
{
    (void)event;
    const bool was = dragging_;
    dragging_      = false;
    return was;
}

bool SplitContainer::onMouseMove(Vec2 position)
{
    lastPosition_ = position;
    if (!dragging_)
        return false;
    const float divider = theme().splitterSize;
    const float total   = vertical_ ? rect().h : rect().w;
    const float avail   = (std::max)(1.f, total - divider);
    const float local   = (vertical_ ? position.y - rect().y : position.x - rect().x) - divider * 0.5f;
    ratio_              = std::clamp(local / avail, 0.f, 1.f);
    return true;
}

Cursor SplitContainer::cursorAt(Vec2 position) const
{
    if (dragging_ || dividerRect().contains(position))
        return vertical_ ? Cursor::ResizeVertical : Cursor::ResizeHorizontal;
    return Cursor::Arrow;
}

Widget* TabContainer::addPage(std::unique_ptr<Widget> page, std::string title)
{
    titles_.push_back(std::move(title));
    Widget* pointer = addChild(std::move(page));
    pointer->setVisible(children().size() - 1 == current_);
    return pointer;
}

void TabContainer::setCurrent(std::size_t index)
{
    if (children().empty())
        return;
    index = (std::min)(index, children().size() - 1);
    for (std::size_t i = 0; i < children().size(); ++i)
        children()[i]->setVisible(i == index);
    if (index == current_)
        return;
    current_ = index;
    if (onChanged_)
        onChanged_(index);
}

float TabContainer::headerHeight() const
{
    return theme().tabHeight;
}

Vec2 TabContainer::contentMinSize() const
{
    Vec2 size = {0.f, 0.f};
    for (const auto& child : children())
    {
        const Vec2 m = child->minSize();
        size.x       = (std::max)(size.x, m.x);
        size.y       = (std::max)(size.y, m.y);
    }
    size.y += headerHeight();
    return size;
}

void TabContainer::layout()
{
    const Rect page = {rect().x, rect().y + headerHeight(), rect().w, (std::max)(0.f, rect().h - headerHeight())};
    for (const auto& child : children())
        child->setRect(page);
}

Rect TabContainer::tabRect(std::size_t index) const
{
    float x = rect().x + 6.f;
    for (std::size_t i = 0; i <= index && i < titles_.size(); ++i)
    {
        const float width = font().textWidth(titles_[i]) + theme().padding * 3.f;
        if (i == index)
            return {x, rect().y + 4.f, width, headerHeight() - 4.f};
        x += width + 2.f;
    }
    return {};
}

void TabContainer::draw(Painter& painter)
{
    const Rect header = {rect().x, rect().y, rect().w, headerHeight()};
    painter.fillRect(header, theme().deep);
    painter.fillRect({rect().x, header.bottom() - 1.f, rect().w, 1.f}, theme().border);

    for (std::size_t i = 0; i < titles_.size(); ++i)
    {
        const Rect tab = tabRect(i);
        const bool selected = i == current_;
        if (selected)
        {
            painter.fillRoundRect(tab, theme().panel, 6.f);
            painter.fillRect({tab.x, tab.bottom() - 6.f, tab.w, 6.f}, theme().panel);
            painter.fillRect({tab.x + 6.f, tab.y, tab.w - 12.f, 2.f}, theme().accent);
        }
        Font& f = font();
        const float x = tab.x + (tab.w - f.textWidth(titles_[i])) * 0.5f;
        const float y = tab.y + (tab.h - f.lineHeight()) * 0.5f;
        painter.text(f, {std::floor(x), std::floor(y)}, titles_[i], selected ? theme().text : theme().textDim);
    }
}

bool TabContainer::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    for (std::size_t i = 0; i < titles_.size(); ++i)
    {
        if (tabRect(i).contains(event.position))
        {
            setCurrent(i);
            return true;
        }
    }
    return false;
}

}
