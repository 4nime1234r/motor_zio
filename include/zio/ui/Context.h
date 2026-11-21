#pragma once

#include <functional>
#include <string>

#include "zio/core/Platform.h"
#include "zio/ui/Theme.h"
#include "zio/ui/Types.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

class ZIO_API Context
{
public:
    Context();
    ~Context();

    Context(const Context&)            = delete;
    Context& operator=(const Context&) = delete;

    Widget& root() { return *root_; }
    const Widget& root() const { return *root_; }

    Theme& theme() { return theme_; }
    const Theme& theme() const { return theme_; }
    void setTheme(const Theme& theme) { theme_ = theme; }

    void setSize(Vec2 size) { size_ = size; }
    Vec2 size() const { return size_; }

    void update(float dt);
    void draw(Painter& painter);

    bool mouseMove(Vec2 position);
    bool mouseButton(events::MouseButton button, bool down, Vec2 position, Modifiers modifiers);
    bool mouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers);
    bool key(events::KeyCode code, bool down, bool repeat, Modifiers modifiers);
    bool text(char32_t codepoint);

    Widget* hovered() const { return hovered_; }
    Widget* focused() const { return focused_; }
    void setFocus(Widget* widget);
    bool wantsMouse() const { return hovered_ != nullptr || captured_ != nullptr; }
    bool wantsKeyboard() const { return focused_ != nullptr; }
    Cursor cursor() const { return cursor_; }
    double time() const { return time_; }

    void setClipboardHandlers(std::function<std::string()> getter, std::function<void(const std::string&)> setter);
    std::string clipboard() const;
    void setClipboard(const std::string& text);

private:
    friend class Widget;

    void forget(Widget* widget);
    void focusNext(bool backwards);
    void updateHover(Vec2 position);

    std::unique_ptr<Widget> root_;
    Theme theme_;
    Vec2 size_ = {800.f, 600.f};
    Widget* hovered_  = nullptr;
    Widget* focused_  = nullptr;
    Widget* captured_ = nullptr;
    events::MouseButton captureButton_ = events::MouseButton::Left;
    Cursor cursor_ = Cursor::Arrow;
    Vec2 lastMouse_ = {-100000.f, -100000.f};
    double time_ = 0.0;
    double lastClickTime_ = -10.0;
    Vec2 lastClickPosition_;
    events::MouseButton lastClickButton_ = events::MouseButton::Left;
    int clickCount_ = 0;
    std::function<std::string()> getClipboard_;
    std::function<void(const std::string&)> setClipboard_;
    std::string fallbackClipboard_;
};

}
