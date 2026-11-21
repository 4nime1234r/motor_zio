#include "zio/ui/Dialog.h"

#include <algorithm>
#include <cmath>

#include "zio/ui/Context.h"

namespace zio::ui
{

Dialog::Dialog(std::string title) : title_(std::move(title)) {}

float Dialog::titleHeight() const
{
    return 44.f;
}

float Dialog::footerHeight() const
{
    return theme().controlHeight + 24.f;
}

void Dialog::setContent(std::unique_ptr<Widget> content)
{
    content_ = addChild(std::move(content));
}

Button* Dialog::addButton(std::string text, std::function<void()> onPress, bool primary)
{
    auto* button = add<Button>(std::move(text));
    button->setOnClick(std::move(onPress));
    button->setPrimary(primary);
    buttons_.push_back(button);
    return button;
}

void Dialog::close()
{
    if (context())
        context()->closePopup(this);
}

void Dialog::cancel()
{
    auto callback = onCancel_;
    close();
    if (callback)
        callback();
}

void Dialog::confirm()
{
    auto callback = onConfirm_;
    if (callback)
        callback();
}

void Dialog::layout()
{
    const Vec2 window = context() ? context()->size() : Vec2{800.f, 600.f};
    setRect({0.f, 0.f, window.x, window.y});

    const Vec2 contentSize = content_ ? content_->minSize() : Vec2{0.f, 0.f};
    float buttonsWidth     = 16.f;
    for (Button* b : buttons_)
        buttonsWidth += b->minSize().x + 8.f;
    const float width  = (std::max)({width_, contentSize.x + 40.f, buttonsWidth + 24.f});
    const float height = titleHeight() + contentSize.y + 20.f + footerHeight();

    box_ = {std::floor((window.x - width) * 0.5f), std::floor((window.y - height) * 0.4f), width, height};

    if (content_)
        content_->setRect({box_.x + 20.f, box_.y + titleHeight() + 4.f, width - 40.f, contentSize.y});

    float x = box_.right() - 16.f;
    const float y = box_.bottom() - footerHeight() + 12.f;
    for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it)
    {
        const Vec2 size = (*it)->minSize();
        x -= size.x;
        (*it)->setRect({x, y, size.x, theme().controlHeight});
        x -= 8.f;
    }
}

void Dialog::draw(Painter& painter)
{
    const Theme& t = theme();
    painter.fillRect(rect(), Color{0.f, 0.f, 0.f, 0.55f});
    painter.roundRectWithBorder(box_, t.raised, t.borderStrong, t.radius + 3.f, 1.f);

    Font& f = theme().fontBold ? *theme().fontBold : font();
    painter.text(f, {std::floor(box_.x + 20.f), std::floor(box_.y + (titleHeight() - f.lineHeight()) * 0.5f + 2.f)}, title_,
                 t.text);
    painter.fillRect({box_.x, box_.y + titleHeight(), box_.w, 1.f}, t.border);
}

void Dialog::onPopupClosed()
{
    if (onClosed_)
    {
        auto callback = std::move(onClosed_);
        onClosed_     = nullptr;
        callback();
    }
}

bool Dialog::onMouseDown(const MouseEvent&)
{
    return true;
}

bool Dialog::onKeyDown(const KeyEvent& event)
{
    if (event.code == events::KeyCode::Escape)
    {
        cancel();
        return true;
    }
    if (event.code == events::KeyCode::Enter && onConfirm_)
    {
        confirm();
        return true;
    }
    return false;
}

}
