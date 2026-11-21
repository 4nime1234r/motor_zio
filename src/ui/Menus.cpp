#include "zio/ui/Menus.h"

#include <algorithm>
#include <cmath>

#include "zio/ui/Context.h"

namespace zio::ui
{

namespace
{

constexpr float kCheckColumn = 28.f;
constexpr float kArrowColumn = 22.f;

}

PopupMenu::PopupMenu(std::vector<MenuItem> items) : items_(std::move(items)) {}

float PopupMenu::itemHeight() const
{
    return font().lineHeight() + 10.f;
}

float PopupMenu::separatorHeight() const
{
    return 9.f;
}

Rect PopupMenu::itemRect(std::size_t index) const
{
    float y = rect().y + 4.f;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const float h = items_[i].separator ? separatorHeight() : itemHeight();
        if (i == index)
            return {rect().x + 4.f, y, rect().w - 8.f, h};
        y += h;
    }
    return {};
}

Vec2 PopupMenu::contentMinSize() const
{
    Font& f       = font();
    float width   = 0.f;
    float height  = 8.f;
    for (const MenuItem& item : items_)
    {
        if (item.separator)
        {
            height += separatorHeight();
            continue;
        }
        float w = kCheckColumn + f.textWidth(item.text) + kArrowColumn;
        if (!item.shortcut.empty())
            w += 24.f + f.textWidth(item.shortcut);
        width = (std::max)(width, w);
        height += itemHeight();
    }
    return {(std::max)(width, 120.f), height};
}

void PopupMenu::layout()
{
    Vec2 size = minSize();
    size.x    = (std::max)(size.x, minWidth_);
    Vec2 pos  = anchor_;

    if (context())
    {
        const Vec2 window = context()->size();
        if (pos.x + size.x > window.x)
            pos.x = flipX_ >= 0.f ? flipX_ - size.x : window.x - size.x;
        if (pos.y + size.y > window.y)
            pos.y = window.y - size.y;
    }
    pos.x = (std::max)(0.f, pos.x);
    pos.y = (std::max)(0.f, pos.y);
    setRect({pos.x, pos.y, size.x, size.y});
}

int PopupMenu::itemAt(Vec2 position) const
{
    if (!rect().contains(position))
        return -1;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const Rect r = itemRect(i);
        if (position.y >= r.y && position.y < r.bottom())
            return items_[i].separator ? -1 : static_cast<int>(i);
    }
    return -1;
}

void PopupMenu::draw(Painter& painter)
{
    const Theme& t = theme();
    painter.roundRectWithBorder(rect(), t.raised, t.borderStrong, t.radius + 2.f, 1.f);

    Font& f = font();
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const MenuItem& item = items_[i];
        const Rect r         = itemRect(i);

        if (item.separator)
        {
            painter.fillRect({r.x + 6.f, std::floor(r.y + r.h * 0.5f), r.w - 12.f, 1.f}, t.borderStrong);
            continue;
        }

        const bool hot = static_cast<int>(i) == hover_ && item.enabled;
        if (hot)
            painter.fillRoundRect(r, t.accentDim, 4.f);

        Color textColor = item.enabled ? t.text : Color{t.text.r, t.text.g, t.text.b, 0.4f};
        const float y   = std::floor(r.y + (r.h - f.lineHeight()) * 0.5f);

        if (item.checkable && item.checked)
        {
            const Vec2 a = {r.x + 8.f, r.y + r.h * 0.5f};
            const Vec2 b = {r.x + 12.f, r.y + r.h * 0.5f + 4.f};
            const Vec2 c = {r.x + 19.f, r.y + r.h * 0.5f - 4.f};
            painter.line(a, b, textColor, 2.f);
            painter.line(b, c, textColor, 2.f);
        }

        painter.text(f, {std::floor(r.x + kCheckColumn), y}, item.text, textColor);

        if (!item.submenu.empty())
        {
            const float cx = r.right() - 12.f;
            const float cy = r.y + r.h * 0.5f;
            painter.triangle({cx - 3.f, cy - 4.f}, {cx + 3.f, cy}, {cx - 3.f, cy + 4.f}, textColor);
        }
        else if (!item.shortcut.empty())
        {
            const float x = r.right() - 12.f - f.textWidth(item.shortcut);
            painter.text(f, {std::floor(x), y}, item.shortcut, t.textDim);
        }
    }
}

void PopupMenu::notifyClosed()
{
    if (onClose_)
    {
        auto callback = std::move(onClose_);
        onClose_      = nullptr;
        callback();
    }
}

void PopupMenu::onMouseLeave()
{
    if (openSub_ < 0)
        hover_ = -1;
}

void PopupMenu::openSubmenu(int index)
{
    if (!context())
        return;
    const MenuItem& item = items_[static_cast<std::size_t>(index)];
    if (item.submenu.empty() || !item.enabled)
        return;

    context()->closePopupsAbove(this);
    auto popup = std::make_unique<PopupMenu>(item.submenu);
    const Rect r = itemRect(static_cast<std::size_t>(index));
    popup->setAnchor({rect().right() - 2.f, r.y - 4.f});
    popup->setFlipX(rect().x + 2.f);
    context()->openPopup(std::move(popup));
    openSub_ = index;
}

bool PopupMenu::onMouseMove(Vec2 position)
{
    const int index = itemAt(position);
    if (index != hover_ || (index >= 0 && openSub_ != index && !items_[static_cast<std::size_t>(index)].submenu.empty()))
    {
        hover_ = index;
        if (index >= 0 && !items_[static_cast<std::size_t>(index)].submenu.empty())
        {
            if (openSub_ != index)
                openSubmenu(index);
        }
        else if (openSub_ >= 0 && context())
        {
            context()->closePopupsAbove(this);
            openSub_ = -1;
        }
    }
    return true;
}

bool PopupMenu::onMouseDown(const MouseEvent& event)
{
    (void)event;
    return true;
}

void PopupMenu::activate(int index)
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return;
    MenuItem item = items_[static_cast<std::size_t>(index)];
    if (item.separator || !item.enabled)
        return;
    if (!item.submenu.empty())
    {
        openSubmenu(index);
        return;
    }
    if (context())
        context()->closePopups();
    if (item.action)
        item.action();
}

bool PopupMenu::onMouseUp(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return true;
    const int index = itemAt(event.position);
    if (index >= 0)
        activate(index);
    return true;
}

void PopupMenu::moveHover(int direction)
{
    const int count = static_cast<int>(items_.size());
    if (count == 0)
        return;
    int index = hover_;
    for (int step = 0; step < count; ++step)
    {
        index = index < 0 ? (direction > 0 ? 0 : count - 1) : (index + direction + count) % count;
        const MenuItem& item = items_[static_cast<std::size_t>(index)];
        if (!item.separator && item.enabled)
        {
            hover_ = index;
            if (openSub_ >= 0 && context() && items_[static_cast<std::size_t>(index)].submenu.empty())
            {
                context()->closePopupsAbove(this);
                openSub_ = -1;
            }
            return;
        }
    }
}

bool PopupMenu::onKeyDown(const KeyEvent& event)
{
    using events::KeyCode;
    switch (event.code)
    {
    case KeyCode::Down:
        moveHover(1);
        return true;
    case KeyCode::Up:
        moveHover(-1);
        return true;
    case KeyCode::Enter:
    case KeyCode::Space:
        activate(hover_);
        return true;
    case KeyCode::Right:
        if (hover_ >= 0 && !items_[static_cast<std::size_t>(hover_)].submenu.empty())
            openSubmenu(hover_);
        return true;
    case KeyCode::Left:
        if (context() && context()->popupCount() > 1)
            context()->closeTopPopup();
        return true;
    case KeyCode::Escape:
        if (context())
            context()->closePopups();
        return true;
    default:
        return true;
    }
}

MenuBar::~MenuBar()
{
    if (context())
        context()->closePopups();
}

void MenuBar::addMenu(std::string title, std::function<std::vector<MenuItem>()> builder)
{
    entries_.push_back({std::move(title), std::move(builder)});
}

void MenuBar::addMenu(std::string title, std::vector<MenuItem> items)
{
    entries_.push_back({std::move(title), [items]() { return items; }});
}

Vec2 MenuBar::contentMinSize() const
{
    float width = 8.f;
    for (const Entry& e : entries_)
        width += font().textWidth(e.title) + 24.f;
    return {width, theme().controlHeight};
}

Rect MenuBar::titleRect(std::size_t index) const
{
    float x = rect().x + 4.f;
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        const float w = font().textWidth(entries_[i].title) + 24.f;
        if (i == index)
            return {x, rect().y + 2.f, w, rect().h - 4.f};
        x += w;
    }
    return {};
}

int MenuBar::titleAt(Vec2 position) const
{
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (titleRect(i).contains(position))
            return static_cast<int>(i);
    return -1;
}

void MenuBar::draw(Painter& painter)
{
    const Theme& t = theme();
    painter.fillRect(rect(), t.deep);

    Font& f = font();
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        const Rect r = titleRect(i);
        const bool open = static_cast<int>(i) == openIndex_;
        if (open)
            painter.fillRoundRect(r, t.raisedPressed, t.radius);
        else if (hovered() && context() && r.contains(context()->mousePosition()))
            painter.fillRoundRect(r, t.raisedHover, t.radius);
        const float x = r.x + (r.w - f.textWidth(entries_[i].title)) * 0.5f;
        const float y = r.y + (r.h - f.lineHeight()) * 0.5f;
        painter.text(f, {std::floor(x), std::floor(y)}, entries_[i].title, t.text);
    }
}

void MenuBar::openMenu(int index)
{
    if (!context() || index < 0 || index >= static_cast<int>(entries_.size()))
        return;
    context()->closePopups();

    std::vector<MenuItem> items;
    if (entries_[static_cast<std::size_t>(index)].builder)
        items = entries_[static_cast<std::size_t>(index)].builder();

    auto popup = std::make_unique<PopupMenu>(std::move(items));
    const Rect title = titleRect(static_cast<std::size_t>(index));
    popup->setAnchor({title.x, rect().bottom()});
    popup->setOnClose([this]() { openIndex_ = -1; });
    context()->openPopup(std::move(popup));
    openIndex_ = index;
}

bool MenuBar::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    const int index = titleAt(event.position);
    if (index < 0)
        return false;
    if (index == openIndex_)
        context()->closePopups();
    else
        openMenu(index);
    return true;
}

bool MenuBar::onMouseMove(Vec2 position)
{
    if (openIndex_ >= 0)
    {
        const int index = titleAt(position);
        if (index >= 0 && index != openIndex_)
            openMenu(index);
    }
    return false;
}

OptionButton::OptionButton()
{
    setFocusable(true);
}

OptionButton::~OptionButton()
{
    if (context())
        context()->closePopups();
}

void OptionButton::setItems(std::vector<std::string> items)
{
    items_ = std::move(items);
    if (selected_ >= static_cast<int>(items_.size()))
        selected_ = items_.empty() ? -1 : 0;
    if (selected_ < 0 && !items_.empty())
        selected_ = 0;
}

void OptionButton::setSelected(int index)
{
    selected_ = (index >= 0 && index < static_cast<int>(items_.size())) ? index : -1;
}

Vec2 OptionButton::contentMinSize() const
{
    float width = 0.f;
    for (const std::string& item : items_)
        width = (std::max)(width, font().textWidth(item));
    return {width + theme().padding * 2.f + 24.f, theme().controlHeight};
}

Cursor OptionButton::cursorAt(Vec2) const
{
    return Cursor::Hand;
}

void OptionButton::draw(Painter& painter)
{
    const Theme& t = theme();
    Color fill     = hovered() || open_ ? t.raisedHover : t.raised;
    if (focused())
        painter.roundRectWithBorder(rect(), fill, t.accent, t.radius, 1.f);
    else
        painter.fillRoundRect(rect(), fill, t.radius);

    Font& f = font();
    const std::string& label = selected_ >= 0 ? items_[static_cast<std::size_t>(selected_)] : std::string();
    const float y            = rect().y + (rect().h - f.lineHeight()) * 0.5f;
    painter.text(f, {std::floor(rect().x + t.padding), std::floor(y)}, label, t.text);

    const float cx = rect().right() - 14.f;
    const float cy = rect().y + rect().h * 0.5f;
    painter.triangle({cx - 4.f, cy - 2.f}, {cx + 4.f, cy - 2.f}, {cx, cy + 3.f}, t.textDim);
}

void OptionButton::openList()
{
    if (!context() || items_.empty())
        return;
    context()->closePopups();

    std::vector<MenuItem> entries;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        MenuItem item = MenuItem::check(items_[i], static_cast<int>(i) == selected_);
        item.action   = [this, i]() {
            selected_ = static_cast<int>(i);
            if (onSelected_)
                onSelected_(selected_);
        };
        entries.push_back(std::move(item));
    }

    auto popup = std::make_unique<PopupMenu>(std::move(entries));
    popup->setAnchor({rect().x, rect().bottom() + 2.f});
    popup->setMinWidth(rect().w);
    popup->setOnClose([this]() { open_ = false; });
    context()->openPopup(std::move(popup));
    open_ = true;
}

bool OptionButton::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    if (open_)
        context()->closePopups();
    else
        openList();
    return true;
}

bool OptionButton::onKeyDown(const KeyEvent& event)
{
    if (event.code == events::KeyCode::Enter || event.code == events::KeyCode::Space || event.code == events::KeyCode::Down)
    {
        openList();
        return true;
    }
    return false;
}

Vec2 Tooltip::contentMinSize() const
{
    return {font().textWidth(text_) + 16.f, font().lineHeight() + 10.f};
}

void Tooltip::layout()
{
    const Vec2 size = minSize();
    Vec2 pos        = {position_.x + 14.f, position_.y + 18.f};
    if (context())
    {
        const Vec2 window = context()->size();
        if (pos.x + size.x > window.x)
            pos.x = window.x - size.x;
        if (pos.y + size.y > window.y)
            pos.y = position_.y - size.y - 6.f;
    }
    setRect({(std::max)(0.f, pos.x), (std::max)(0.f, pos.y), size.x, size.y});
}

void Tooltip::draw(Painter& painter)
{
    const Theme& t = theme();
    painter.roundRectWithBorder(rect(), t.raised, t.borderStrong, t.radius, 1.f);
    Font& f = font();
    painter.text(f, {std::floor(rect().x + 8.f), std::floor(rect().y + 5.f)}, text_, t.text);
}

}
