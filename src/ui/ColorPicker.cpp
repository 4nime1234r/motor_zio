#include "zio/ui/ColorPicker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "zio/ui/Context.h"

namespace zio::ui
{

namespace
{

constexpr float kPad        = 10.f;
constexpr float kSquare     = 176.f;
constexpr float kHueWidth   = 18.f;
constexpr float kAlphaHeight = 16.f;
constexpr float kHexHeight  = 30.f;
constexpr int kSquareCells  = 28;
constexpr int kHueCells     = 48;
constexpr int kAlphaCells   = 32;

void drawChecker(Painter& painter, const Rect& r)
{
    constexpr float kCell = 8.f;
    const Color light     = rgb(0x4a4f63);
    const Color dark      = rgb(0x2a2d3a);
    painter.fillRect(r, dark);
    painter.pushClip(r);
    int row = 0;
    for (float y = r.y; y < r.bottom(); y += kCell, ++row)
    {
        int column = row % 2;
        for (float x = r.x + static_cast<float>(column) * kCell; x < r.right(); x += kCell * 2.f)
            painter.fillRect({x, y, kCell, kCell}, light);
    }
    painter.popClip();
}

}

Hsv toHsv(Color color)
{
    const float r = color.r;
    const float g = color.g;
    const float b = color.b;
    const float maximum = (std::max)({r, g, b});
    const float minimum = (std::min)({r, g, b});
    const float delta   = maximum - minimum;

    Hsv result;
    result.v = maximum;
    result.s = maximum > 0.f ? delta / maximum : 0.f;
    if (delta <= 0.f)
    {
        result.h = 0.f;
        return result;
    }
    float hue = 0.f;
    if (maximum == r)
        hue = std::fmod((g - b) / delta, 6.f);
    else if (maximum == g)
        hue = (b - r) / delta + 2.f;
    else
        hue = (r - g) / delta + 4.f;
    hue /= 6.f;
    if (hue < 0.f)
        hue += 1.f;
    result.h = hue;
    return result;
}

Color fromHsv(Hsv hsv, float alpha)
{
    const float h = (hsv.h - std::floor(hsv.h)) * 6.f;
    const float c = hsv.v * hsv.s;
    const float x = c * (1.f - std::fabs(std::fmod(h, 2.f) - 1.f));
    const float m = hsv.v - c;
    float r = 0.f, g = 0.f, b = 0.f;
    if (h < 1.f)
    {
        r = c;
        g = x;
    }
    else if (h < 2.f)
    {
        r = x;
        g = c;
    }
    else if (h < 3.f)
    {
        g = c;
        b = x;
    }
    else if (h < 4.f)
    {
        g = x;
        b = c;
    }
    else if (h < 5.f)
    {
        r = x;
        b = c;
    }
    else
    {
        r = c;
        b = x;
    }
    return {r + m, g + m, b + m, alpha};
}

std::string toHex(Color color, bool withAlpha)
{
    auto byte = [](float v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
    char buffer[16];
    if (withAlpha)
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b), byte(color.a));
    else
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b));
    return buffer;
}

bool parseHex(const std::string& text, Color& out)
{
    std::string digits = text;
    if (!digits.empty() && digits[0] == '#')
        digits.erase(0, 1);
    if (digits.size() != 6 && digits.size() != 8)
        return false;
    for (char c : digits)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    const unsigned long value = std::strtoul(digits.c_str(), nullptr, 16);
    if (digits.size() == 6)
        out = rgb(static_cast<std::uint32_t>(value));
    else
        out = rgb(static_cast<std::uint32_t>(value >> 8), static_cast<float>(value & 0xFF) / 255.f);
    return true;
}

Vec2 PopupPanel::contentMinSize() const
{
    Vec2 size = {0.f, 0.f};
    for (const auto& child : children())
    {
        const Vec2 m = child->minSize();
        size.x       = (std::max)(size.x, m.x);
        size.y       = (std::max)(size.y, m.y);
    }
    return {size.x + padding_ * 2.f, size.y + padding_ * 2.f};
}

void PopupPanel::layout()
{
    const Vec2 size = minSize();
    Vec2 pos        = anchor_;
    if (context())
    {
        const Vec2 window = context()->size();
        if (pos.x + size.x > window.x)
            pos.x = window.x - size.x;
        if (pos.y + size.y > window.y)
            pos.y = window.y - size.y;
    }
    pos.x = (std::max)(0.f, pos.x);
    pos.y = (std::max)(0.f, pos.y);
    setRect({pos.x, pos.y, size.x, size.y});

    const Rect inner = rect().shrink(padding_);
    for (const auto& child : children())
        child->setRect(inner);
}

void PopupPanel::draw(Painter& painter)
{
    painter.roundRectWithBorder(rect(), theme().raised, theme().borderStrong, theme().radius + 2.f, 1.f);
}

bool PopupPanel::onMouseDown(const MouseEvent& event)
{
    (void)event;
    return true;
}

bool PopupPanel::onKeyDown(const KeyEvent& event)
{
    if (event.code == events::KeyCode::Escape && context())
    {
        context()->closePopups();
        return true;
    }
    return false;
}

void PopupPanel::onPopupClosed()
{
    if (onClose_)
    {
        auto callback = std::move(onClose_);
        onClose_      = nullptr;
        callback();
    }
}

ColorPicker::ColorPicker()
{
    hsv_ = {0.65f, 0.6f, 0.95f};
    hex_ = add<LineEdit>();
    hex_->setOnSubmit([this](const std::string& text) { applyHex(text); });
    hex_->setOnFocusLost([this]() { applyHex(hex_->text()); });
    refreshHex();
}

void ColorPicker::setAlphaEnabled(bool enabled)
{
    alphaEnabled_ = enabled;
    refreshHex();
}

void ColorPicker::setColor(Color color)
{
    Hsv next = toHsv(color);
    if (next.s <= 0.f || next.v <= 0.f)
        next.h = hsv_.h;
    hsv_   = next;
    alpha_ = color.a;
    refreshHex();
}

Color ColorPicker::color() const
{
    return fromHsv(hsv_, alphaEnabled_ ? alpha_ : 1.f);
}

void ColorPicker::refreshHex()
{
    hex_->setText(toHex(color(), alphaEnabled_));
}

void ColorPicker::applyHex(const std::string& text)
{
    Color parsed;
    if (parseHex(text, parsed))
    {
        setColor(parsed);
        if (onChanged_)
            onChanged_(color());
    }
    else
    {
        refreshHex();
    }
}

void ColorPicker::changed()
{
    refreshHex();
    if (onChanged_)
        onChanged_(color());
}

Rect ColorPicker::squareRect() const
{
    return {rect().x + kPad, rect().y + kPad, kSquare, kSquare};
}

Rect ColorPicker::hueRect() const
{
    const Rect s = squareRect();
    return {s.right() + kPad, s.y, kHueWidth, s.h};
}

Rect ColorPicker::alphaRect() const
{
    const Rect s = squareRect();
    return {s.x, s.bottom() + kPad, kSquare + kPad + kHueWidth, kAlphaHeight};
}

Vec2 ColorPicker::contentMinSize() const
{
    float height = kPad + kSquare + kPad + kHexHeight;
    if (alphaEnabled_)
        height += kPad + kAlphaHeight;
    return {kPad * 2.f + kSquare + kPad + kHueWidth, height + kPad};
}

void ColorPicker::layout()
{
    const Rect anchor = alphaEnabled_ ? alphaRect() : squareRect();
    hex_->setRect({rect().x + kPad, anchor.bottom() + kPad, rect().w - kPad * 2.f, kHexHeight});
}

Cursor ColorPicker::cursorAt(Vec2 position) const
{
    if (squareRect().contains(position) || hueRect().contains(position) || (alphaEnabled_ && alphaRect().contains(position)))
        return Cursor::Hand;
    return Cursor::Arrow;
}

void ColorPicker::draw(Painter& painter)
{
    const Rect square = squareRect();
    const float cellW = square.w / static_cast<float>(kSquareCells);
    const float cellH = square.h / static_cast<float>(kSquareCells);
    for (int j = 0; j < kSquareCells; ++j)
    {
        for (int i = 0; i < kSquareCells; ++i)
        {
            const float s = static_cast<float>(i) / static_cast<float>(kSquareCells - 1);
            const float v = 1.f - static_cast<float>(j) / static_cast<float>(kSquareCells - 1);
            const float x0 = square.x + static_cast<float>(i) * cellW;
            const float y0 = square.y + static_cast<float>(j) * cellH;
            const float x1 = square.x + static_cast<float>(i + 1) * cellW;
            const float y1 = square.y + static_cast<float>(j + 1) * cellH;
            painter.fillRect({x0, y0, x1 - x0, y1 - y0}, fromHsv({hsv_.h, s, v}));
        }
    }

    const Rect hue    = hueRect();
    const float hueH  = hue.h / static_cast<float>(kHueCells);
    for (int i = 0; i < kHueCells; ++i)
    {
        const float h  = static_cast<float>(i) / static_cast<float>(kHueCells);
        const float y0 = hue.y + static_cast<float>(i) * hueH;
        const float y1 = hue.y + static_cast<float>(i + 1) * hueH;
        painter.fillRect({hue.x, y0, hue.w, y1 - y0}, fromHsv({h, 1.f, 1.f}));
    }

    const Color solid = fromHsv(hsv_, 1.f);
    if (alphaEnabled_)
    {
        const Rect alpha = alphaRect();
        drawChecker(painter, alpha);
        const float cell = alpha.w / static_cast<float>(kAlphaCells);
        for (int i = 0; i < kAlphaCells; ++i)
        {
            const float a  = static_cast<float>(i) / static_cast<float>(kAlphaCells - 1);
            const float x0 = alpha.x + static_cast<float>(i) * cell;
            const float x1 = alpha.x + static_cast<float>(i + 1) * cell;
            painter.fillRect({x0, alpha.y, x1 - x0, alpha.h}, Color{solid.r, solid.g, solid.b, a});
        }
        const float ax = alpha.x + alpha_ * alpha.w;
        painter.roundRectWithBorder({ax - 3.f, alpha.y - 2.f, 6.f, alpha.h + 4.f}, Color{solid.r, solid.g, solid.b, alpha_},
                                    theme().text, 2.f, 1.5f);
    }

    const float sx = square.x + hsv_.s * square.w;
    const float sy = square.y + (1.f - hsv_.v) * square.h;
    painter.roundRectWithBorder({sx - 7.f, sy - 7.f, 14.f, 14.f}, solid, theme().text, 7.f, 2.f);

    const float hy = hue.y + hsv_.h * hue.h;
    painter.roundRectWithBorder({hue.x - 3.f, hy - 3.f, hue.w + 6.f, 6.f}, fromHsv({hsv_.h, 1.f, 1.f}), theme().text, 3.f, 1.5f);
}

void ColorPicker::pick(Vec2 position)
{
    if (dragging_ == 1)
    {
        const Rect s = squareRect();
        hsv_.s       = std::clamp((position.x - s.x) / s.w, 0.f, 1.f);
        hsv_.v       = 1.f - std::clamp((position.y - s.y) / s.h, 0.f, 1.f);
    }
    else if (dragging_ == 2)
    {
        const Rect h = hueRect();
        hsv_.h       = std::clamp((position.y - h.y) / h.h, 0.f, 0.9999f);
    }
    else if (dragging_ == 3)
    {
        const Rect a = alphaRect();
        alpha_       = std::clamp((position.x - a.x) / a.w, 0.f, 1.f);
    }
    changed();
}

bool ColorPicker::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    if (squareRect().contains(event.position))
        dragging_ = 1;
    else if (hueRect().contains(event.position))
        dragging_ = 2;
    else if (alphaEnabled_ && alphaRect().contains(event.position))
        dragging_ = 3;
    else
        return false;
    pick(event.position);
    return true;
}

bool ColorPicker::onMouseMove(Vec2 position)
{
    if (dragging_ == 0)
        return false;
    pick(position);
    return true;
}

bool ColorPicker::onMouseUp(const MouseEvent& event)
{
    (void)event;
    const bool was = dragging_ != 0;
    dragging_      = 0;
    return was;
}

ColorButton::ColorButton()
{
    setFocusable(true);
}

ColorButton::~ColorButton()
{
    if (context())
        context()->closePopups();
}

Vec2 ColorButton::contentMinSize() const
{
    return {48.f, theme().controlHeight};
}

Cursor ColorButton::cursorAt(Vec2) const
{
    return Cursor::Hand;
}

void ColorButton::draw(Painter& painter)
{
    const Theme& t = theme();
    const Color border = focused() ? t.accent : (hovered() || open_ ? t.borderStrong : t.border);
    painter.roundRectWithBorder(rect(), t.deep, border, t.radius, 1.f);

    const Rect swatch = rect().shrink(4.f);
    if (swatch.empty())
        return;
    if (color_.a < 1.f)
    {
        painter.pushClip(swatch);
        drawChecker(painter, swatch);
        painter.popClip();
    }
    painter.fillRoundRect(swatch, color_, t.radius - 1.f);
}

void ColorButton::openPicker()
{
    if (!context())
        return;
    context()->closePopups();

    auto panel  = std::make_unique<PopupPanel>();
    auto* picker = panel->add<ColorPicker>();
    picker->setAlphaEnabled(alphaEnabled_);
    picker->setColor(color_);
    picker->setOnChanged([this](Color c) {
        color_ = c;
        if (onChanged_)
            onChanged_(c);
    });
    panel->setAnchor({rect().x, rect().bottom() + 2.f});
    panel->setOnClose([this]() { open_ = false; });
    context()->openPopup(std::move(panel));
    open_ = true;
}

bool ColorButton::onMouseDown(const MouseEvent& event)
{
    if (event.button != events::MouseButton::Left)
        return false;
    if (open_)
        context()->closePopups();
    else
        openPicker();
    return true;
}

bool ColorButton::onKeyDown(const KeyEvent& event)
{
    if (event.code == events::KeyCode::Enter || event.code == events::KeyCode::Space)
    {
        openPicker();
        return true;
    }
    return false;
}

}
