#include "zio/scene3d/Node3D.h"

#include <algorithm>
#include <cmath>

#include "zio/scene3d/Renderer3D.h"
#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"

namespace zio::scene3d
{

Node3D::~Node3D()
{
    for (ScriptSlot& slot : scripts_)
        if (slot.instance && slot.started)
            slot.instance->onDestroy();
}

bool Node3D::addScript(const std::string& name)
{
    if (name.empty() || hasScript(name))
        return false;
    ScriptSlot slot;
    slot.name     = name;
    slot.instance = scripting::Registry::instance().create(name);
    slot.tried    = scripting::Registry::instance().version();
    if (slot.instance)
        slot.instance->node_ = this;
    const bool created = slot.instance != nullptr;
    scripts_.push_back(std::move(slot));
    return created;
}

bool Node3D::removeScript(const std::string& name)
{
    for (std::size_t i = 0; i < scripts_.size(); ++i)
    {
        if (scripts_[i].name != name)
            continue;
        ScriptSlot slot = std::move(scripts_[i]);
        scripts_.erase(scripts_.begin() + static_cast<std::ptrdiff_t>(i));
        if (slot.instance && slot.started)
            slot.instance->onDestroy();
        return true;
    }
    return false;
}

bool Node3D::hasScript(const std::string& name) const
{
    for (const ScriptSlot& slot : scripts_)
        if (slot.name == name)
            return true;
    return false;
}

std::vector<std::string> Node3D::scriptNames() const
{
    std::vector<std::string> names;
    for (const ScriptSlot& slot : scripts_)
        names.push_back(slot.name);
    return names;
}

void Node3D::releaseScripts()
{
    for (ScriptSlot& slot : scripts_)
    {
        if (slot.instance && slot.started)
            slot.instance->onDestroy();
        slot.instance.reset();
        slot.started = false;
        slot.tried   = 0;
    }
    for (auto& child : children_)
        child->releaseScripts();
}

void Node3D::runScripts(float dt)
{
    for (std::size_t i = 0; i < scripts_.size(); ++i)
    {
        if (!scripts_[i].instance && scripts_[i].tried != scripting::Registry::instance().version())
        {
            scripts_[i].tried    = scripting::Registry::instance().version();
            scripts_[i].instance = scripting::Registry::instance().create(scripts_[i].name);
            if (scripts_[i].instance)
                scripts_[i].instance->node_ = this;
        }
        scripting::Script* script = scripts_[i].instance.get();
        if (!script)
            continue;
        if (!scripts_[i].started)
        {
            scripts_[i].started = true;
            script->onStart();
            if (i >= scripts_.size() || scripts_[i].instance.get() != script)
                continue;
        }
        script->onUpdate(dt);
    }
}

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

std::size_t Node3D::removeDestroyed()
{
    std::size_t removed = 0;
    for (std::size_t i = 0; i < children_.size();)
    {
        if (children_[i]->destroyPending_)
        {
            std::unique_ptr<Node3D> doomed = removeChild(children_[i].get());
            doomed.reset();
            ++removed;
            continue;
        }
        removed += children_[i]->removeDestroyed();
        ++i;
    }
    return removed;
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
    loadPartTextures();
    if (mesh_->parts.size() > 1)
    {
        color_ = renderer::Color::white();
        setTexturePath({});
        return true;
    }
    color_ = renderer::Color{mesh_->baseColor[0], mesh_->baseColor[1], mesh_->baseColor[2], mesh_->baseColor[3]};
    if (!mesh_->texturePath.empty())
        setTexturePath(mesh_->texturePath);
    return true;
}

void MeshNode::setMesh(std::shared_ptr<Mesh> mesh)
{
    mesh_ = std::move(mesh);
    meshSource_.clear();
    loadPartTextures();
}

void MeshNode::loadPartTextures()
{
    partTextures_.clear();
    materialTextures_.clear();
    morphWeights_.clear();
    morphed_.reset();
    if (!mesh_)
        return;
    morphWeights_.assign(mesh_->morphTargets.size(), 0.f);
    for (std::size_t i = 0; i < morphWeights_.size() && i < mesh_->morphWeights.size(); ++i)
        morphWeights_[i] = mesh_->morphWeights[i];
    applyMorph();
    auto load = [this](const std::string& path) -> std::shared_ptr<renderer::Texture>
    {
        if (path.empty())
            return nullptr;
        if (path.rfind(kEmbeddedPrefix, 0) == 0)
        {
            const auto it = mesh_->embeddedImages.find(path);
            if (it == mesh_->embeddedImages.end())
                return nullptr;
            return renderer::TextureCache::instance().loadFromMemory(path, it->second.data(), it->second.size(),
                                                                     render::TextureFilter::Linear,
                                                                     render::TextureWrap::Repeat);
        }
        return renderer::TextureCache::instance().loadSync(path, render::TextureFilter::Linear,
                                                           render::TextureWrap::Repeat);
    };
    auto loadMaterial = [&](const Material& m)
    {
        MaterialTextures t;
        t.normal            = load(m.normalTexture);
        t.metallicRoughness = load(m.metallicRoughnessTexture);
        t.emissive          = load(m.emissiveTexture);
        t.occlusion         = load(m.occlusionTexture);
        return t;
    };
    if (mesh_->parts.size() < 2)
    {
        materialTextures_.push_back(loadMaterial(mesh_->material));
        return;
    }
    for (const SubMesh& part : mesh_->parts)
    {
        partTextures_.push_back(load(part.texturePath));
        materialTextures_.push_back(loadMaterial(part.material));
    }
}

void MeshNode::applyMaterial(MeshStyle& style, const Material& material, const MaterialTextures& textures) const
{
    style.metallic          = material.metallic;
    style.roughness         = material.roughness;
    style.normalScale       = material.normalScale;
    style.occlusionStrength = material.occlusionStrength;
    style.alphaMode         = material.alphaMode;
    style.alphaCutoff       = material.alphaCutoff;
    style.doubleSided       = material.doubleSided;
    for (int i = 0; i < 3; ++i)
        style.emissive[i] = material.emissive[i];
    auto handle = [](const std::shared_ptr<renderer::Texture>& t)
    { return t ? t->handle : render::kInvalidHandle; };
    style.normalTexture            = handle(textures.normal);
    style.metallicRoughnessTexture = handle(textures.metallicRoughness);
    style.emissiveTexture          = handle(textures.emissive);
    style.occlusionTexture         = handle(textures.occlusion);
}

void MeshNode::setTexturePath(std::string path)
{
    texture_.reset();
    if (path.rfind(kEmbeddedPrefix, 0) == 0)
    {
        if (mesh_)
        {
            const auto it = mesh_->embeddedImages.find(path);
            if (it != mesh_->embeddedImages.end())
                texture_ = renderer::TextureCache::instance().loadFromMemory(
                    path, it->second.data(), it->second.size(), render::TextureFilter::Linear,
                    render::TextureWrap::Repeat);
        }
    }
    else if (!path.empty())
    {
        texture_ = renderer::TextureCache::instance().loadSync(path, render::TextureFilter::Linear,
                                                               render::TextureWrap::Repeat);
    }
    texturePath_ = std::move(path);
}

std::vector<std::string> MeshNode::animationNames() const
{
    std::vector<std::string> names;
    if (mesh_ && mesh_->skin)
        for (const AnimationClip& clip : mesh_->skin->clips)
            names.push_back(clip.name);
    return names;
}

bool MeshNode::playAnimation(const std::string& name, bool loop, float fadeSeconds)
{
    if (!mesh_ || !mesh_->skin || !mesh_->skin->findClip(name))
        return false;
    if (fadeSeconds > 0.f && posed_)
    {
        prevName_     = animName_;
        prevTime_     = animTime_;
        fadeDuration_ = fadeSeconds;
        fadeElapsed_  = 0.f;
    }
    else
    {
        prevName_.clear();
        fadeDuration_ = 0.f;
        fadeElapsed_  = 0.f;
    }
    animName_    = name;
    animLoop_    = loop;
    animTime_    = 0.f;
    animPlaying_ = true;
    pose();
    return true;
}

void MeshNode::stopAnimation()
{
    animPlaying_ = false;
    animName_.clear();
    animTime_ = 0.f;
    prevName_.clear();
    fadeDuration_ = 0.f;
    fadeElapsed_  = 0.f;
    posed_.reset();
}

void MeshNode::setAnimationTime(float seconds)
{
    animTime_ = seconds < 0.f ? 0.f : seconds;
    if (animPlaying_)
        pose();
}

void MeshNode::update(float dt)
{
    if (!animPlaying_ || !mesh_ || !mesh_->skin)
        return;
    const AnimationClip* clip = mesh_->skin->findClip(animName_);
    if (!clip)
    {
        animPlaying_ = false;
        return;
    }
    if (isBlending())
    {
        fadeElapsed_ += dt;
        prevTime_ += dt * animSpeed_;
        if (const AnimationClip* prev = mesh_->skin->findClip(prevName_))
            if (prev->duration > 0.f)
                prevTime_ = std::fmod(prevTime_, prev->duration);
    }
    animTime_ += dt * animSpeed_;
    if (clip->duration > 0.f)
    {
        if (animLoop_)
        {
            animTime_ = std::fmod(animTime_, clip->duration);
            if (animTime_ < 0.f)
                animTime_ += clip->duration;
        }
        else if (animTime_ >= clip->duration)
        {
            animTime_    = clip->duration;
            animPlaying_ = false;
        }
    }
    pose();
}

void MeshNode::pose()
{
    if (!mesh_ || !mesh_->skin)
        return;
    const AnimationClip* clip = mesh_->skin->findClip(animName_);
    std::vector<math::Mat4> matrices;
    if (isBlending())
    {
        std::vector<JointPose> from, to, mixed;
        mesh_->skin->samplePose(mesh_->skin->findClip(prevName_), prevTime_, from);
        mesh_->skin->samplePose(clip, animTime_, to);
        Skin::blendPoses(from, to, fadeElapsed_ / fadeDuration_, mixed);
        mesh_->skin->skinMatrices(mixed, matrices);
    }
    else
    {
        mesh_->skin->skinMatrices(clip, animTime_, matrices);
    }
    if (!posed_)
        posed_ = std::make_shared<Mesh>();
    posed_->dynamic = true;
    mesh_->skin->apply(morphed_ ? *morphed_ : *mesh_, matrices, *posed_);
}

const std::string& MeshNode::morphTargetName(std::size_t index) const
{
    static const std::string empty;
    return mesh_ && index < mesh_->morphTargets.size() ? mesh_->morphTargets[index].name : empty;
}

void MeshNode::setMorphWeight(std::size_t index, float weight)
{
    if (index >= morphWeights_.size())
        return;
    morphWeights_[index] = weight;
    applyMorph();
    if (animPlaying_)
        pose();
}

void MeshNode::applyMorph()
{
    morphed_.reset();
    if (!mesh_ || mesh_->morphTargets.empty())
        return;
    bool any = false;
    for (const float w : morphWeights_)
        any = any || w != 0.f;
    if (!any)
        return;
    morphed_           = std::make_shared<Mesh>();
    morphed_->dynamic  = true;
    morphed_->vertices = mesh_->vertices;
    const std::size_t count = morphed_->vertices.size();
    for (std::size_t t = 0; t < morphWeights_.size() && t < mesh_->morphTargets.size(); ++t)
    {
        const float w = morphWeights_[t];
        if (w == 0.f)
            continue;
        const MorphTarget& target = mesh_->morphTargets[t];
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i < target.positions.size())
                morphed_->vertices[i].position = morphed_->vertices[i].position + target.positions[i] * w;
            if (i < target.normals.size())
                morphed_->vertices[i].normal = morphed_->vertices[i].normal + target.normals[i] * w;
        }
    }
    for (Vertex3D& v : morphed_->vertices)
        v.normal = v.normal.normalized();
}

std::size_t MeshNode::addFileLights()
{
    if (!mesh_)
        return 0;
    std::size_t added = 0;
    for (const FileLight& fl : mesh_->lights)
    {
        if (fl.type == FileLightType::Directional)
            continue;
        LightNode* node = addChild<LightNode>();
        node->setName(fl.name.empty() ? "Light" : fl.name);
        node->setLightType(fl.type == FileLightType::Spot ? LightType::Spot : LightType::Point);
        node->setColor(renderer::Color{fl.color[0], fl.color[1], fl.color[2], 1.f});
        node->setIntensity(fl.intensity <= 10.f ? fl.intensity : 1.f);
        node->setRange(fl.range > 0.f ? fl.range : 10.f);
        node->setInnerAngle(fl.innerAngleDegrees);
        node->setOuterAngle(fl.outerAngleDegrees);
        node->setPosition({fl.world.m[0][3], fl.world.m[1][3], fl.world.m[2][3]});
        const math::Vec3 forward = fl.world.transformVector({0.f, 0.f, -1.f}).normalized();
        const math::Vec3 down{0.f, -1.f, 0.f};
        const float d = std::clamp(down.x * forward.x + down.y * forward.y + down.z * forward.z, -1.f, 1.f);
        const math::Vec3 axis = down.cross(forward);
        if (d < -0.9999f)
            node->setRotation(math::Quaternion::fromAxisAngle({1.f, 0.f, 0.f}, 180.f));
        else if (axis.length() > 1e-6f)
            node->setRotation(math::Quaternion::fromAxisAngle(axis.normalized(), std::acos(d) * 57.29578f));
        ++added;
    }
    return added;
}

bool MeshNode::fileCamera(std::size_t index, Camera& out) const
{
    if (!mesh_ || index >= mesh_->cameras.size())
        return false;
    const FileCamera& fc = mesh_->cameras[index];
    const auto& w        = fc.world;
    out.position         = {w.m[0][3], w.m[1][3], w.m[2][3]};
    out.target           = out.position + w.transformVector({0.f, 0.f, -1.f}).normalized();
    out.up               = w.transformVector({0.f, 1.f, 0.f}).normalized();
    out.fovYDegrees      = fc.fovYDegrees;
    out.nearZ            = fc.nearZ;
    out.farZ             = fc.farZ;
    return true;
}

namespace
{

constexpr int kMaxPrefabDepth = 8;
thread_local int prefabDepth  = 0;

struct PrefabDepthGuard
{
    PrefabDepthGuard() { ++prefabDepth; }
    ~PrefabDepthGuard() { --prefabDepth; }
};

}

bool PrefabNode::setSource(std::string source, std::string* error)
{
    source_ = std::move(source);
    return reload(error);
}

bool PrefabNode::reload(std::string* error)
{
    removeAllChildren();
    if (source_.empty())
        return true;
    if (prefabDepth >= kMaxPrefabDepth)
    {
        if (error)
            *error = "los prefabs se contienen unos a otros demasiado hondo: " + source_;
        return false;
    }
    PrefabDepthGuard guard;
    Scene3D inner;
    if (!loadScene3DFile(inner, source_, error))
        return false;
    while (!inner.root().children().empty())
    {
        std::unique_ptr<Node3D> child = inner.root().removeChild(inner.root().children().front().get());
        addChild(std::move(child));
    }
    return true;
}

void LightNode::onDraw(Renderer3D& renderer)
{
    const math::Mat4& w = worldMatrix();
    LocalLight light;
    light.type              = type_;
    light.position          = {w.m[0][3], w.m[1][3], w.m[2][3]};
    light.direction         = w.transformVector({0.f, -1.f, 0.f});
    light.color             = color_;
    light.intensity         = intensity_;
    light.range             = range_;
    light.innerAngleDegrees = inner_;
    light.outerAngleDegrees = outer_;
    light.shadows           = castShadow_;
    light.shadowOpacity     = shadowOpacity_;
    renderer.addLight(light);
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
    const Mesh& drawn = posed_ ? *posed_ : (morphed_ ? *morphed_ : *mesh_);
    if (mesh_->parts.size() < 2 || partTextures_.size() != mesh_->parts.size())
    {
        if (!materialTextures_.empty())
            applyMaterial(style, mesh_->material, materialTextures_[0]);
        renderer.drawMesh(drawn, worldMatrix(), style);
        return;
    }
    const render::TextureHandle nodeTexture = style.texture;
    for (std::size_t i = 0; i < mesh_->parts.size(); ++i)
    {
        const SubMesh& part = mesh_->parts[i];
        MeshStyle partStyle = style;
        partStyle.color     = renderer::Color{part.baseColor[0] * color_.r, part.baseColor[1] * color_.g,
                                          part.baseColor[2] * color_.b, part.baseColor[3] * color_.a};
        if (nodeTexture != render::kInvalidHandle)
            partStyle.texture = nodeTexture;
        else
            partStyle.texture = partTextures_[i] ? partTextures_[i]->handle : render::kInvalidHandle;
        if (i < materialTextures_.size())
            applyMaterial(partStyle, part.material, materialTextures_[i]);
        renderer.drawMeshPart(drawn, part.first, part.count, worldMatrix(), partStyle);
    }
}

}
