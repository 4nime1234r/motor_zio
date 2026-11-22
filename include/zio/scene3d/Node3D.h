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
#include "zio/scene3d/AnimationGraph.h"
#include "zio/scene3d/Mesh.h"
#include "zio/scene3d/Renderer3D.h"
#include "zio/scripting/Script.h"

namespace zio::scene3d
{

class Renderer3D;

class ZIO_API Node3D
{
public:
    Node3D() = default;
    virtual ~Node3D();

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

    void markForDestroy() { destroyPending_ = true; }
    bool destroyPending() const { return destroyPending_; }
    std::size_t removeDestroyed();

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

    struct ScriptSlot
    {
        std::string name;
        std::unique_ptr<scripting::Script> instance;
        bool started = false;
        unsigned tried = 0;
    };

    bool addScript(const std::string& name);
    bool removeScript(const std::string& name);
    bool hasScript(const std::string& name) const;
    std::vector<std::string> scriptNames() const;
    const std::vector<ScriptSlot>& scripts() const { return scripts_; }
    void runScripts(float dt);
    void releaseScripts();

    void visit(Renderer3D& renderer);

protected:
    virtual void onDraw(Renderer3D& renderer) {}

private:
    Node3D* parent_ = nullptr;
    std::vector<std::unique_ptr<Node3D>> children_;
    std::vector<ScriptSlot> scripts_;
    std::string name_;

    math::Vec3 position_        = math::Vec3::zero();
    math::Quaternion rotation_  = math::Quaternion::identity();
    math::Vec3 scale_           = math::Vec3::one();
    bool visible_               = true;
    bool destroyPending_        = false;
    math::Mat4 worldMatrix_     = math::Mat4::identity();
};

class ZIO_API MeshNode final : public Node3D
{
public:
    MeshNode() = default;
    explicit MeshNode(std::string source) { setMeshSource(std::move(source)); }

    bool setMeshSource(std::string source, std::string* error = nullptr);
    const std::string& meshSource() const { return meshSource_; }

    void setMesh(std::shared_ptr<Mesh> mesh);
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

    std::size_t morphTargetCount() const { return mesh_ ? mesh_->morphTargets.size() : 0; }
    const std::string& morphTargetName(std::size_t index) const;
    void setMorphWeight(std::size_t index, float weight);
    float morphWeight(std::size_t index) const { return index < morphWeights_.size() ? morphWeights_[index] : 0.f; }

    std::size_t addLayer(const std::string& rootJoint = {});
    bool playLayer(std::size_t layer, const std::string& clip, bool loop = true);
    void stopLayer(std::size_t layer);
    void setLayerWeight(std::size_t layer, float weight);
    float layerWeight(std::size_t layer) const;
    std::size_t layerCount() const { return layers_.size(); }
    void removeLayers();

    void setAnimationGraph(std::shared_ptr<AnimationGraph> graph);
    const std::shared_ptr<AnimationGraph>& animationGraph() const { return graph_; }
    const std::vector<math::Mat4>& skinMatrices() const { return skinMatrices_; }

    std::vector<std::string> morphAnimationNames() const;
    bool playMorphAnimation(const std::string& name, bool loop = true);
    void stopMorphAnimation() { morphPlaying_ = false; }
    bool isMorphAnimationPlaying() const { return morphPlaying_; }

    std::size_t addFileLights();
    bool fileCamera(std::size_t index, Camera& out) const;

    bool hasSkeleton() const { return mesh_ && mesh_->skin; }
    std::vector<std::string> animationNames() const;
    bool playAnimation(const std::string& name, bool loop = true, float fadeSeconds = 0.f);
    bool isBlending() const { return fadeDuration_ > 0.f && fadeElapsed_ < fadeDuration_; }
    void stopAnimation();
    bool isAnimationPlaying() const { return animPlaying_; }
    const std::string& currentAnimation() const { return animName_; }
    bool animationLoops() const { return animLoop_; }
    void setAnimationLoop(bool loop) { animLoop_ = loop; }
    void setAnimationSpeed(float speed) { animSpeed_ = speed; }
    float animationSpeed() const { return animSpeed_; }
    void setAnimationTime(float seconds);
    float animationTime() const { return animTime_; }

    void update(float dt) override;

protected:
    void onDraw(Renderer3D& renderer) override;

private:
    std::shared_ptr<Mesh> mesh_;
    std::string meshSource_;
    renderer::Color color_ = renderer::Color::white();
    std::string texturePath_;
    std::shared_ptr<renderer::Texture> texture_;
    std::vector<std::shared_ptr<renderer::Texture>> partTextures_;
    struct MaterialTextures
    {
        std::shared_ptr<renderer::Texture> normal;
        std::shared_ptr<renderer::Texture> metallicRoughness;
        std::shared_ptr<renderer::Texture> emissive;
        std::shared_ptr<renderer::Texture> occlusion;
    };
    std::vector<MaterialTextures> materialTextures_;
    struct AnimationLayer
    {
        std::string clip;
        std::string root;
        float time    = 0.f;
        float weight  = 1.f;
        bool loop     = true;
        bool playing  = false;
        bool active   = false;
    };
    std::vector<AnimationLayer> layers_;
    std::vector<math::Mat4> skinMatrices_;
    std::shared_ptr<AnimationGraph> graph_;
    std::vector<float> morphWeights_;
    std::string morphClip_;
    float morphTime_     = 0.f;
    bool morphLoop_      = true;
    bool morphPlaying_   = false;
    void sampleMorph(const MorphClip& clip);
    std::shared_ptr<Mesh> morphed_;
    void applyMorph();
    void loadPartTextures();
    void applyMaterial(MeshStyle& style, const Material& material, const MaterialTextures& textures) const;
    math::Vec2 uvScale_ = math::Vec2::one();
    bool castShadow_    = true;

    void pose();

    std::shared_ptr<Mesh> posed_;
    std::string animName_;
    float animTime_    = 0.f;
    std::string prevName_;
    float prevTime_       = 0.f;
    float fadeDuration_   = 0.f;
    float fadeElapsed_    = 0.f;
    float animSpeed_   = 1.f;
    bool animPlaying_  = false;
    bool animLoop_     = true;
};

class ZIO_API PrefabNode final : public Node3D
{
public:
    PrefabNode() = default;

    bool setSource(std::string source, std::string* error = nullptr);
    const std::string& source() const { return source_; }
    bool reload(std::string* error = nullptr);

private:
    std::string source_;
};

class ZIO_API LightNode final : public Node3D
{
public:
    LightNode() = default;

    void setLightType(LightType type) { type_ = type; }
    LightType lightType() const { return type_; }

    void setColor(renderer::Color color) { color_ = color; }
    renderer::Color color() const { return color_; }

    void setIntensity(float intensity) { intensity_ = intensity; }
    float intensity() const { return intensity_; }

    void setRange(float range) { range_ = range; }
    float range() const { return range_; }

    void setInnerAngle(float degrees) { inner_ = degrees; }
    float innerAngle() const { return inner_; }

    void setOuterAngle(float degrees) { outer_ = degrees; }
    float outerAngle() const { return outer_; }

    void setCastShadow(bool cast) { castShadow_ = cast; }
    bool castShadow() const { return castShadow_; }

    void setShadowOpacity(float opacity) { shadowOpacity_ = opacity; }
    float shadowOpacity() const { return shadowOpacity_; }

protected:
    void onDraw(Renderer3D& renderer) override;

private:
    LightType type_          = LightType::Point;
    renderer::Color color_   = renderer::Color::white();
    float intensity_         = 1.f;
    float range_             = 10.f;
    float inner_             = 20.f;
    float outer_             = 35.f;
    bool castShadow_         = false;
    float shadowOpacity_     = 1.f;
};

}
