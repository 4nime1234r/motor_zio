#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "zio/core/Platform.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

class ZIO_API Label : public Widget
{
public:
    explicit Label(std::string text = {});

    void setText(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }
    void setColor(std::optional<Color> color) { color_ = color; }
    void setAlign(Align align) { align_ = align; }
    void setBold(bool bold) { bold_ = bold; }

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;

private:
    Font& activeFont() const;

    std::string text_;
    std::optional<Color> color_;
    Align align_ = Align::Start;
    bool bold_   = false;
};

class ZIO_API Button : public Widget
{
public:
    explicit Button(std::string text = {});

    void setText(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }
    void setToggle(bool toggle) { toggle_ = toggle; }
    bool toggled() const { return toggled_; }
    void setToggled(bool toggled) { toggled_ = toggled; }
    void setFlat(bool flat) { flat_ = flat; }
    void setPrimary(bool primary) { primary_ = primary; }
    void setOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }
    void setOnToggled(std::function<void(bool)> callback) { onToggled_ = std::move(callback); }
    void click();

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    std::string text_;
    bool toggle_  = false;
    bool toggled_ = false;
    bool flat_    = false;
    bool primary_ = false;
    bool pressed_ = false;
    std::function<void()> onClick_;
    std::function<void(bool)> onToggled_;
};

class ZIO_API CheckBox : public Widget
{
public:
    explicit CheckBox(std::string text = {});

    void setText(std::string text) { text_ = std::move(text); }
    bool checked() const { return checked_; }
    void setChecked(bool checked) { checked_ = checked; }
    void setOnToggled(std::function<void(bool)> callback) { onToggled_ = std::move(callback); }

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    void toggle();

    std::string text_;
    bool checked_ = false;
    bool pressed_ = false;
    std::function<void(bool)> onToggled_;
};

class ZIO_API LineEdit : public Widget
{
public:
    LineEdit();

    void setText(std::string text);
    const std::string& text() const { return text_; }
    void setPlaceholder(std::string text) { placeholder_ = std::move(text); }
    void setReadOnly(bool readOnly) { readOnly_ = readOnly; }
    void setOnChanged(std::function<void(const std::string&)> callback) { onChanged_ = std::move(callback); }
    void setOnSubmit(std::function<void(const std::string&)> callback) { onSubmit_ = std::move(callback); }
    void setOnFocusLost(std::function<void()> callback) { onFocusLost_ = std::move(callback); }
    void setOnCancel(std::function<void()> callback) { onCancel_ = std::move(callback); }

    std::size_t caret() const { return caret_; }
    bool hasSelection() const { return caret_ != anchor_; }
    std::string selectedText() const;
    void selectAll();
    void moveCaretToEnd();

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onKeyDown(const KeyEvent& event) override;
    bool onChar(char32_t codepoint) override;
    void onFocusChanged(bool focused) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    float widthOf(std::size_t index) const;
    std::size_t indexAtX(float x) const;
    Rect textArea() const;
    void moveCaret(std::size_t index, bool extend);
    void insert(const std::string& utf8);
    void eraseRange(std::size_t from, std::size_t to);
    void deleteSelection();
    void ensureCaretVisible();
    void changed();
    void restartBlink();

    std::string text_;
    std::string placeholder_;
    std::size_t caret_  = 0;
    std::size_t anchor_ = 0;
    float scrollX_      = 0.f;
    bool dragging_      = false;
    bool readOnly_      = false;
    double blinkBase_   = 0.0;
    std::function<void(const std::string&)> onChanged_;
    std::function<void(const std::string&)> onSubmit_;
    std::function<void()> onFocusLost_;
    std::function<void()> onCancel_;
};

class ZIO_API Separator final : public Widget
{
public:
    explicit Separator(bool vertical = false);

    Vec2 contentMinSize() const override { return {1.f, 1.f}; }
    void draw(Painter& painter) override;

private:
    bool vertical_;
};

class ZIO_API ProgressBar final : public Widget
{
public:
    void setValue(float value) { value_ = value < 0.f ? 0.f : (value > 1.f ? 1.f : value); }
    float value() const { return value_; }

    Vec2 contentMinSize() const override { return {0.f, 8.f}; }
    void draw(Painter& painter) override;

private:
    float value_ = 0.f;
};

}
