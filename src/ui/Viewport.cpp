#include "zio/ui/Viewport.h"

namespace zio::ui
{

ViewportWidget::ViewportWidget()
{
    setFocusable(true);
}

void ViewportWidget::draw(Painter& painter)
{
    if (background_)
        painter.fillRect(rect(), *background_);
}

bool ViewportWidget::onMouseDown(const MouseEvent& event)
{
    dragging_   = true;
    dragButton_ = event.button;
    last_       = event.position;
    haveLast_   = true;
    if (onDown_)
        onDown_(event);
    return true;
}

bool ViewportWidget::onMouseUp(const MouseEvent& event)
{
    if (event.button != dragButton_ && dragging_)
        return true;
    dragging_ = false;
    if (onUp_)
        onUp_(event);
    return true;
}

bool ViewportWidget::onMouseMove(Vec2 position)
{
    const Vec2 delta = haveLast_ ? position - last_ : Vec2{0.f, 0.f};
    last_            = position;
    haveLast_        = true;
    if (onMove_)
        onMove_(position, delta);
    return dragging_;
}

bool ViewportWidget::onMouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers)
{
    (void)position;
    if (!onScroll_)
        return false;
    onScroll_(delta, modifiers);
    return true;
}

bool ViewportWidget::onKeyDown(const KeyEvent& event)
{
    return onKey_ ? onKey_(event, true) : false;
}

bool ViewportWidget::onKeyUp(const KeyEvent& event)
{
    return onKey_ ? onKey_(event, false) : false;
}

Cursor ViewportWidget::cursorAt(Vec2) const
{
    return cursor_;
}

}
