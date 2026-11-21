#pragma once

#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/math/Mat3.h"
#include "zio/math/Vec2.h"

namespace zio::scene2d
{

class ZIO_API Node
{
public:
    Node()          = default;
    virtual ~Node() = default;

    Node(const Node&)            = delete;
    Node& operator=(const Node&) = delete;

    void setName(std::string name) { name_ = std::move(name); }
    const std::string& name() const { return name_; }

    void setPosition(math::Vec2 position);
    math::Vec2 position() const { return position_; }

    void setRotation(float degrees);
    float rotation() const { return rotationDegrees_; }

    void setScale(math::Vec2 scale);
    math::Vec2 scale() const { return scale_; }

    void setAnchorPoint(math::Vec2 anchor);
    math::Vec2 anchorPoint() const { return anchor_; }

    void setContentSize(math::Vec2 size);
    math::Vec2 contentSize() const { return contentSize_; }

    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

    void setLocalZOrder(int zOrder);
    int localZOrder() const { return zOrder_; }

    Node* addChild(std::unique_ptr<Node> child);
    template <typename NodeT, typename... Args>
    NodeT* addChild(Args&&... args)
    {
        auto child = std::make_unique<NodeT>(std::forward<Args>(args)...);
        NodeT* ptr = child.get();
        addChild(std::move(child));
        return ptr;
    }
    std::unique_ptr<Node> removeChild(Node* child);
    void removeAllChildren();

    const std::vector<std::unique_ptr<Node>>& children() const { return children_; }
    Node* parent() const { return parent_; }

    const math::Mat3& localMatrix() const;
    const math::Mat3& worldMatrix() const { return worldMatrix_; }

    void updateWorldMatrix(const math::Mat3& parentWorld);

    virtual void update(float dt) {}

protected:
    virtual void onDraw() {}

public:
    void visit();

private:
    void insertChildSorted(std::unique_ptr<Node> child);
    void visitChildren();

    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    std::string name_;

    math::Vec2 position_    = math::Vec2::zero();
    float rotationDegrees_  = 0.f;
    math::Vec2 scale_       = math::Vec2::one();
    math::Vec2 anchor_      = math::Vec2::zero();
    math::Vec2 contentSize_ = math::Vec2::zero();
    int zOrder_             = 0;
    bool visible_           = true;

    mutable math::Mat3 localMatrix_ = math::Mat3::identity();
    mutable bool dirty_             = true;
    math::Mat3 worldMatrix_         = math::Mat3::identity();
};

}
