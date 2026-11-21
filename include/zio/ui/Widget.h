#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Painter.h"
#include "zio/ui/Theme.h"
#include "zio/ui/Types.h"

namespace zio::ui
{

class Context;

class ZIO_API Widget
{
public:
    Widget();
    virtual ~Widget();

    Widget(const Widget&)            = delete;
    Widget& operator=(const Widget&) = delete;

    Widget* parent() const { return parent_; }
    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

    Widget* addChild(std::unique_ptr<Widget> child);
    std::unique_ptr<Widget> removeChild(Widget* child);
    void clearChildren();

    template <typename T, typename... Args>
    T* add(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* pointer = child.get();
        addChild(std::move(child));
        return pointer;
    }

    Context* context() const { return context_; }
    const Theme& theme() const;

    void setName(std::string name) { name_ = std::move(name); }
    const std::string& name() const { return name_; }

    void setVisible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    bool enabledInTree() const;

    void setRect(const Rect& rect) { rect_ = rect; }
    const Rect& rect() const { return rect_; }

    void setMinSize(Vec2 size) { customMin_ = size; }
    Vec2 customMinSize() const { return customMin_; }
    Vec2 minSize() const;

    void setExpand(bool horizontal, bool vertical, float stretch = 1.f)
    {
        expandH_ = horizontal;
        expandV_ = vertical;
        stretch_ = stretch;
    }
    bool expandH() const { return expandH_; }
    bool expandV() const { return expandV_; }
    float stretch() const { return stretch_; }

    void setFocusable(bool focusable) { focusable_ = focusable; }
    bool focusable() const { return focusable_; }
    void setMouseTransparent(bool transparent) { mouseTransparent_ = transparent; }
    bool mouseTransparent() const { return mouseTransparent_; }
    void setClipChildren(bool clip) { clipChildren_ = clip; }
    void setTooltip(std::string text) { tooltip_ = std::move(text); }
    const std::string& tooltip() const { return tooltip_; }
    bool clipChildren() const { return clipChildren_; }

    bool hovered() const { return hovered_; }
    bool focused() const { return focused_; }

    virtual Vec2 contentMinSize() const { return {0.f, 0.f}; }
    virtual void layout() {}
    virtual void update(float dt) { (void)dt; }
    virtual void draw(Painter& painter) { (void)painter; }
    virtual void drawForeground(Painter& painter) { (void)painter; }
    virtual Widget* hitTest(Vec2 point);

    virtual bool onMouseDown(const MouseEvent& event) { (void)event; return false; }
    virtual bool onMouseUp(const MouseEvent& event) { (void)event; return false; }
    virtual bool onMouseMove(Vec2 position) { (void)position; return false; }
    virtual bool onMouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers)
    {
        (void)position;
        (void)delta;
        (void)modifiers;
        return false;
    }
    virtual bool onKeyDown(const KeyEvent& event) { (void)event; return false; }
    virtual bool onKeyUp(const KeyEvent& event) { (void)event; return false; }
    virtual bool onChar(char32_t codepoint) { (void)codepoint; return false; }
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}
    virtual void onFocusChanged(bool focused) { (void)focused; }
    virtual void onPopupClosed() {}
    virtual Cursor cursorAt(Vec2 position) const { (void)position; return Cursor::Arrow; }

    void layoutTree();
    void updateTree(float dt);
    void drawTree(Painter& painter);

protected:
    Font& font() const;

private:
    friend class Context;

    void attachContext(Context* context);

    Widget* parent_   = nullptr;
    Context* context_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    std::string name_;
    std::string tooltip_;
    Rect rect_;
    Vec2 customMin_ = {0.f, 0.f};
    float stretch_  = 1.f;
    bool visible_   = true;
    bool enabled_   = true;
    bool expandH_   = false;
    bool expandV_   = false;
    bool focusable_ = false;
    bool mouseTransparent_ = false;
    bool clipChildren_     = false;
    bool hovered_   = false;
    bool focused_   = false;
};

}
