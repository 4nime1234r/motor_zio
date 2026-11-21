#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/math/Vec3.h"
#include "zio/scene3d/Skeleton.h"

namespace zio::scene3d
{

struct Vertex3D
{
    math::Vec3 position;
    math::Vec3 normal;
    float u = 0.f;
    float v = 0.f;
};

class ZIO_API Mesh
{
public:
    std::vector<Vertex3D> vertices;
    float baseColor[4] = {1.f, 1.f, 1.f, 1.f};
    std::string texturePath;
    std::shared_ptr<Skin> skin;

    static std::shared_ptr<Mesh> cube(float size = 1.f);

    static std::shared_ptr<Mesh> loadGltf(const std::string& path, std::string* error = nullptr);

    static std::shared_ptr<Mesh> loadObj(const std::string& path, std::string* error = nullptr);

    static std::shared_ptr<Mesh> fromSource(std::string_view source, std::string* error = nullptr);
};

}
