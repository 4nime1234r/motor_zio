#include "zio/scene2d/Node.h"

#include <algorithm>

namespace zio::scene2d
{

void Node::setPosition(math::Vec2 position)
{
    position_ = position;
    dirty_    = true;
}

void Node::setRotation(float degrees)
{
    rotationDegrees_ = degrees;
    dirty_           = true;
}

void Node::setScale(math::Vec2 scale)
{
    scale_ = scale;
    dirty_ = true;
}

void Node::setAnchorPoint(math::Vec2 anchor)
{
    anchor_ = anchor;
    dirty_  = true;
}

void Node::setContentSize(math::Vec2 size)
{
    contentSize_ = size;
    dirty_       = true;
}

void Node::setLocalZOrder(int zOrder)
{
    if (zOrder_ == zOrder)
        return;
    zOrder_ = zOrder;

    if (!parent_)
        return;
    auto& siblings = parent_->children_;
    auto it         = std::find_if(siblings.begin(), siblings.end(), [this](const std::unique_ptr<Node>& n) { return n.get() == this; });
    if (it == siblings.end())
        return;
    std::unique_ptr<Node> self = std::move(*it);
    siblings.erase(it);
    parent_->insertChildSorted(std::move(self));
}

void Node::insertChildSorted(std::unique_ptr<Node> child)
{
    auto it = std::find_if(children_.begin(), children_.end(),
                            [&](const std::unique_ptr<Node>& n) { return n->zOrder_ > child->zOrder_; });
    children_.insert(it, std::move(child));
}

Node* Node::addChild(std::unique_ptr<Node> child)
{
    child->parent_ = this;
    Node* ptr        = child.get();
    insertChildSorted(std::move(child));
    return ptr;
}

std::unique_ptr<Node> Node::removeChild(Node* child)
{
    auto it = std::find_if(children_.begin(), children_.end(),
                            [child](const std::unique_ptr<Node>& n) { return n.get() == child; });
    if (it == children_.end())
        return nullptr;

    std::unique_ptr<Node> removed = std::move(*it);
    children_.erase(it);
    removed->parent_ = nullptr;
    return removed;
}

void Node::removeAllChildren()
{
    for (auto& child : children_)
        child->parent_ = nullptr;
    children_.clear();
}

const math::Mat3& Node::localMatrix() const
{
    if (dirty_)
    {
        const math::Mat3 anchorOffset = math::Mat3::translation(
            math::Vec2{-anchor_.x * contentSize_.x, -anchor_.y * contentSize_.y});
        localMatrix_ = math::Mat3::translation(position_) * math::Mat3::rotationDegrees(rotationDegrees_) *
                       math::Mat3::scale(scale_) * anchorOffset;
        dirty_ = false;
    }
    return localMatrix_;
}

void Node::updateWorldMatrix(const math::Mat3& parentWorld)
{
    worldMatrix_ = parentWorld * localMatrix();
    for (auto& child : children_)
        child->updateWorldMatrix(worldMatrix_);
}

void Node::visit()
{
    if (!visible_)
        return;

    onDraw();
    visitChildren();
}

void Node::visitChildren()
{
    for (std::size_t i = 0; i < children_.size();)
    {
        Node* child = children_[i].get();
        if (child->visible_)
            child->onDraw();

        if (i >= children_.size() || children_[i].get() != child)
            continue;

        if (child->visible_)
            child->visitChildren();
        ++i;
    }
}

}
