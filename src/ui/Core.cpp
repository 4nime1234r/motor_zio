#include <algorithm>
#include <cmath>

#include "zio/ui/Containers.h"
#include "zio/ui/Context.h"
#include "zio/ui/Menus.h"
#include "zio/ui/Font.h"
#include "zio/ui/Text.h"
#include "zio/ui/Theme.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

namespace
{

class FixedFont final : public Font
{
public:
    explicit FixedFont(float size) : size_(size) {}

    float size() const override { return size_; }
    float lineHeight() const override { return size_ * 1.3f; }
    float ascent() const override { return size_; }
    float advance(char32_t) override { return size_ * 0.5f; }

private:
    float size_;
};

const Theme& fallbackTheme()
{
    static const Theme theme = makeZionTheme(createFixedFont(14.f));
    return theme;
}

}

float Font::textWidth(std::string_view utf8)
{
    float width       = 0.f;
    std::size_t index = 0;
    while (index < utf8.size())
        width += advance(text::decode(utf8, index));
    return width;
}

std::shared_ptr<Font> createFixedFont(float sizePx)
{
    return std::make_shared<FixedFont>(sizePx);
}

Theme makeZionTheme(std::shared_ptr<Font> font)
{
    Theme theme;
    theme.font = std::move(font);
    return theme;
}

Widget::Widget() = default;

Widget::~Widget()
{
    if (context_)
        context_->forget(this);
}

const Theme& Widget::theme() const
{
    return context_ ? context_->theme() : fallbackTheme();
}

Font& Widget::font() const
{
    return *theme().font;
}

bool Widget::enabledInTree() const
{
    for (const Widget* w = this; w; w = w->parent_)
        if (!w->enabled_)
            return false;
    return true;
}

Vec2 Widget::minSize() const
{
    const Vec2 content = contentMinSize();
    return {(std::max)(content.x, customMin_.x), (std::max)(content.y, customMin_.y)};
}

Widget* Widget::addChild(std::unique_ptr<Widget> child)
{
    Widget* pointer = child.get();
    pointer->parent_ = this;
    pointer->attachContext(context_);
    children_.push_back(std::move(child));
    return pointer;
}

std::unique_ptr<Widget> Widget::removeChild(Widget* child)
{
    for (auto it = children_.begin(); it != children_.end(); ++it)
    {
        if (it->get() != child)
            continue;
        std::unique_ptr<Widget> removed = std::move(*it);
        children_.erase(it);
        removed->parent_ = nullptr;
        removed->attachContext(nullptr);
        return removed;
    }
    return nullptr;
}

void Widget::clearChildren()
{
    for (auto& child : children_)
        child->attachContext(nullptr);
    children_.clear();
}

void Widget::attachContext(Context* context)
{
    if (context_ && context_ != context)
        context_->forget(this);
    context_ = context;
    for (auto& child : children_)
        child->attachContext(context);
}

Widget* Widget::hitTest(Vec2 point)
{
    if (!visible_)
        return nullptr;
    if (clipChildren_ && !rect_.contains(point))
        return nullptr;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it)
        if (Widget* hit = (*it)->hitTest(point))
            return hit;
    if (!mouseTransparent_ && rect_.contains(point))
        return this;
    return nullptr;
}

void Widget::layoutTree()
{
    if (!visible_)
        return;
    layout();
    for (auto& child : children_)
        child->layoutTree();
}

void Widget::updateTree(float dt)
{
    update(dt);
    for (auto& child : children_)
        child->updateTree(dt);
}

void Widget::drawTree(Painter& painter)
{
    if (!visible_)
        return;
    draw(painter);
    if (clipChildren_)
        painter.pushClip(rect_);
    for (auto& child : children_)
        child->drawTree(painter);
    if (clipChildren_)
        painter.popClip();
    drawForeground(painter);
}

Context::Context()
{
    auto root = std::make_unique<Panel>();
    root->setTransparent(true);
    root_ = std::move(root);
    theme_ = makeZionTheme(createFixedFont(14.f));
    root_->setMouseTransparent(true);
    root_->attachContext(this);

    overlay_ = std::make_unique<Widget>();
    overlay_->setMouseTransparent(true);
    overlay_->attachContext(this);
}

Context::~Context()
{
    overlay_.reset();
    graveyard_.clear();
    root_.reset();
}

void Context::forget(Widget* widget)
{
    if (hovered_ == widget)
        hovered_ = nullptr;
    if (focused_ == widget)
        focused_ = nullptr;
    if (captured_ == widget)
        captured_ = nullptr;
    if (tooltip_ == widget)
        tooltip_ = nullptr;
    if (tooltipTarget_ == widget)
        tooltipTarget_ = nullptr;
}

void Context::collectGarbage()
{
    graveyard_.clear();
}

std::vector<Widget*> Context::popupList() const
{
    std::vector<Widget*> list;
    if (!overlay_)
        return list;
    for (const auto& child : overlay_->children())
        if (child.get() != tooltip_)
            list.push_back(child.get());
    return list;
}

bool Context::hasPopups() const
{
    return !popupList().empty();
}

std::size_t Context::popupCount() const
{
    return popupList().size();
}

Widget* Context::topPopup() const
{
    const auto list = popupList();
    return list.empty() ? nullptr : list.back();
}

Widget* Context::openPopup(std::unique_ptr<Widget> popup)
{
    hideTooltip();
    return overlay_->addChild(std::move(popup));
}

void Context::closePopupWidget(Widget* popup)
{
    popup->onPopupClosed();
    if (auto removed = overlay_->removeChild(popup))
        graveyard_.push_back(std::move(removed));
}

void Context::closePopups()
{
    if (!overlay_)
        return;
    auto list = popupList();
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        closePopupWidget(*it);
}

void Context::closePopupsAbove(Widget* popup)
{
    if (!overlay_ || !popup)
        return;
    const auto list = popupList();
    bool after      = false;
    std::vector<Widget*> toClose;
    for (Widget* w : list)
    {
        if (after)
            toClose.push_back(w);
        if (w == popup)
            after = true;
    }
    for (auto it = toClose.rbegin(); it != toClose.rend(); ++it)
        closePopupWidget(*it);
}

void Context::closeTopPopup()
{
    if (Widget* top = topPopup())
        closePopupWidget(top);
}

void Context::hideTooltip()
{
    if (!tooltip_ || !overlay_)
        return;
    Widget* shown = tooltip_;
    tooltip_      = nullptr;
    if (auto removed = overlay_->removeChild(shown))
        graveyard_.push_back(std::move(removed));
}

void Context::updateTooltip(float dt)
{
    Widget* target = hovered_;
    if (hasPopups() || !target || target->tooltip().empty() || captured_)
    {
        hideTooltip();
        tooltipTimer_  = 0.f;
        tooltipTarget_ = target;
        return;
    }
    if (target != tooltipTarget_)
    {
        tooltipTarget_ = target;
        tooltipTimer_  = 0.f;
        hideTooltip();
        return;
    }
    tooltipTimer_ += dt;
    if (!tooltip_ && tooltipTimer_ >= 0.5f)
        tooltip_ = overlay_->addChild(std::make_unique<Tooltip>(target->tooltip(), lastMouse_));
}

void Context::update(float dt)
{
    collectGarbage();
    time_ += static_cast<double>(dt);
    const Rect window = {0.f, 0.f, size_.x, size_.y};
    root_->setRect(window);
    root_->layoutTree();
    overlay_->setRect(window);
    overlay_->layoutTree();
    root_->updateTree(dt);
    overlay_->updateTree(dt);
    updateHover(lastMouse_);
    updateTooltip(dt);
}

void Context::draw(Painter& painter)
{
    root_->drawTree(painter);
    overlay_->drawTree(painter);
}

void Context::updateHover(Vec2 position)
{
    Widget* next = overlay_->hitTest(position);
    if (!next)
        next = root_->hitTest(position);
    if (next == hovered_)
        return;
    Widget* previous = hovered_;
    hovered_         = next;
    if (previous)
    {
        previous->hovered_ = false;
        previous->onMouseLeave();
    }
    if (hovered_)
    {
        hovered_->hovered_ = true;
        hovered_->onMouseEnter();
    }
}

bool Context::mouseMove(Vec2 position)
{
    lastMouse_ = position;
    updateHover(position);

    Widget* target = captured_ ? captured_ : hovered_;
    bool handled   = false;
    for (Widget* w = target; w && !handled; w = w->parent())
        if (w->enabledInTree())
            handled = w->onMouseMove(position);

    Widget* owner = captured_ ? captured_ : hovered_;
    cursor_       = owner ? owner->cursorAt(position) : Cursor::Arrow;
    collectGarbage();
    return wantsMouse();
}

bool Context::mouseButton(events::MouseButton button, bool down, Vec2 position, Modifiers modifiers)
{
    lastMouse_ = position;
    updateHover(position);

    if (down)
    {
        hideTooltip();

        if (hasPopups() && !overlay_->hitTest(position))
        {
            closePopups();
            collectGarbage();
            return true;
        }

        const Vec2 delta = position - lastClickPosition_;
        if (button == lastClickButton_ && time_ - lastClickTime_ < 0.4 && delta.length() < 6.f)
            ++clickCount_;
        else
            clickCount_ = 1;
        lastClickTime_     = time_;
        lastClickPosition_ = position;
        lastClickButton_   = button;

        Widget* target = hovered_;
        if (!target)
        {
            setFocus(nullptr);
            collectGarbage();
            return false;
        }

        Widget* focusTarget = nullptr;
        for (Widget* w = target; w; w = w->parent())
        {
            if (w->focusable() && w->enabledInTree())
            {
                focusTarget = w;
                break;
            }
        }
        bool inOverlay = false;
        for (Widget* w = target; w; w = w->parent())
            if (w == overlay_.get())
                inOverlay = true;
        if (!inOverlay || focusTarget)
            setFocus(focusTarget);

        MouseEvent event;
        event.position  = position;
        event.button    = button;
        event.modifiers = modifiers;
        event.clicks    = clickCount_;
        for (Widget* w = target; w; w = w->parent())
        {
            if (w->enabledInTree() && w->onMouseDown(event))
            {
                captured_      = w;
                captureButton_ = button;
                break;
            }
        }
        collectGarbage();
        return true;
    }

    MouseEvent event;
    event.position  = position;
    event.button    = button;
    event.modifiers = modifiers;
    event.clicks    = clickCount_;

    bool handled = false;
    if (captured_)
    {
        if (captured_->enabledInTree())
            handled = captured_->onMouseUp(event);
        if (button == captureButton_)
            captured_ = nullptr;
    }
    else
    {
        for (Widget* w = hovered_; w && !handled; w = w->parent())
            if (w->enabledInTree())
                handled = w->onMouseUp(event);
    }
    updateHover(position);
    collectGarbage();
    return handled || hovered_ != nullptr;
}

bool Context::mouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers)
{
    lastMouse_ = position;
    updateHover(position);

    bool handled = false;
    for (Widget* w = hovered_; w && !handled; w = w->parent())
        if (w->enabledInTree())
            handled = w->onMouseScroll(position, delta, modifiers);
    collectGarbage();
    return handled || hovered_ != nullptr;
}

bool Context::key(events::KeyCode code, bool down, bool repeat, Modifiers modifiers)
{
    KeyEvent event;
    event.code      = code;
    event.modifiers = modifiers;
    event.repeat    = repeat;

    if (Widget* popup = topPopup())
    {
        bool focusInside = false;
        for (Widget* w = focused_; w; w = w->parent())
            if (w == popup)
                focusInside = true;
        if (!focusInside)
        {
            if (down)
                popup->onKeyDown(event);
            collectGarbage();
            return true;
        }
    }

    if (!focused_)
        return false;

    bool handled = false;
    for (Widget* w = focused_; w && !handled; w = w->parent())
        if (w->enabledInTree())
            handled = down ? w->onKeyDown(event) : w->onKeyUp(event);

    if (down && !handled && code == events::KeyCode::Tab)
    {
        focusNext(modifiers.shift);
        handled = true;
    }
    collectGarbage();
    return handled;
}

bool Context::text(char32_t codepoint)
{
    if (Widget* popup = topPopup())
    {
        bool focusInside = false;
        for (Widget* w = focused_; w; w = w->parent())
            if (w == popup)
                focusInside = true;
        if (!focusInside)
            return true;
    }
    if (!focused_ || !focused_->enabledInTree())
        return false;
    const bool handled = focused_->onChar(codepoint);
    collectGarbage();
    return handled;
}

void Context::setFocus(Widget* widget)
{
    if (widget == focused_)
        return;
    Widget* previous = focused_;
    focused_         = widget;
    if (previous)
    {
        previous->focused_ = false;
        previous->onFocusChanged(false);
    }
    if (focused_)
    {
        focused_->focused_ = true;
        focused_->onFocusChanged(true);
    }
}

void Context::focusNext(bool backwards)
{
    std::vector<Widget*> order;
    std::vector<Widget*> stack;
    stack.push_back(root_.get());
    while (!stack.empty())
    {
        Widget* w = stack.back();
        stack.pop_back();
        if (!w->visible())
            continue;
        if (w->focusable() && w->enabledInTree())
            order.push_back(w);
        for (auto it = w->children().rbegin(); it != w->children().rend(); ++it)
            stack.push_back(it->get());
    }
    if (order.empty())
        return;

    std::size_t index = 0;
    const auto found  = std::find(order.begin(), order.end(), focused_);
    if (found != order.end())
    {
        index = static_cast<std::size_t>(found - order.begin());
        index = backwards ? (index + order.size() - 1) % order.size() : (index + 1) % order.size();
    }
    setFocus(order[index]);
}

void Context::setClipboardHandlers(std::function<std::string()> getter, std::function<void(const std::string&)> setter)
{
    getClipboard_ = std::move(getter);
    setClipboard_ = std::move(setter);
}

std::string Context::clipboard() const
{
    return getClipboard_ ? getClipboard_() : fallbackClipboard_;
}

void Context::setClipboard(const std::string& text)
{
    if (setClipboard_)
        setClipboard_(text);
    else
        fallbackClipboard_ = text;
}

}
