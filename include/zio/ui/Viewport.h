#pragma once

#include <functional>
#include <optional>

#include "zio/core/Platform.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

class ZIO_API ViewportWidget final : public Widget
{
public:
    ViewportWidget();

    Rect viewportRect() const { return rect(); }
    Vec2 localPosition(Vec2 position) const { return {position.x - rect().x, position.y - rect().y}; }
    bool dragging() const { return dragging_; }
    events::MouseButton dragButton() const { return dragButton_; }

    void setBackground(std::optional<Color> color) { background_ = color; }
    void setCursor(Cursor cursor) { cursor_ = cursor; }

    void setOnMouseDown(std::function<void(const MouseEvent&)> callback) { onDown_ = std::move(callback); }
    void setOnMouseUp(std::function<void(const MouseEvent&)> callback) { onUp_ = std::move(callback); }
    void setOnMouseMove(std::function<void(Vec2 position, Vec2 delta)> callback) { onMove_ = std::move(callback); }
    void setOnScroll(std::function<void(Vec2 delta, Modifiers modifiers)> callback) { onScroll_ = std::move(callback); }
    void setOnKey(std::function<bool(const KeyEvent&, bool down)> callback) { onKey_ = std::move(callback); }

    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onMouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers) override;
    bool onKeyDown(const KeyEvent& event) override;
    bool onKeyUp(const KeyEvent& event) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    std::optional<Color> background_;
    Cursor cursor_ = Cursor::Arrow;
    bool dragging_ = false;
    events::MouseButton dragButton_ = events::MouseButton::Left;
    Vec2 last_ = {0.f, 0.f};
    bool haveLast_ = false;
    std::function<void(const MouseEvent&)> onDown_;
    std::function<void(const MouseEvent&)> onUp_;
    std::function<void(Vec2, Vec2)> onMove_;
    std::function<void(Vec2, Modifiers)> onScroll_;
    std::function<bool(const KeyEvent&, bool)> onKey_;
};

}
