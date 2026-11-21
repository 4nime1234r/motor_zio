#pragma once

#include <functional>
#include <string>

#include "zio/core/Platform.h"
#include "zio/ui/Controls.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

struct Hsv
{
    float h = 0.f;
    float s = 0.f;
    float v = 0.f;
};

ZIO_API Hsv toHsv(Color color);
ZIO_API Color fromHsv(Hsv hsv, float alpha = 1.f);
ZIO_API std::string toHex(Color color, bool withAlpha);
ZIO_API bool parseHex(const std::string& text, Color& out);

class ZIO_API PopupPanel final : public Widget
{
public:
    void setAnchor(Vec2 position) { anchor_ = position; }
    void setPadding(float padding) { padding_ = padding; }
    void setOnClose(std::function<void()> callback) { onClose_ = std::move(callback); }

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    void onPopupClosed() override;

private:
    Vec2 anchor_ = {0.f, 0.f};
    float padding_ = 10.f;
    std::function<void()> onClose_;
};

class ZIO_API ColorPicker final : public Widget
{
public:
    ColorPicker();

    void setColor(Color color);
    Color color() const;
    void setAlphaEnabled(bool enabled);
    void setOnChanged(std::function<void(Color)> callback) { onChanged_ = std::move(callback); }
    Hsv hsv() const { return hsv_; }

    Rect squareRect() const;
    Rect hueRect() const;
    Rect alphaRect() const;

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    void pick(Vec2 position);
    void changed();
    void refreshHex();
    void applyHex(const std::string& text);

    Hsv hsv_;
    float alpha_ = 1.f;
    bool alphaEnabled_ = true;
    int dragging_ = 0;
    LineEdit* hex_ = nullptr;
    std::function<void(Color)> onChanged_;
};

class ZIO_API ColorButton final : public Widget
{
public:
    ColorButton();
    ~ColorButton() override;

    void setColor(Color color) { color_ = color; }
    Color color() const { return color_; }
    void setAlphaEnabled(bool enabled) { alphaEnabled_ = enabled; }
    void setOnChanged(std::function<void(Color)> callback) { onChanged_ = std::move(callback); }

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    void openPicker();

    Color color_       = Color::white();
    bool alphaEnabled_ = true;
    bool open_         = false;
    std::function<void(Color)> onChanged_;
};

}
