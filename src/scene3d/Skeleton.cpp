#include "zio/scene3d/Skeleton.h"

#include <algorithm>
#include <cmath>

#include "zio/scene3d/Mesh.h"

namespace zio::scene3d
{

namespace
{

void sampleChannel(const AnimationChannel& c, float time, std::size_t width, float* out)
{
    const std::size_t stride = c.interpolation == Interpolation::CubicSpline ? width * 3 : width;
    const std::size_t valueOffset = c.interpolation == Interpolation::CubicSpline ? width : 0;
    const auto value = [&](std::size_t key, std::size_t k) { return c.values[key * stride + valueOffset + k]; };

    const std::size_t count = c.times.size();
    if (count == 1 || time <= c.times.front())
    {
        for (std::size_t k = 0; k < width; ++k)
            out[k] = value(0, k);
        return;
    }
    if (time >= c.times.back())
    {
        for (std::size_t k = 0; k < width; ++k)
            out[k] = value(count - 1, k);
        return;
    }
    const auto it         = std::upper_bound(c.times.begin(), c.times.end(), time);
    const std::size_t k1  = static_cast<std::size_t>(it - c.times.begin());
    const std::size_t k0  = k1 - 1;
    const float span      = c.times[k1] - c.times[k0];
    const float s         = span > 0.f ? (time - c.times[k0]) / span : 0.f;

    if (c.interpolation == Interpolation::Step)
    {
        for (std::size_t k = 0; k < width; ++k)
            out[k] = value(k0, k);
        return;
    }
    if (c.interpolation == Interpolation::CubicSpline)
    {
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.f * s3 - 3.f * s2 + 1.f;
        const float h10 = s3 - 2.f * s2 + s;
        const float h01 = -2.f * s3 + 3.f * s2;
        const float h11 = s3 - s2;
        for (std::size_t k = 0; k < width; ++k)
        {
            const float v0 = c.values[k0 * stride + width + k];
            const float b0 = c.values[k0 * stride + width * 2 + k] * span;
            const float a1 = c.values[k1 * stride + k] * span;
            const float v1 = c.values[k1 * stride + width + k];
            out[k]         = h00 * v0 + h10 * b0 + h01 * v1 + h11 * a1;
        }
        return;
    }
    if (width == 4)
    {
        const math::Quaternion a{value(k0, 0), value(k0, 1), value(k0, 2), value(k0, 3)};
        const math::Quaternion b{value(k1, 0), value(k1, 1), value(k1, 2), value(k1, 3)};
        const math::Quaternion q = math::Quaternion::slerp(a.normalized(), b.normalized(), s);
        out[0] = q.x;
        out[1] = q.y;
        out[2] = q.z;
        out[3] = q.w;
        return;
    }
    for (std::size_t k = 0; k < width; ++k)
        out[k] = value(k0, k) + (value(k1, k) - value(k0, k)) * s;
}

}

const AnimationClip* Skin::findClip(const std::string& name) const
{
    for (const AnimationClip& clip : clips)
        if (clip.name == name)
            return &clip;
    return nullptr;
}

void Skin::restPose(std::vector<JointPose>& out) const
{
    out.resize(joints.size());
    for (std::size_t i = 0; i < joints.size(); ++i)
        out[i] = {joints[i].translation, joints[i].rotation, joints[i].scale};
}

void Skin::samplePose(const AnimationClip* clip, float time, std::vector<JointPose>& out) const
{
    restPose(out);
    if (!clip)
        return;
    for (const AnimationChannel& c : clip->channels)
    {
        if (c.joint < 0 || static_cast<std::size_t>(c.joint) >= joints.size() || c.times.empty())
            continue;
        const std::size_t width  = c.path == AnimationPath::Rotation ? 4 : 3;
        const std::size_t stride = c.interpolation == Interpolation::CubicSpline ? width * 3 : width;
        if (c.values.size() < c.times.size() * stride)
            continue;
        float v[4] = {};
        sampleChannel(c, time, width, v);
        JointPose& p = out[static_cast<std::size_t>(c.joint)];
        switch (c.path)
        {
        case AnimationPath::Translation:
            p.translation = {v[0], v[1], v[2]};
            break;
        case AnimationPath::Scale:
            p.scale = {v[0], v[1], v[2]};
            break;
        case AnimationPath::Rotation:
            p.rotation = math::Quaternion{v[0], v[1], v[2], v[3]}.normalized();
            break;
        }
    }
}

void Skin::blendPoses(const std::vector<JointPose>& from, const std::vector<JointPose>& to, float t,
                      std::vector<JointPose>& out)
{
    const std::size_t n = std::min(from.size(), to.size());
    out.resize(n);
    t = std::clamp(t, 0.f, 1.f);
    for (std::size_t i = 0; i < n; ++i)
    {
        const JointPose& a = from[i];
        const JointPose& b = to[i];
        out[i].translation = {a.translation.x + (b.translation.x - a.translation.x) * t,
                              a.translation.y + (b.translation.y - a.translation.y) * t,
                              a.translation.z + (b.translation.z - a.translation.z) * t};
        out[i].scale       = {a.scale.x + (b.scale.x - a.scale.x) * t, a.scale.y + (b.scale.y - a.scale.y) * t,
                              a.scale.z + (b.scale.z - a.scale.z) * t};
        out[i].rotation    = math::Quaternion::slerp(a.rotation, b.rotation, t);
    }
}

void Skin::skinMatrices(const std::vector<JointPose>& pose, std::vector<math::Mat4>& out) const
{
    const std::size_t count = joints.size();
    std::vector<math::Mat4> global(count);
    std::vector<char> done(count, 0);
    const auto compute = [&](auto&& self, std::size_t i, int depth) -> void {
        if (done[i])
            return;
        const JointPose& p     = pose[i];
        const math::Mat4 local = math::Mat4::translation(p.translation) * math::Mat4::rotation(p.rotation) *
                                 math::Mat4::scale(p.scale);
        const int parent = joints[i].parent;
        if (parent >= 0 && static_cast<std::size_t>(parent) < count && depth < 256)
        {
            self(self, static_cast<std::size_t>(parent), depth + 1);
            global[i] = global[static_cast<std::size_t>(parent)] * local;
        }
        else
        {
            global[i] = joints[i].parentTransform * local;
        }
        done[i] = 1;
    };
    for (std::size_t i = 0; i < count; ++i)
        compute(compute, i, 0);

    out.resize(count);
    for (std::size_t i = 0; i < count; ++i)
        out[i] = global[i] * joints[i].inverseBind;
}

void Skin::skinMatrices(const AnimationClip* clip, float time, std::vector<math::Mat4>& out) const
{
    std::vector<JointPose> pose;
    samplePose(clip, time, pose);
    skinMatrices(pose, out);
}

void Skin::apply(const Mesh& bind, const std::vector<math::Mat4>& skinMatrices, Mesh& out) const
{
    const std::size_t n = bind.vertices.size();
    out.vertices.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const Vertex3D& src = bind.vertices[i];
        Vertex3D& dst       = out.vertices[i];
        dst                 = src;
        if (i >= vertices.size())
            continue;
        const VertexSkin& vs = vertices[i];
        float total          = 0.f;
        for (int k = 0; k < kMaxInfluences; ++k)
            total += vs.weights[k];
        if (!(total > 0.f))
            continue;
        math::Vec3 p{0.f, 0.f, 0.f}, nrm{0.f, 0.f, 0.f};
        for (int k = 0; k < kMaxInfluences; ++k)
        {
            const float w = vs.weights[k] / total;
            if (w <= 0.f || vs.joints[k] >= skinMatrices.size())
                continue;
            const math::Mat4& m = skinMatrices[vs.joints[k]];
            const math::Vec3 tp = m.transformPoint(src.position);
            const math::Vec3 tn = m.transformVector(src.normal);
            p.x += tp.x * w;
            p.y += tp.y * w;
            p.z += tp.z * w;
            nrm.x += tn.x * w;
            nrm.y += tn.y * w;
            nrm.z += tn.z * w;
        }
        dst.position = p;
        dst.normal   = nrm.normalized();
    }
}

}
