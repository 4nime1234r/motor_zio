#pragma once

#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Containers.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

enum class DockSide : std::uint8_t
{
    Left,
    Right,
    Bottom
};

class ZIO_API PanelFrame final : public Widget
{
public:
    explicit PanelFrame(std::string title);

    const std::string& title() const { return title_; }
    void setContent(std::unique_ptr<Widget> content);
    Widget* content() const { return content_; }

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;

private:
    float headerHeight() const;

    std::string title_;
    Widget* content_ = nullptr;
};

class ZIO_API DockSpace final : public Widget
{
public:
    DockSpace();

    void setCenter(std::unique_ptr<Widget> center);
    Widget* center() const { return centerRaw_; }

    Widget* addPanel(std::string id, std::string title, std::unique_ptr<Widget> content, DockSide side);
    void movePanel(const std::string& id, DockSide side);
    bool hasPanel(const std::string& id) const;
    DockSide sideOf(const std::string& id) const;
    Widget* panelContent(const std::string& id) const;
    std::vector<std::string> panelIds() const;

    void setFractions(float left, float right, float bottom);
    float leftFraction();
    float rightFraction();
    float bottomFraction();

    std::string saveLayout();
    void loadLayout(const std::string& text);

    Vec2 contentMinSize() const override { return {0.f, 0.f}; }
    void layout() override;

private:
    struct Entry
    {
        std::string id;
        std::string title;
        DockSide side = DockSide::Left;
        Widget* raw   = nullptr;
        std::unique_ptr<Widget> owned;
    };

    Entry* find(const std::string& id);
    const Entry* find(const std::string& id) const;
    void syncFractions();
    void reclaim();
    void rebuild();
    std::unique_ptr<Widget> buildStack(const std::vector<Entry*>& list, std::size_t index);

    std::vector<Entry> entries_;
    Widget* centerRaw_ = nullptr;
    std::unique_ptr<Widget> centerOwned_;
    Widget* built_              = nullptr;
    SplitContainer* outerSplit_ = nullptr;
    SplitContainer* innerSplit_ = nullptr;
    SplitContainer* bottomSplit_ = nullptr;
    bool hasLeft_   = false;
    bool hasRight_  = false;
    bool hasBottom_ = false;
    bool dirty_     = true;
    bool skipSync_  = false;
    float leftFraction_   = 0.22f;
    float rightFraction_  = 0.24f;
    float bottomFraction_ = 0.26f;
};

}
