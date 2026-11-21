#include "zio/scene3d/Skeleton.h"

#include <algorithm>
#include <cmath>

#include "zio/scene3d/Mesh.h"

namespace zio::scene3d
{

namespace
{

struct Sample
{
    std::size_t index = 0;
    std::size_t next  = 0;
    float t           = 0.f;
};

Sample locate(const std::vector<float>& times, float time)
{
    Sample s;
    if (times.size() < 2 || time <= times.front())
        return s;
    if (time >= times.back())
    {
        s.index = s.next = times.size() - 1;
        return s;
    }
    const auto it = std::upper_bound(times.begin(), times.end(), time);
    s.next        = static_cast<std::size_t>(it - times.begin());
    s.index       = s.next - 1;
    const float span = times[s.next] - times[s.index];
    s.t              = span > 0.f ? (time - times[s.index]) / span : 0.f;
    return s;
}

math::Vec3 sampleVec3(const AnimationChannel& c, float time)
{
    const Sample s = locate(c.times, time);
    const auto at  = [&](std::size_t i) {
        return math::Vec3{c.values[i * 3], c.values[i * 3 + 1], c.values[i * 3 + 2]};
    };
    const math::Vec3 a = at(s.index);
    if (c.step || s.index == s.next)
        return a;
    const math::Vec3 b = at(s.next);
    return {a.x + (b.x - a.x) * s.t, a.y + (b.y - a.y) * s.t, a.z + (b.z - a.z) * s.t};
}

math::Quaternion sampleQuat(const AnimationChannel& c, float time)
{
    const Sample s = locate(c.times, time);
    const auto at  = [&](std::size_t i) {
        return math::Quaternion{c.values[i * 4], c.values[i * 4 + 1], c.values[i * 4 + 2], c.values[i * 4 + 3]};
    };
    const math::Quaternion a = at(s.index);
    if (c.step || s.index == s.next)
        return a.normalized();
    return math::Quaternion::slerp(a.normalized(), at(s.next).normalized(), s.t);
}

}

const AnimationClip* Skin::findClip(const std::string& name) const
{
    for (const AnimationClip& clip : clips)
        if (clip.name == name)
            return &clip;
    return nullptr;
}

void Skin::skinMatrices(const AnimationClip* clip, float time, std::vector<math::Mat4>& out) const
{
    const std::size_t count = joints.size();
    std::vector<math::Vec3> translation(count), scale(count);
    std::vector<math::Quaternion> rotation(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        translation[i] = joints[i].translation;
        rotation[i]    = joints[i].rotation;
        scale[i]       = joints[i].scale;
    }

    if (clip)
    {
        for (const AnimationChannel& c : clip->channels)
        {
            if (c.joint < 0 || static_cast<std::size_t>(c.joint) >= count || c.times.empty())
                continue;
            const std::size_t j     = static_cast<std::size_t>(c.joint);
            const std::size_t width = c.path == AnimationPath::Rotation ? 4 : 3;
            if (c.values.size() < c.times.size() * width)
                continue;
            switch (c.path)
            {
            case AnimationPath::Translation:
                translation[j] = sampleVec3(c, time);
                break;
            case AnimationPath::Scale:
                scale[j] = sampleVec3(c, time);
                break;
            case AnimationPath::Rotation:
                rotation[j] = sampleQuat(c, time);
                break;
            }
        }
    }

    std::vector<math::Mat4> global(count);
    std::vector<char> done(count, 0);
    const auto compute = [&](auto&& self, std::size_t i, int depth) -> void {
        if (done[i])
            return;
        const math::Mat4 local =
            math::Mat4::translation(translation[i]) * math::Mat4::rotation(rotation[i]) * math::Mat4::scale(scale[i]);
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
        const float total    = vs.weights[0] + vs.weights[1] + vs.weights[2] + vs.weights[3];
        if (!(total > 0.f))
            continue;
        math::Vec3 p{0.f, 0.f, 0.f}, nrm{0.f, 0.f, 0.f};
        for (int k = 0; k < 4; ++k)
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
