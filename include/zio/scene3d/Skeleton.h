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

constexpr int kMaxInfluences = 8;

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

enum class Interpolation
{
    Linear,
    Step,
    CubicSpline
};

struct AnimationChannel
{
    int joint                = 0;
    AnimationPath path       = AnimationPath::Translation;
    Interpolation interpolation = Interpolation::Linear;
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
    std::uint16_t joints[kMaxInfluences] = {};
    float weights[kMaxInfluences]        = {};
};

struct JointPose
{
    math::Vec3 translation;
    math::Quaternion rotation;
    math::Vec3 scale;
};

struct ZIO_API Skin
{
    std::vector<SkinJoint> joints;
    std::vector<AnimationClip> clips;
    std::vector<VertexSkin> vertices;

    const AnimationClip* findClip(const std::string& name) const;

    void restPose(std::vector<JointPose>& out) const;
    void samplePose(const AnimationClip* clip, float time, std::vector<JointPose>& out) const;
    static void blendPoses(const std::vector<JointPose>& from, const std::vector<JointPose>& to, float t,
                           std::vector<JointPose>& out);
    std::vector<float> jointMask(const std::string& rootJoint) const;
    static void blendMasked(std::vector<JointPose>& base, const std::vector<JointPose>& overlay,
                            const std::vector<float>& weights);
    void skinMatrices(const std::vector<JointPose>& pose, std::vector<math::Mat4>& out) const;
    void skinMatrices(const AnimationClip* clip, float time, std::vector<math::Mat4>& out) const;

    void apply(const Mesh& bind, const std::vector<math::Mat4>& skinMatrices, Mesh& out) const;
};

}
