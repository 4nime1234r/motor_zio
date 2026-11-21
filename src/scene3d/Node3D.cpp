#include "zio/scene3d/Node3D.h"

#include <algorithm>
#include <cmath>

#include "zio/scene3d/Renderer3D.h"

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

void Node3D::runScripts(float dt)
{
    for (std::size_t i = 0; i < scripts_.size(); ++i)
    {
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
    color_      = renderer::Color{mesh_->baseColor[0], mesh_->baseColor[1], mesh_->baseColor[2], mesh_->baseColor[3]};
    if (!mesh_->texturePath.empty())
        setTexturePath(mesh_->texturePath);
    return true;
}

void MeshNode::setTexturePath(std::string path)
{
    texture_ = path.empty() ? nullptr
                            : renderer::TextureCache::instance().loadSync(path, render::TextureFilter::Linear,
                                                                           render::TextureWrap::Repeat);
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
    mesh_->skin->apply(*mesh_, matrices, *posed_);
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
    renderer.drawMesh(posed_ ? *posed_ : *mesh_, worldMatrix(), style);
}

}
