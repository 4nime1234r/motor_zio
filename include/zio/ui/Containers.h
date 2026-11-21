#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

class ZIO_API BoxContainer : public Widget
{
public:
    explicit BoxContainer(bool vertical = true) : vertical_(vertical) { setMouseTransparent(true); }

    void setSeparation(float separation) { separation_ = separation; }
    float separation() const;
    void setPadding(Edges padding) { padding_ = padding; }
    const Edges& padding() const { return padding_; }
    bool vertical() const { return vertical_; }

    Vec2 contentMinSize() const override;
    void layout() override;

private:
    bool vertical_;
    float separation_ = -1.f;
    Edges padding_;
};

class ZIO_API VBox final : public BoxContainer
{
public:
    VBox() : BoxContainer(true) {}
};

class ZIO_API HBox final : public BoxContainer
{
public:
    HBox() : BoxContainer(false) {}
};

class ZIO_API Panel : public Widget
{
public:
    void setPadding(Edges padding) { padding_ = padding; }
    const Edges& padding() const { return padding_; }
    void setBackground(std::optional<Color> color) { background_ = color; }
    void setBorder(std::optional<Color> color, float thickness = 1.f)
    {
        border_          = color;
        borderThickness_ = thickness;
    }
    void setRadius(float radius) { radius_ = radius; }
    void setTransparent(bool transparent) { transparent_ = transparent; }

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;

private:
    Edges padding_;
    std::optional<Color> background_;
    std::optional<Color> border_;
    float borderThickness_ = 1.f;
    float radius_          = 0.f;
    bool transparent_      = false;
};

class ZIO_API ScrollContainer final : public Widget
{
public:
    explicit ScrollContainer(bool vertical = true, bool horizontal = false);

    Vec2 scroll() const { return scroll_; }
    void setScroll(Vec2 scroll);
    void ensureVisible(const Rect& target);
    Vec2 contentSize() const { return contentSize_; }

    Vec2 contentMinSize() const override { return {0.f, 0.f}; }
    void layout() override;
    void drawForeground(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onMouseScroll(Vec2 position, Vec2 delta, Modifiers modifiers) override;

private:
    Rect verticalTrack() const;
    Rect verticalThumb() const;
    Rect horizontalTrack() const;
    Rect horizontalThumb() const;
    void clampScroll();

    bool vertical_;
    bool horizontal_;
    Vec2 scroll_      = {0.f, 0.f};
    Vec2 contentSize_ = {0.f, 0.f};
    Rect viewport_;
    bool showV_       = false;
    bool showH_       = false;
    int dragging_     = 0;
    float dragOffset_ = 0.f;
};

class ZIO_API SplitContainer final : public Widget
{
public:
    explicit SplitContainer(bool vertical = false) : vertical_(vertical) {}

    void setRatio(float ratio) { ratio_ = ratio; }
    float ratio() const { return ratio_; }

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    Rect dividerRect() const;

    bool vertical_;
    float ratio_      = 0.5f;
    float firstSize_  = 0.f;
    Vec2 lastPosition_;
    bool dragging_    = false;
};

class ZIO_API TabContainer final : public Widget
{
public:
    Widget* addPage(std::unique_ptr<Widget> page, std::string title);
    void setCurrent(std::size_t index);
    std::size_t current() const { return current_; }
    void setOnChanged(std::function<void(std::size_t)> callback) { onChanged_ = std::move(callback); }

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;

private:
    Rect tabRect(std::size_t index) const;
    float headerHeight() const;

    std::vector<std::string> titles_;
    std::size_t current_ = 0;
    std::function<void(std::size_t)> onChanged_;
};

}
