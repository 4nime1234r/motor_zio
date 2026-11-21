#include "zio/ui/Controls.h"

#include <algorithm>
#include <cmath>

#include "zio/ui/Context.h"
#include "zio/ui/Text.h"

namespace zio::ui
{

namespace
{

Color dimmed(Color color)
{
    color.a *= 0.45f;
    return color;
}

}

Label::Label(std::string text) : text_(std::move(text))
{
    setMouseTransparent(true);
}

Vec2 Label::contentMinSize() const
{
    return {font().textWidth(text_), font().lineHeight()};
}

void Label::draw(Painter& painter)
{
    Font& f         = font();
    const float w   = f.textWidth(text_);
    float x         = rect().x;
    if (align_ == Align::Center)
        x = rect().x + (rect().w - w) * 0.5f;
    else if (align_ == Align::End)
        x = rect().right() - w;
    const float y = rect().y + (rect().h - f.lineHeight()) * 0.5f;

    Color color = color_ ? *color_ : theme().text;
    if (!enabledInTree())
        color = dimmed(color);
    painter.text(f, {std::floor(x), std::floor(y)}, text_, color);
}

Button::Button(std::string text) : text_(std::move(text))
{
    setFocusable(true);
}

Vec2 Button::contentMinSize() const
{
    return {font().textWidth(text_) + theme().padding * 2.f, theme().controlHeight};
}

void Button::click()
{
    if (toggle_)
    {
        toggled_ = !toggled_;
        if (onToggled_)
            onToggled_(toggled_);
    }
    else if (onClick_)
    {
        onClick_();
    }
}

void Button::draw(Painter& painter)
{
    const Theme& t       = theme();
    const bool enabled   = enabledInTree();
    const bool inside    = hovered() || pressed_;

    Color fill = t.raised;
    if (toggled_)
        fill = t.accent;
    else if (pressed_ && enabled)
        fill = t.raisedPressed;
    else if (inside && enabled)
        fill = t.raisedHover;
    else if (flat_)
        fill = rgb(0xffffff, 0.f);

    if (!enabled)
        fill = dimmed(fill);

    if (focused())
        painter.roundRectWithBorder(rect(), fill, t.accent, t.radius, 1.f);
    else
        painter.fillRoundRect(rect(), fill, t.radius);

    Font& f       = font();
    const float x = rect().x + (rect().w - f.textWidth(text_)) * 0.5f;
    const float y = rect().y + (rect().h - f.lineHeight()) * 0.5f;
    painter.text(f, {std::floor(x), std::floor(y)}, text_, enabled ? t.text : dimmed(t.text));
}

bool Button::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    pressed_ = true;
    return true;
}

bool Button::onMouseUp(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    const bool fire = pressed_ && rect().contains(event.position);
    pressed_        = false;
    if (fire)
        click();
    return true;
}

bool Button::onKeyDown(const KeyEvent& event)
{
    if (event.code == events::KeyCode::Enter || event.code == events::KeyCode::Space)
    {
        click();
        return true;
    }
    return false;
}

Cursor Button::cursorAt(Vec2) const
{
    return Cursor::Hand;
}

CheckBox::CheckBox(std::string text) : text_(std::move(text))
{
    setFocusable(true);
}

Vec2 CheckBox::contentMinSize() const
{
    const float box = 18.f;
    return {box + theme().spacing + font().textWidth(text_), (std::max)(box, font().lineHeight()) + 8.f};
}

void CheckBox::toggle()
{
    checked_ = !checked_;
    if (onToggled_)
        onToggled_(checked_);
}

void CheckBox::draw(Painter& painter)
{
    const Theme& t     = theme();
    const bool enabled = enabledInTree();
    const float box    = 18.f;
    const Rect boxRect = {rect().x, rect().y + (rect().h - box) * 0.5f, box, box};

    Color fill = checked_ ? t.accent : t.deep;
    if (hovered() && enabled)
        fill = checked_ ? t.accentHover : t.raised;
    if (!enabled)
        fill = dimmed(fill);
    const Color border = focused() ? t.accent : t.borderStrong;
    painter.roundRectWithBorder(boxRect, fill, border, 4.f, 1.f);

    if (checked_)
    {
        const Vec2 a = {boxRect.x + 4.5f, boxRect.y + 9.5f};
        const Vec2 b = {boxRect.x + 7.5f, boxRect.y + 12.5f};
        const Vec2 c = {boxRect.x + 13.5f, boxRect.y + 5.5f};
        painter.line(a, b, t.text, 2.f);
        painter.line(b, c, t.text, 2.f);
    }

    Font& f       = font();
    const float x = boxRect.right() + t.spacing;
    const float y = rect().y + (rect().h - f.lineHeight()) * 0.5f;
    painter.text(f, {std::floor(x), std::floor(y)}, text_, enabled ? t.text : dimmed(t.text));
}

bool CheckBox::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    pressed_ = true;
    return true;
}

bool CheckBox::onMouseUp(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    const bool fire = pressed_ && rect().contains(event.position);
    pressed_        = false;
    if (fire)
        toggle();
    return true;
}

bool CheckBox::onKeyDown(const KeyEvent& event)
{
    if (event.code == events::KeyCode::Enter || event.code == events::KeyCode::Space)
    {
        toggle();
        return true;
    }
    return false;
}

Cursor CheckBox::cursorAt(Vec2) const
{
    return Cursor::Hand;
}

LineEdit::LineEdit()
{
    setFocusable(true);
}

void LineEdit::setText(std::string text)
{
    text_   = std::move(text);
    caret_  = (std::min)(caret_, text_.size());
    anchor_ = caret_;
    scrollX_ = 0.f;
}

std::string LineEdit::selectedText() const
{
    const std::size_t from = (std::min)(caret_, anchor_);
    const std::size_t to   = (std::max)(caret_, anchor_);
    return text_.substr(from, to - from);
}

void LineEdit::selectAll()
{
    anchor_ = 0;
    caret_  = text_.size();
}

Vec2 LineEdit::contentMinSize() const
{
    return {theme().padding * 2.f + 40.f, theme().controlHeight};
}

Rect LineEdit::textArea() const
{
    return rect().shrink(theme().padding, 0.f, theme().padding, 0.f);
}

float LineEdit::widthOf(std::size_t index) const
{
    return font().textWidth(std::string_view(text_).substr(0, (std::min)(index, text_.size())));
}

std::size_t LineEdit::indexAtX(float x) const
{
    Font& f           = font();
    float accumulated = 0.f;
    std::size_t index = 0;
    while (index < text_.size())
    {
        std::size_t next    = index;
        const char32_t code = text::decode(text_, next);
        const float advance = f.advance(code);
        if (x < accumulated + advance * 0.5f)
            return index;
        accumulated += advance;
        index = next;
    }
    return text_.size();
}

void LineEdit::restartBlink()
{
    blinkBase_ = context() ? context()->time() : 0.0;
}

void LineEdit::ensureCaretVisible()
{
    const float width = textArea().w;
    const float x     = widthOf(caret_);
    if (x - scrollX_ > width - 2.f)
        scrollX_ = x - width + 2.f;
    if (x - scrollX_ < 0.f)
        scrollX_ = x;
    const float total = font().textWidth(text_);
    scrollX_          = std::clamp(scrollX_, 0.f, (std::max)(0.f, total - width + 2.f));
}

void LineEdit::moveCaret(std::size_t index, bool extend)
{
    caret_ = (std::min)(index, text_.size());
    if (!extend)
        anchor_ = caret_;
    restartBlink();
    ensureCaretVisible();
}

void LineEdit::changed()
{
    restartBlink();
    ensureCaretVisible();
    if (onChanged_)
        onChanged_(text_);
}

void LineEdit::eraseRange(std::size_t from, std::size_t to)
{
    if (from >= to)
        return;
    text_.erase(from, to - from);
    caret_  = from;
    anchor_ = from;
    changed();
}

void LineEdit::deleteSelection()
{
    eraseRange((std::min)(caret_, anchor_), (std::max)(caret_, anchor_));
}

void LineEdit::insert(const std::string& utf8)
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

void LineEdit::onFocusChanged(bool focused)
{
    if (focused)
        restartBlink();
    else
    {
        dragging_ = false;
        anchor_   = caret_;
    }
}

Cursor LineEdit::cursorAt(Vec2) const
{
    return Cursor::IBeam;
}

void LineEdit::draw(Painter& painter)
{
    const Theme& t      = theme();
    const bool enabled  = enabledInTree();
    const Color border  = focused() ? t.accent : (hovered() ? t.borderStrong : t.border);
    painter.roundRectWithBorder(rect(), enabled ? t.deep : dimmed(t.deep), border, t.radius, 1.f);

    Font& f           = font();
    const Rect area   = textArea();
    const float textY = std::floor(rect().y + (rect().h - f.lineHeight()) * 0.5f);
    const float originX = area.x - scrollX_;

    painter.pushClip(area);
    if (hasSelection() && focused())
    {
        const float x0 = originX + widthOf((std::min)(caret_, anchor_));
        const float x1 = originX + widthOf((std::max)(caret_, anchor_));
        painter.fillRect({x0, textY, x1 - x0, f.lineHeight()}, t.selection);
    }

    if (text_.empty())
    {
        if (!placeholder_.empty())
            painter.text(f, {std::floor(area.x), textY}, placeholder_, t.textDim);
    }
    else
    {
        painter.text(f, {std::floor(originX), textY}, text_, enabled ? t.text : dimmed(t.text));
    }

    if (focused() && enabled && !readOnly_ && context())
    {
        const double phase = context()->time() - blinkBase_;
        if (std::fmod(phase, 1.0) < 0.55)
            painter.fillRect({std::floor(originX + widthOf(caret_)), textY, 1.5f, f.lineHeight()}, t.text);
    }
    painter.popClip();
}

bool LineEdit::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;

    const std::size_t index = indexAtX(event.position.x - textArea().x + scrollX_);
    if (event.clicks >= 3)
    {
        selectAll();
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

bool LineEdit::onMouseUp(const MouseEvent& event)
{
    (void)event;
    dragging_ = false;
    return true;
}

bool LineEdit::onMouseMove(Vec2 position)
{
    if (!dragging_)
        return false;
    moveCaret(indexAtX(position.x - textArea().x + scrollX_), true);
    return true;
}

bool LineEdit::onChar(char32_t codepoint)
{
    if (readOnly_ || codepoint < 32 || codepoint == 127)
        return false;
    std::string utf8;
    text::append(utf8, codepoint);
    insert(utf8);
    return true;
}

bool LineEdit::onKeyDown(const KeyEvent& event)
{
    using events::KeyCode;
    const bool ctrl  = event.modifiers.ctrl;
    const bool shift = event.modifiers.shift;

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
    case KeyCode::Home:
        moveCaret(0, shift);
        return true;
    case KeyCode::End:
        moveCaret(text_.size(), shift);
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
        if (onSubmit_)
            onSubmit_(text_);
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
                clip.erase(std::remove_if(clip.begin(), clip.end(), [](char c) { return c == '\r' || c == '\n'; }),
                           clip.end());
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

Separator::Separator(bool vertical) : vertical_(vertical)
{
    setMouseTransparent(true);
}

void Separator::draw(Painter& painter)
{
    const Rect r = rect();
    if (vertical_)
        painter.fillRect({std::floor(r.x + r.w * 0.5f), r.y, 1.f, r.h}, theme().borderStrong);
    else
        painter.fillRect({r.x, std::floor(r.y + r.h * 0.5f), r.w, 1.f}, theme().borderStrong);
}

void ProgressBar::draw(Painter& painter)
{
    const Theme& t = theme();
    painter.fillRoundRect(rect(), t.deep, rect().h * 0.5f);
    if (value_ > 0.f)
    {
        Rect fill = rect();
        fill.w    = (std::max)(rect().h, rect().w * value_);
        painter.fillRoundRect(fill, t.accent, rect().h * 0.5f);
    }
}

}
