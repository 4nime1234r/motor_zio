#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Controls.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

class ZIO_API NumberEdit final : public Widget
{
public:
    NumberEdit();

    void setValue(double value);
    double value() const { return value_; }
    void setRange(double minimum, double maximum);
    void setStep(double step) { step_ = step; }
    void setDecimals(int decimals) { decimals_ = decimals < 0 ? 0 : decimals; }
    void setLabel(std::string text, std::optional<Color> color = std::nullopt);
    void setOnChanged(std::function<void(double)> callback) { onChanged_ = std::move(callback); }
    bool editing() const { return editing_; }
    std::string formatted() const;

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onKeyDown(const KeyEvent& event) override;
    bool onChar(char32_t codepoint) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    void startEditing(const std::string& initial, bool selectAll);
    void commit(const std::string& text);
    void finishEditing();
    void apply(double value, bool notify);
    double round(double value) const;

    double value_    = 0.0;
    double minimum_  = -1e9;
    double maximum_  = 1e9;
    double step_     = 1.0;
    int decimals_    = 2;
    std::string label_;
    std::optional<Color> labelColor_;
    std::function<void(double)> onChanged_;

    LineEdit* edit_ = nullptr;
    bool editing_   = false;
    bool cancel_    = false;
    bool pressed_   = false;
    bool dragged_   = false;
    float pressX_   = 0.f;
    double startValue_ = 0.0;
};

class ZIO_API TextEdit final : public Widget
{
public:
    TextEdit();

    void setText(std::string text);
    const std::string& text() const { return text_; }
    void setReadOnly(bool readOnly) { readOnly_ = readOnly; }
    void setOnChanged(std::function<void(const std::string&)> callback) { onChanged_ = std::move(callback); }

    std::size_t lineCount() const { return lineStarts_.size(); }
    std::size_t caret() const { return caret_; }
    std::size_t caretLine() const { return lineOf(caret_); }
    bool hasSelection() const { return caret_ != anchor_; }
    std::string selectedText() const;
    float scrollY() const { return scrollY_; }
    void selectAll();

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onMouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers) override;
    bool onKeyDown(const KeyEvent& event) override;
    bool onChar(char32_t codepoint) override;
    void onFocusChanged(bool focused) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    Rect textArea() const;
    float lineHeight() const;
    std::size_t lineStart(std::size_t line) const { return lineStarts_[line]; }
    std::size_t lineEnd(std::size_t line) const;
    std::size_t lineOf(std::size_t index) const;
    float xOf(std::size_t index) const;
    std::size_t indexInLine(std::size_t line, float x) const;
    std::size_t indexAt(Vec2 position) const;
    void rebuildLines();
    void moveCaret(std::size_t index, bool extend);
    void moveVertical(int lines, bool extend);
    void insert(const std::string& utf8);
    void eraseRange(std::size_t from, std::size_t to);
    void deleteSelection();
    void ensureCaretVisible();
    void clampScroll();
    void changed();
    void restartBlink();

    std::string text_;
    std::vector<std::size_t> lineStarts_ = {0};
    std::size_t caret_  = 0;
    std::size_t anchor_ = 0;
    float scrollY_      = 0.f;
    float scrollX_      = 0.f;
    float desiredX_     = -1.f;
    bool dragging_      = false;
    bool readOnly_      = false;
    double blinkBase_   = 0.0;
    std::function<void(const std::string&)> onChanged_;
};

}
