#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/ui/Widget.h"

namespace zio::ui
{

struct MenuItem
{
    std::string text;
    std::string shortcut;
    bool separator = false;
    bool enabled   = true;
    bool checkable = false;
    bool checked   = false;
    std::function<void()> action;
    std::vector<MenuItem> submenu;

    static MenuItem item(std::string text, std::function<void()> action = {}, std::string shortcut = {})
    {
        MenuItem m;
        m.text     = std::move(text);
        m.action   = std::move(action);
        m.shortcut = std::move(shortcut);
        return m;
    }

    static MenuItem line()
    {
        MenuItem m;
        m.separator = true;
        return m;
    }

    static MenuItem check(std::string text, bool checked, std::function<void()> action = {})
    {
        MenuItem m;
        m.text      = std::move(text);
        m.checkable = true;
        m.checked   = checked;
        m.action    = std::move(action);
        return m;
    }

    static MenuItem submenuItem(std::string text, std::vector<MenuItem> items)
    {
        MenuItem m;
        m.text    = std::move(text);
        m.submenu = std::move(items);
        return m;
    }
};

class ZIO_API PopupMenu final : public Widget
{
public:
    explicit PopupMenu(std::vector<MenuItem> items);

    void setAnchor(Vec2 position) { anchor_ = position; }
    void setFlipX(float rightEdgeOfParent) { flipX_ = rightEdgeOfParent; }
    void setMinWidth(float width) { minWidth_ = width; }
    void setOnClose(std::function<void()> callback) { onClose_ = std::move(callback); }
    void notifyClosed();
    int hoveredIndex() const { return hover_; }
    const std::vector<MenuItem>& items() const { return items_; }
    Rect itemRect(std::size_t index) const;

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseUp(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;
    bool onKeyDown(const KeyEvent& event) override;
    void onMouseLeave() override;

private:
    float itemHeight() const;
    float separatorHeight() const;
    int itemAt(Vec2 position) const;
    void openSubmenu(int index);
    void activate(int index);
    void moveHover(int direction);

    std::vector<MenuItem> items_;
    Vec2 anchor_ = {0.f, 0.f};
    float flipX_ = -1.f;
    float minWidth_ = 0.f;
    int hover_ = -1;
    int openSub_ = -1;
    std::function<void()> onClose_;
};

class ZIO_API MenuBar final : public Widget
{
public:
    ~MenuBar() override;

    void addMenu(std::string title, std::function<std::vector<MenuItem>()> builder);
    void addMenu(std::string title, std::vector<MenuItem> items);
    int openIndex() const { return openIndex_; }
    void openMenu(int index);
    Rect titleRect(std::size_t index) const;

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onMouseMove(Vec2 position) override;

private:
    struct Entry
    {
        std::string title;
        std::function<std::vector<MenuItem>()> builder;
    };

    int titleAt(Vec2 position) const;

    std::vector<Entry> entries_;
    int openIndex_ = -1;
};

class ZIO_API OptionButton final : public Widget
{
public:
    OptionButton();
    ~OptionButton() override;

    void setItems(std::vector<std::string> items);
    const std::vector<std::string>& items() const { return items_; }
    int selected() const { return selected_; }
    void setSelected(int index);
    void setOnSelected(std::function<void(int)> callback) { onSelected_ = std::move(callback); }

    Vec2 contentMinSize() const override;
    void draw(Painter& painter) override;
    bool onMouseDown(const MouseEvent& event) override;
    bool onKeyDown(const KeyEvent& event) override;
    Cursor cursorAt(Vec2 position) const override;

private:
    void openList();

    std::vector<std::string> items_;
    int selected_ = -1;
    bool open_    = false;
    std::function<void(int)> onSelected_;
};

class ZIO_API Tooltip final : public Widget
{
public:
    Tooltip(std::string text, Vec2 position) : text_(std::move(text)), position_(position)
    {
        setMouseTransparent(true);
    }

    Vec2 contentMinSize() const override;
    void layout() override;
    void draw(Painter& painter) override;

private:
    std::string text_;
    Vec2 position_;
};

}
