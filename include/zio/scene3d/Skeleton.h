#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/math/Mat4.h"
#include "zio/math/Quaternion.h"
#include "zio/math/Vec3.h"

namespace zio::scene3d
{

class Mesh;

struct SkinJoint
{
    std::string name;
    int parent = -1;
    math::Vec3 translation       = math::Vec3::zero();
    math::Quaternion rotation    = math::Quaternion::identity();
    math::Vec3 scale             = math::Vec3::one();
    math::Mat4 parentTransform   = math::Mat4::identity();
    math::Mat4 inverseBind       = math::Mat4::identity();
};

enum class AnimationPath
{
    Translation,
    Rotation,
    Scale
};

struct AnimationChannel
{
    int joint          = 0;
    AnimationPath path = AnimationPath::Translation;
    bool step          = false;
    std::vector<float> times;
    std::vector<float> values;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.f;
    std::vector<AnimationChannel> channels;
};

struct VertexSkin
{
    std::uint16_t joints[4] = {0, 0, 0, 0};
    float weights[4]        = {0.f, 0.f, 0.f, 0.f};
};

struct ZIO_API Skin
{
    std::vector<SkinJoint> joints;
    std::vector<AnimationClip> clips;
    std::vector<VertexSkin> vertices;

    const AnimationClip* findClip(const std::string& name) const;

    void skinMatrices(const AnimationClip* clip, float time, std::vector<math::Mat4>& out) const;

    void apply(const Mesh& bind, const std::vector<math::Mat4>& skinMatrices, Mesh& out) const;
};

}
