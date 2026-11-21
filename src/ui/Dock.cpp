#include "zio/ui/Dock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace zio::ui
{

namespace
{

const char* sideName(DockSide side)
{
    switch (side)
    {
    case DockSide::Left:
        return "izquierda";
    case DockSide::Right:
        return "derecha";
    case DockSide::Bottom:
        return "abajo";
    }
    return "izquierda";
}

bool parseSide(const std::string& text, DockSide& out)
{
    for (DockSide side : {DockSide::Left, DockSide::Right, DockSide::Bottom})
        if (text == sideName(side))
        {
            out = side;
            return true;
        }
    return false;
}

}

PanelFrame::PanelFrame(std::string title) : title_(std::move(title)) {}

float PanelFrame::headerHeight() const
{
    return font().lineHeight() + 14.f;
}

void PanelFrame::setContent(std::unique_ptr<Widget> content)
{
    content_ = addChild(std::move(content));
}

Vec2 PanelFrame::contentMinSize() const
{
    Vec2 size = content_ ? content_->minSize() : Vec2{0.f, 0.f};
    size.y += headerHeight();
    return size;
}

void PanelFrame::layout()
{
    if (content_)
        content_->setRect({rect().x, rect().y + headerHeight(), rect().w, (std::max)(0.f, rect().h - headerHeight())});
}

void PanelFrame::draw(Painter& painter)
{
    const Theme& t = theme();
    painter.fillRect(rect(), t.panel);
    const Rect header = {rect().x, rect().y, rect().w, headerHeight()};
    painter.fillRect(header, t.deep);
    painter.fillRect({header.x, header.bottom() - 1.f, header.w, 1.f}, t.border);
    painter.text(font(), {std::floor(header.x + 12.f), std::floor(header.y + (header.h - font().lineHeight()) * 0.5f)},
                 title_, t.textDim);
}

DockSpace::DockSpace() {}

DockSpace::Entry* DockSpace::find(const std::string& id)
{
    for (Entry& e : entries_)
        if (e.id == id)
            return &e;
    return nullptr;
}

const DockSpace::Entry* DockSpace::find(const std::string& id) const
{
    for (const Entry& e : entries_)
        if (e.id == id)
            return &e;
    return nullptr;
}

void DockSpace::setCenter(std::unique_ptr<Widget> center)
{
    reclaim();
    centerRaw_   = center.get();
    centerOwned_ = std::move(center);
    dirty_       = true;
}

Widget* DockSpace::addPanel(std::string id, std::string title, std::unique_ptr<Widget> content, DockSide side)
{
    reclaim();
    Entry entry;
    entry.id    = std::move(id);
    entry.title = std::move(title);
    entry.side  = side;
    entry.raw   = content.get();
    entry.owned = std::move(content);
    entries_.push_back(std::move(entry));
    dirty_ = true;
    return entries_.back().raw;
}

void DockSpace::movePanel(const std::string& id, DockSide side)
{
    Entry* entry = find(id);
    if (!entry || entry->side == side)
        return;
    entry->side = side;
    dirty_      = true;
}

bool DockSpace::hasPanel(const std::string& id) const
{
    return find(id) != nullptr;
}

DockSide DockSpace::sideOf(const std::string& id) const
{
    const Entry* entry = find(id);
    return entry ? entry->side : DockSide::Left;
}

Widget* DockSpace::panelContent(const std::string& id) const
{
    const Entry* entry = find(id);
    return entry ? entry->raw : nullptr;
}

std::vector<std::string> DockSpace::panelIds() const
{
    std::vector<std::string> ids;
    for (const Entry& e : entries_)
        ids.push_back(e.id);
    return ids;
}

void DockSpace::setFractions(float left, float right, float bottom)
{
    leftFraction_   = std::clamp(left, 0.05f, 0.6f);
    rightFraction_  = std::clamp(right, 0.05f, 0.6f);
    bottomFraction_ = std::clamp(bottom, 0.05f, 0.7f);
    dirty_          = true;
    skipSync_       = true;
}

void DockSpace::syncFractions()
{
    if (!built_)
        return;
    if (hasLeft_ && outerSplit_)
        leftFraction_ = outerSplit_->ratio();
    if (hasRight_ && innerSplit_)
    {
        const float remaining = hasLeft_ ? 1.f - leftFraction_ : 1.f;
        rightFraction_        = (1.f - innerSplit_->ratio()) * remaining;
    }
    if (hasBottom_ && bottomSplit_)
        bottomFraction_ = 1.f - bottomSplit_->ratio();
}

float DockSpace::leftFraction()
{
    syncFractions();
    return leftFraction_;
}

float DockSpace::rightFraction()
{
    syncFractions();
    return rightFraction_;
}

float DockSpace::bottomFraction()
{
    syncFractions();
    return bottomFraction_;
}

void DockSpace::reclaim()
{
    if (!skipSync_)
        syncFractions();
    skipSync_ = false;
    for (Entry& e : entries_)
    {
        if (!e.owned && e.raw && e.raw->parent())
            e.owned = e.raw->parent()->removeChild(e.raw);
    }
    if (!centerOwned_ && centerRaw_ && centerRaw_->parent())
        centerOwned_ = centerRaw_->parent()->removeChild(centerRaw_);

    if (built_)
    {
        removeChild(built_);
        built_       = nullptr;
        outerSplit_  = nullptr;
        innerSplit_  = nullptr;
        bottomSplit_ = nullptr;
    }
    dirty_ = true;
}

std::unique_ptr<Widget> DockSpace::buildStack(const std::vector<Entry*>& list, std::size_t index)
{
    auto frame = std::make_unique<PanelFrame>(list[index]->title);
    frame->setContent(std::move(list[index]->owned));
    frame->setExpand(true, true);
    if (index + 1 == list.size())
        return frame;

    auto split = std::make_unique<SplitContainer>(true);
    split->setRatio(1.f / static_cast<float>(list.size() - index));
    split->setExpand(true, true);
    split->addChild(std::move(frame));
    split->addChild(buildStack(list, index + 1));
    return split;
}

void DockSpace::rebuild()
{
    std::vector<Entry*> left;
    std::vector<Entry*> right;
    std::vector<Entry*> bottom;
    for (Entry& e : entries_)
    {
        switch (e.side)
        {
        case DockSide::Left:
            left.push_back(&e);
            break;
        case DockSide::Right:
            right.push_back(&e);
            break;
        case DockSide::Bottom:
            bottom.push_back(&e);
            break;
        }
    }
    hasLeft_   = !left.empty();
    hasRight_  = !right.empty();
    hasBottom_ = !bottom.empty();

    std::unique_ptr<Widget> middle;
    if (centerOwned_)
    {
        centerOwned_->setExpand(true, true);
        middle = std::move(centerOwned_);
    }
    else
    {
        middle = std::make_unique<Widget>();
    }

    if (hasBottom_)
    {
        auto tabs = std::make_unique<TabContainer>();
        tabs->setExpand(true, true);
        for (Entry* e : bottom)
            tabs->addPage(std::move(e->owned), e->title);
        auto split = std::make_unique<SplitContainer>(true);
        split->setExpand(true, true);
        split->setRatio(1.f - bottomFraction_);
        bottomSplit_ = split.get();
        split->addChild(std::move(middle));
        split->addChild(std::move(tabs));
        middle = std::move(split);
    }

    if (hasRight_)
    {
        auto split = std::make_unique<SplitContainer>(false);
        split->setExpand(true, true);
        const float remaining = hasLeft_ ? 1.f - leftFraction_ : 1.f;
        split->setRatio(std::clamp(1.f - rightFraction_ / remaining, 0.2f, 0.95f));
        innerSplit_ = split.get();
        split->addChild(std::move(middle));
        split->addChild(buildStack(right, 0));
        middle = std::move(split);
    }

    if (hasLeft_)
    {
        auto split = std::make_unique<SplitContainer>(false);
        split->setExpand(true, true);
        split->setRatio(leftFraction_);
        outerSplit_ = split.get();
        split->addChild(buildStack(left, 0));
        split->addChild(std::move(middle));
        middle = std::move(split);
    }

    built_ = addChild(std::move(middle));
    dirty_ = false;
}

void DockSpace::layout()
{
    if (dirty_)
    {
        if (built_)
            reclaim();
        rebuild();
    }
    if (built_)
        built_->setRect(rect());
}

std::string DockSpace::saveLayout()
{
    syncFractions();
    std::ostringstream out;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4f", leftFraction_);
    out << "izquierda=" << buffer << "\n";
    std::snprintf(buffer, sizeof(buffer), "%.4f", rightFraction_);
    out << "derecha=" << buffer << "\n";
    std::snprintf(buffer, sizeof(buffer), "%.4f", bottomFraction_);
    out << "abajo=" << buffer << "\n";
    for (const Entry& e : entries_)
        out << "panel:" << e.id << "=" << sideName(e.side) << "\n";
    return out.str();
}

void DockSpace::loadLayout(const std::string& text)
{
    float left   = leftFraction_;
    float right  = rightFraction_;
    float bottom = bottomFraction_;

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key   = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "izquierda")
            left = static_cast<float>(std::atof(value.c_str()));
        else if (key == "derecha")
            right = static_cast<float>(std::atof(value.c_str()));
        else if (key == "abajo")
            bottom = static_cast<float>(std::atof(value.c_str()));
        else if (key.rfind("panel:", 0) == 0)
        {
            DockSide side;
            if (parseSide(value, side))
                movePanel(key.substr(6), side);
        }
    }
    setFractions(left, right, bottom);
}

}
