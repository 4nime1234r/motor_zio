#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Controls.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

class ZIO_API Dialog final : public Widget
{
public:
    explicit Dialog(std::string title);

    void setContent(std::unique_ptr<Widget> content);
    Button* addButton(std::string text, std::function<void()> onPress, bool primary = false);
    void setWidth(float width) { width_ = width; }
    void setOnCancel(std::function<void()> callback) { onCancel_ = std::move(callback); }
    void setOnConfirm(std::function<void()> callback) { onConfirm_ = std::move(callback); }
    void setOnClosed(std::function<void()> callback) { onClosed_ = std::move(callback); }

    void close();
    void cancel();
    void confirm();
    Rect boxRect() const { return box_; }

    Vec2 contentMinSize() const override { return {0.f, 0.f}; }
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    void onPopupClosed() override;

private:
    float titleHeight() const;
    float footerHeight() const;

    std::string title_;
    Widget* content_ = nullptr;
    std::vector<Button*> buttons_;
    float width_ = 440.f;
    Rect box_;
    std::function<void()> onCancel_;
    std::function<void()> onConfirm_;
    std::function<void()> onClosed_;
};

}
