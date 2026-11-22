#pragma once

#include <algorithm>
#include <memory>
#include <variant>
#include <vector>

#include "zio/math/Vec3.h"

namespace zio::physics3d
{

struct SphereShape
{
    float radius = 0.5f;
};

struct BoxShape
{
    math::Vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

struct CapsuleShape
{
    float radius     = 0.5f;
    float halfHeight = 0.5f;
};

struct MeshData
{
    std::vector<math::Vec3> triangles;
    math::Vec3 boundsMin = math::Vec3::zero();
    math::Vec3 boundsMax = math::Vec3::zero();
};

struct MeshShape
{
    std::shared_ptr<const MeshData> data;
};

inline MeshShape makeMeshShape(std::vector<math::Vec3> triangles)
{
    auto data = std::make_shared<MeshData>();
    data->triangles.assign(triangles.begin(), triangles.end() - static_cast<std::ptrdiff_t>(triangles.size() % 3));
    if (!data->triangles.empty())
    {
        data->boundsMin = data->triangles.front();
        data->boundsMax = data->triangles.front();
        for (const math::Vec3& v : data->triangles)
        {
            data->boundsMin = {std::min(data->boundsMin.x, v.x), std::min(data->boundsMin.y, v.y),
                               std::min(data->boundsMin.z, v.z)};
            data->boundsMax = {std::max(data->boundsMax.x, v.x), std::max(data->boundsMax.y, v.y),
                               std::max(data->boundsMax.z, v.z)};
        }
    }
    return MeshShape{std::move(data)};
}

using Shape3D = std::variant<SphereShape, BoxShape, CapsuleShape, MeshShape>;

}
