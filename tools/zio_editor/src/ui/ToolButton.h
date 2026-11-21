#pragma once

#include <functional>

#include "zio/ui/Widget.h"

namespace zio_editor
{

enum class ToolIcon
{
    Move,
    Rotate,
    Scale,
    Focus,
    Play,
    Stop
};

class ToolButton final : public zio::ui::Widget
{
public:
    ToolButton(ToolIcon icon, float size);

    void setActive(bool active) { active_ = active; }
    bool active() const { return active_; }
    void setOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }

    zio::ui::Vec2 contentMinSize() const override { return {size_, size_}; }
    void draw(zio::ui::Painter& painter) override;
    bool onMouseDown(const zio::ui::MouseEvent& event) override;
    bool onMouseUp(const zio::ui::MouseEvent& event) override;
    zio::ui::Cursor cursorAt(zio::ui::Vec2 position) const override { return zio::ui::Cursor::Hand; }

private:
    ToolIcon icon_;
    float size_;
    bool active_  = false;
    bool pressed_ = false;
    std::function<void()> onClick_;
};

}
