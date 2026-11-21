#include "zio/ui/Fields.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "zio/ui/Context.h"
#include "zio/ui/Text.h"

namespace zio::ui
{

namespace
{

constexpr float kPixelsPerStep = 4.f;
constexpr float kDragThreshold = 3.f;

}

NumberEdit::NumberEdit()
{
    setFocusable(true);
    edit_ = add<LineEdit>();
    edit_->setVisible(false);
    edit_->setOnSubmit([this](const std::string& text) {
        if (!editing_)
            return;
        commit(text);
        finishEditing();
    });
    edit_->setOnCancel([this]() { cancel_ = true; });
    edit_->setOnFocusLost([this]() {
        if (!editing_)
            return;
        if (!cancel_)
            commit(edit_->text());
        cancel_ = false;
        finishEditing();
    });
}

double NumberEdit::round(double value) const
{
    const double scale = std::pow(10.0, decimals_);
    return std::round(value * scale) / scale;
}

void NumberEdit::apply(double value, bool notify)
{
    const double clamped = std::clamp(round(value), minimum_, maximum_);
    if (clamped == value_)
        return;
    value_ = clamped;
    if (notify && onChanged_)
        onChanged_(value_);
}

void NumberEdit::setValue(double value)
{
    value_ = std::clamp(round(value), minimum_, maximum_);
}

void NumberEdit::setRange(double minimum, double maximum)
{
    minimum_ = minimum;
    maximum_ = maximum;
    value_   = std::clamp(value_, minimum_, maximum_);
}

void NumberEdit::setLabel(std::string text, std::optional<Color> color)
{
    label_      = std::move(text);
    labelColor_ = color;
}

std::string NumberEdit::formatted() const
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals_, value_);
    return buffer;
}

Vec2 NumberEdit::contentMinSize() const
{
    return {64.f, theme().controlHeight};
}

void NumberEdit::layout()
{
    edit_->setRect(rect());
}

Cursor NumberEdit::cursorAt(Vec2) const
{
    return editing_ ? Cursor::IBeam : Cursor::ResizeHorizontal;
}

void NumberEdit::draw(Painter& painter)
{
    if (editing_)
        return;

    const Theme& t     = theme();
    const bool enabled = enabledInTree();
    Color border       = focused() ? t.accent : (hovered() ? t.borderStrong : t.border);
    Color fill         = enabled ? t.deep : Color{t.deep.r, t.deep.g, t.deep.b, 0.45f};
    painter.roundRectWithBorder(rect(), fill, border, t.radius, 1.f);

    Font& f         = font();
    const float y   = std::floor(rect().y + (rect().h - f.lineHeight()) * 0.5f);
    float labelRoom = 0.f;
    if (!label_.empty())
    {
        painter.text(f, {std::floor(rect().x + 8.f), y}, label_, labelColor_ ? *labelColor_ : t.textDim);
        labelRoom = f.textWidth(label_) + 10.f;
    }
    const std::string text = formatted();
    const float area       = rect().w - labelRoom;
    const float x          = rect().x + labelRoom + (area - f.textWidth(text)) * 0.5f;
    painter.text(f, {std::floor(x), y}, text, t.text);
}

void NumberEdit::startEditing(const std::string& initial, bool selectAll)
{
    if (editing_ || !context())
        return;
    editing_ = true;
    cancel_  = false;
    edit_->setVisible(true);
    edit_->setText(initial);
    if (selectAll)
        edit_->selectAll();
    context()->setFocus(edit_);
}

void NumberEdit::finishEditing()
{
    editing_ = false;
    edit_->setVisible(false);
    if (context() && context()->focused() == edit_)
        context()->setFocus(this);
}

void NumberEdit::commit(const std::string& text)
{
    char* end          = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() && std::isfinite(value))
        apply(value, true);
}

bool NumberEdit::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left || editing_)
        return false;
    pressed_    = true;
    dragged_    = false;
    pressX_     = event.position.x;
    startValue_ = value_;
    return true;
}

bool NumberEdit::onMouseMove(Vec2 position)
{
    if (!pressed_)
        return false;
    const float dx = position.x - pressX_;
    if (!dragged_ && std::fabs(dx) < kDragThreshold)
        return true;
    dragged_ = true;

    const double steps = static_cast<double>(dx) / kPixelsPerStep;
    apply(startValue_ + steps * step_, true);
    return true;
}

bool NumberEdit::onMouseUp(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    const bool wasPressed = pressed_;
    pressed_              = false;
    if (wasPressed && !dragged_)
        startEditing(formatted(), true);
    dragged_ = false;
    return true;
}

bool NumberEdit::onKeyDown(const KeyEvent& event)
{
    using events::KeyCode;
    const double stepSize = event.modifiers.shift ? step_ * 10.0 : step_;
    switch (event.code)
    {
    case KeyCode::Up:
    case KeyCode::Right:
        apply(value_ + stepSize, true);
        return true;
    case KeyCode::Down:
    case KeyCode::Left:
        apply(value_ - stepSize, true);
        return true;
    case KeyCode::Enter:
        startEditing(formatted(), true);
        return true;
    default:
        return false;
    }
}

bool NumberEdit::onChar(char32_t codepoint)
{
    if (editing_)
        return false;
    if ((codepoint >= '0' && codepoint <= '9') || codepoint == '-' || codepoint == '.' || codepoint == ',')
    {
        startEditing(std::string(1, codepoint == ',' ? '.' : static_cast<char>(codepoint)), false);
        if (editing_)
            edit_->moveCaretToEnd();
        return true;
    }
    return false;
}


TextEdit::TextEdit()
{
    setFocusable(true);
}

float TextEdit::lineHeight() const
{
    return font().lineHeight() + 2.f;
}

Rect TextEdit::textArea() const
{
    return rect().shrink(theme().padding, 6.f, theme().padding + 4.f, 6.f);
}

Vec2 TextEdit::contentMinSize() const
{
    return {120.f, theme().controlHeight * 3.f};
}

Cursor TextEdit::cursorAt(Vec2) const
{
    return Cursor::IBeam;
}

std::size_t TextEdit::lineEnd(std::size_t line) const
{
    return line + 1 < lineStarts_.size() ? lineStarts_[line + 1] - 1 : text_.size();
}

std::size_t TextEdit::lineOf(std::size_t index) const
{
    const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), index);
    return static_cast<std::size_t>(it - lineStarts_.begin()) - 1;
}

void TextEdit::rebuildLines()
{
    lineStarts_.assign(1, 0);
    for (std::size_t i = 0; i < text_.size(); ++i)
        if (text_[i] == '\n')
            lineStarts_.push_back(i + 1);
}

float TextEdit::xOf(std::size_t index) const
{
    const std::size_t line = lineOf(index);
    return font().textWidth(std::string_view(text_).substr(lineStart(line), index - lineStart(line)));
}

std::size_t TextEdit::indexInLine(std::size_t line, float x) const
{
    Font& f           = font();
    float accumulated = 0.f;
    std::size_t index = lineStart(line);
    const std::size_t end = lineEnd(line);
    while (index < end)
    {
        std::size_t next    = index;
        const char32_t code = text::decode(text_, next);
        const float advance = f.advance(code);
        if (x < accumulated + advance * 0.5f)
            return index;
        accumulated += advance;
        index = next;
    }
    return end;
}

std::size_t TextEdit::indexAt(Vec2 position) const
{
    const Rect area = textArea();
    const float row = (position.y - area.y + scrollY_) / lineHeight();
    const int line  = std::clamp(static_cast<int>(std::floor(row)), 0, static_cast<int>(lineStarts_.size()) - 1);
    return indexInLine(static_cast<std::size_t>(line), position.x - area.x + scrollX_);
}

std::string TextEdit::selectedText() const
{
    const std::size_t from = (std::min)(caret_, anchor_);
    const std::size_t to   = (std::max)(caret_, anchor_);
    return text_.substr(from, to - from);
}

void TextEdit::selectAll()
{
    anchor_ = 0;
    caret_  = text_.size();
}

void TextEdit::setText(std::string text)
{
    text_ = std::move(text);
    text_.erase(std::remove(text_.begin(), text_.end(), '\r'), text_.end());
    rebuildLines();
    caret_   = (std::min)(caret_, text_.size());
    anchor_  = caret_;
    scrollY_ = 0.f;
    scrollX_ = 0.f;
}

void TextEdit::restartBlink()
{
    blinkBase_ = context() ? context()->time() : 0.0;
}

void TextEdit::clampScroll()
{
    const Rect area = textArea();
    const float maxY = (std::max)(0.f, static_cast<float>(lineStarts_.size()) * lineHeight() - area.h);
    scrollY_         = std::clamp(scrollY_, 0.f, maxY);
    scrollX_         = (std::max)(0.f, scrollX_);
}

void TextEdit::ensureCaretVisible()
{
    const Rect area = textArea();
    const float top = static_cast<float>(lineOf(caret_)) * lineHeight();
    if (top < scrollY_)
        scrollY_ = top;
    else if (top + lineHeight() > scrollY_ + area.h)
        scrollY_ = top + lineHeight() - area.h;

    const float x = xOf(caret_);
    if (x < scrollX_)
        scrollX_ = x;
    else if (x > scrollX_ + area.w - 2.f)
        scrollX_ = x - area.w + 2.f;
    clampScroll();
}

void TextEdit::moveCaret(std::size_t index, bool extend)
{
    caret_ = (std::min)(index, text_.size());
    if (!extend)
        anchor_ = caret_;
    desiredX_ = -1.f;
    restartBlink();
    ensureCaretVisible();
}

void TextEdit::moveVertical(int lines, bool extend)
{
    const std::size_t line = lineOf(caret_);
    if (desiredX_ < 0.f)
        desiredX_ = xOf(caret_);
    const long target = static_cast<long>(line) + lines;
    std::size_t index = caret_;
    if (target < 0)
        index = 0;
    else if (target >= static_cast<long>(lineStarts_.size()))
        index = text_.size();
    else
        index = indexInLine(static_cast<std::size_t>(target), desiredX_);

    const float keep = desiredX_;
    moveCaret(index, extend);
    desiredX_ = keep;
}

void TextEdit::changed()
{
    rebuildLines();
    restartBlink();
    ensureCaretVisible();
    if (onChanged_)
        onChanged_(text_);
}

void TextEdit::eraseRange(std::size_t from, std::size_t to)
{
    if (from >= to)
        return;
    text_.erase(from, to - from);
    caret_  = from;
    anchor_ = from;
    changed();
}

void TextEdit::deleteSelection()
{
    eraseRange((std::min)(caret_, anchor_), (std::max)(caret_, anchor_));
}

void TextEdit::insert(const std::string& utf8)
{
    if (readOnly_ || utf8.empty())
        return;
    if (hasSelection())
        deleteSelection();
    text_.insert(caret_, utf8);
    caret_ += utf8.size();
    anchor_ = caret_;
    changed();
}

void TextEdit::onFocusChanged(bool focused)
{
    if (focused)
        restartBlink();
    else
    {
        dragging_ = false;
        anchor_   = caret_;
    }
}

void TextEdit::draw(Painter& painter)
{
    const Theme& t     = theme();
    const bool enabled = enabledInTree();
    const Color border = focused() ? t.accent : (hovered() ? t.borderStrong : t.border);
    painter.roundRectWithBorder(rect(), enabled ? t.deep : Color{t.deep.r, t.deep.g, t.deep.b, 0.45f}, border, t.radius, 1.f);

    Font& f         = font();
    const Rect area = textArea();
    const float lh  = lineHeight();

    painter.pushClip(area);
    const long first = (std::max)(0L, static_cast<long>(std::floor(scrollY_ / lh)));
    const long last  = (std::min)(static_cast<long>(lineStarts_.size()) - 1, static_cast<long>(std::floor((scrollY_ + area.h) / lh)));
    const std::size_t selFrom = (std::min)(caret_, anchor_);
    const std::size_t selTo   = (std::max)(caret_, anchor_);

    for (long l = first; l <= last; ++l)
    {
        const std::size_t line = static_cast<std::size_t>(l);
        const float y          = area.y + static_cast<float>(l) * lh - scrollY_;
        const std::size_t s    = lineStart(line);
        const std::size_t e    = lineEnd(line);

        if (focused() && selFrom != selTo && selTo > s && selFrom <= e)
        {
            const std::size_t from = (std::max)(selFrom, s);
            const std::size_t to   = (std::min)(selTo, e);
            const float x0         = area.x - scrollX_ + font().textWidth(std::string_view(text_).substr(s, from - s));
            float x1               = area.x - scrollX_ + font().textWidth(std::string_view(text_).substr(s, to - s));
            if (selTo > e)
                x1 += 6.f;
            painter.fillRect({x0, y, x1 - x0, lh}, t.selection);
        }
        if (e > s)
            painter.text(f, {std::floor(area.x - scrollX_), std::floor(y + 1.f)}, std::string_view(text_).substr(s, e - s),
                         enabled ? t.text : Color{t.text.r, t.text.g, t.text.b, 0.45f});
    }

    if (focused() && enabled && !readOnly_ && context() && std::fmod(context()->time() - blinkBase_, 1.0) < 0.55)
    {
        const std::size_t line = lineOf(caret_);
        const float y          = area.y + static_cast<float>(line) * lh - scrollY_;
        painter.fillRect({std::floor(area.x - scrollX_ + xOf(caret_)), y + 1.f, 1.5f, lh - 2.f}, t.text);
    }
    painter.popClip();

    const float content = static_cast<float>(lineStarts_.size()) * lh;
    if (content > area.h)
    {
        const float trackH = rect().h - 8.f;
        const float thumbH = (std::max)(24.f, trackH * area.h / content);
        const float travel = trackH - thumbH;
        const float range  = content - area.h;
        const float y      = rect().y + 4.f + (range > 0.f ? travel * scrollY_ / range : 0.f);
        painter.fillRoundRect({rect().right() - 8.f, y, 4.f, thumbH}, rgb(0xffffff, 0.22f), 2.f);
    }
}

bool TextEdit::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    const std::size_t index = indexAt(event.position);
    if (event.clicks >= 3)
    {
        const std::size_t line = lineOf(index);
        anchor_                = lineStart(line);
        caret_                 = lineEnd(line);
    }
    else if (event.clicks == 2)
    {
        anchor_ = text::previousWordBoundary(text_, index);
        caret_  = text::nextWordBoundary(text_, index);
    }
    else
    {
        moveCaret(index, event.modifiers.shift);
        dragging_ = true;
    }
    restartBlink();
    return true;
}

bool TextEdit::onMouseUp(const MouseEvent&)
{
    dragging_ = false;
    return true;
}

bool TextEdit::onMouseMove(Vec2 position)
{
    if (!dragging_)
        return false;
    moveCaret(indexAt(position), true);
    return true;
}

bool TextEdit::onMouseScroll(Vec2, Vec2 delta, Modifiers)
{
    const Rect area = textArea();
    if (static_cast<float>(lineStarts_.size()) * lineHeight() <= area.h)
        return false;
    scrollY_ -= delta.y * lineHeight() * 3.f;
    clampScroll();
    return true;
}

bool TextEdit::onChar(char32_t codepoint)
{
    if (readOnly_ || codepoint < 32 || codepoint == 127)
        return false;
    std::string utf8;
    text::append(utf8, codepoint);
    insert(utf8);
    return true;
}

bool TextEdit::onKeyDown(const KeyEvent& event)
{
    using events::KeyCode;
    const bool ctrl  = event.modifiers.ctrl;
    const bool shift = event.modifiers.shift;
    const std::size_t line = lineOf(caret_);

    switch (event.code)
    {
    case KeyCode::Left:
        if (hasSelection() && !shift)
            moveCaret((std::min)(caret_, anchor_), false);
        else
            moveCaret(ctrl ? text::previousWordBoundary(text_, caret_) : text::previousBoundary(text_, caret_), shift);
        return true;
    case KeyCode::Right:
        if (hasSelection() && !shift)
            moveCaret((std::max)(caret_, anchor_), false);
        else
            moveCaret(ctrl ? text::nextWordBoundary(text_, caret_) : text::nextBoundary(text_, caret_), shift);
        return true;
    case KeyCode::Up:
        moveVertical(-1, shift);
        return true;
    case KeyCode::Down:
        moveVertical(1, shift);
        return true;
    case KeyCode::PageUp:
        moveVertical(-static_cast<int>(textArea().h / lineHeight()), shift);
        return true;
    case KeyCode::PageDown:
        moveVertical(static_cast<int>(textArea().h / lineHeight()), shift);
        return true;
    case KeyCode::Home:
        moveCaret(ctrl ? 0 : lineStart(line), shift);
        return true;
    case KeyCode::End:
        moveCaret(ctrl ? text_.size() : lineEnd(line), shift);
        return true;
    case KeyCode::Backspace:
        if (readOnly_)
            return true;
        if (hasSelection())
            deleteSelection();
        else
            eraseRange(ctrl ? text::previousWordBoundary(text_, caret_) : text::previousBoundary(text_, caret_), caret_);
        return true;
    case KeyCode::Delete:
        if (readOnly_)
            return true;
        if (hasSelection())
            deleteSelection();
        else
            eraseRange(caret_, ctrl ? text::nextWordBoundary(text_, caret_) : text::nextBoundary(text_, caret_));
        return true;
    case KeyCode::Enter:
        insert("\n");
        return true;
    case KeyCode::Tab:
        if (readOnly_)
            return false;
        insert("    ");
        return true;
    case KeyCode::Escape:
        if (context())
            context()->setFocus(nullptr);
        return true;
    case KeyCode::A:
        if (ctrl)
        {
            selectAll();
            restartBlink();
            return true;
        }
        break;
    case KeyCode::C:
        if (ctrl)
        {
            if (hasSelection() && context())
                context()->setClipboard(selectedText());
            return true;
        }
        break;
    case KeyCode::X:
        if (ctrl)
        {
            if (hasSelection() && context() && !readOnly_)
            {
                context()->setClipboard(selectedText());
                deleteSelection();
            }
            return true;
        }
        break;
    case KeyCode::V:
        if (ctrl)
        {
            if (context() && !readOnly_)
            {
                std::string clip = context()->clipboard();
                clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
                insert(clip);
            }
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

}
