#pragma once

#include <memory>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/math/Mat4.h"
#include "zio/math/Quaternion.h"
#include "zio/math/Vec2.h"
#include "zio/math/Vec3.h"
#include "zio/renderer/Color.h"
#include "zio/renderer/Texture.h"
#include "zio/scene3d/Mesh.h"

namespace zio::scene3d
{

class Renderer3D;

class ZIO_API Node3D
{
public:
    Node3D()          = default;
    virtual ~Node3D() = default;

    Node3D(const Node3D&)            = delete;
    Node3D& operator=(const Node3D&) = delete;

    void setName(std::string name) { name_ = std::move(name); }
    const std::string& name() const { return name_; }

    void setPosition(math::Vec3 position) { position_ = position; }
    math::Vec3 position() const { return position_; }

    void setRotation(math::Quaternion rotation) { rotation_ = rotation.normalized(); }
    math::Quaternion rotation() const { return rotation_; }

    void setScale(math::Vec3 scale) { scale_ = scale; }
    math::Vec3 scale() const { return scale_; }

    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

    Node3D* addChild(std::unique_ptr<Node3D> child);
    template <typename NodeT, typename... Args>
    NodeT* addChild(Args&&... args)
    {
        auto child  = std::make_unique<NodeT>(std::forward<Args>(args)...);
        NodeT* ptr  = child.get();
        addChild(std::move(child));
        return ptr;
    }
    std::unique_ptr<Node3D> removeChild(Node3D* child);
    void removeAllChildren();

    const std::vector<std::unique_ptr<Node3D>>& children() const { return children_; }
    Node3D* parent() const { return parent_; }

    math::Mat4 localMatrix() const;
    const math::Mat4& worldMatrix() const { return worldMatrix_; }
    void updateWorldMatrix(const math::Mat4& parentWorld);

    virtual void update(float dt) {}

    void visit(Renderer3D& renderer);

protected:
    virtual void onDraw(Renderer3D& renderer) {}

private:
    Node3D* parent_ = nullptr;
    std::vector<std::unique_ptr<Node3D>> children_;
    std::string name_;

    math::Vec3 position_        = math::Vec3::zero();
    math::Quaternion rotation_  = math::Quaternion::identity();
    math::Vec3 scale_           = math::Vec3::one();
    bool visible_               = true;
    math::Mat4 worldMatrix_     = math::Mat4::identity();
};

class ZIO_API MeshNode final : public Node3D
{
public:
    MeshNode() = default;
    explicit MeshNode(std::string source) { setMeshSource(std::move(source)); }

    bool setMeshSource(std::string source, std::string* error = nullptr);
    const std::string& meshSource() const { return meshSource_; }

    void setMesh(std::shared_ptr<Mesh> mesh)
    {
        mesh_ = std::move(mesh);
        meshSource_.clear();
    }
    const std::shared_ptr<Mesh>& mesh() const { return mesh_; }

    void setColor(renderer::Color color) { color_ = color; }
    renderer::Color color() const { return color_; }

    void setTexturePath(std::string path);
    const std::string& texturePath() const { return texturePath_; }
    bool hasTexture() const { return texture_ != nullptr; }

    void setUvScale(math::Vec2 scale) { uvScale_ = scale; }
    math::Vec2 uvScale() const { return uvScale_; }

    void setCastShadow(bool cast) { castShadow_ = cast; }
    bool castShadow() const { return castShadow_; }

protected:
    void onDraw(Renderer3D& renderer) override;

private:
    std::shared_ptr<Mesh> mesh_;
    std::string meshSource_;
    renderer::Color color_ = renderer::Color::white();
    std::string texturePath_;
    std::shared_ptr<renderer::Texture> texture_;
    math::Vec2 uvScale_ = math::Vec2::one();
    bool castShadow_    = true;
};

}
