#include "zio/scene3d/Node3D.h"

#include <algorithm>

#include "zio/scene3d/Renderer3D.h"

namespace zio::scene3d
{

Node3D* Node3D::addChild(std::unique_ptr<Node3D> child)
{
    child->parent_ = this;
    children_.push_back(std::move(child));
    return children_.back().get();
}

std::unique_ptr<Node3D> Node3D::removeChild(Node3D* child)
{
    const auto it = std::find_if(children_.begin(), children_.end(),
                                 [child](const std::unique_ptr<Node3D>& c) { return c.get() == child; });
    if (it == children_.end())
        return nullptr;
    std::unique_ptr<Node3D> out = std::move(*it);
    children_.erase(it);
    out->parent_ = nullptr;
    return out;
}

void Node3D::removeAllChildren()
{
    children_.clear();
}

math::Mat4 Node3D::localMatrix() const
{
    return math::Mat4::translation(position_) * math::Mat4::rotation(rotation_) * math::Mat4::scale(scale_);
}

void Node3D::updateWorldMatrix(const math::Mat4& parentWorld)
{
    worldMatrix_ = parentWorld * localMatrix();
    for (auto& child : children_)
        child->updateWorldMatrix(worldMatrix_);
}

void Node3D::visit(Renderer3D& renderer)
{
    if (!visible_)
        return;
    onDraw(renderer);
    for (auto& child : children_)
        child->visit(renderer);
}

bool MeshNode::setMeshSource(std::string source, std::string* error)
{
    auto mesh = Mesh::fromSource(source, error);
    if (!mesh)
        return false;
    mesh_       = std::move(mesh);
    meshSource_ = std::move(source);
    return true;
}

void MeshNode::setTexturePath(std::string path)
{
    texture_ = path.empty() ? nullptr
                            : renderer::TextureCache::instance().loadSync(path, render::TextureFilter::Linear,
                                                                           render::TextureWrap::Repeat);
    texturePath_ = std::move(path);
}

void MeshNode::onDraw(Renderer3D& renderer)
{
    if (!mesh_)
        return;
    MeshStyle style;
    style.color       = color_;
    style.texture     = texture_ ? texture_->handle : render::kInvalidHandle;
    style.uvScale     = uvScale_;
    style.castShadow  = castShadow_;
    renderer.drawMesh(*mesh_, worldMatrix(), style);
}

}
