#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/math/Mat4.h"
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
    float r = 1.f;
    float g = 1.f;
    float b = 1.f;
    float a = 1.f;
};

enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

inline constexpr const char* kEmbeddedPrefix = "embedded:";

struct Material
{
    float metallic          = 0.f;
    float roughness         = 1.f;
    float emissive[3]       = {0.f, 0.f, 0.f};
    float normalScale       = 1.f;
    float occlusionStrength = 1.f;
    AlphaMode alphaMode     = AlphaMode::Opaque;
    float alphaCutoff       = 0.5f;
    bool doubleSided        = false;
    std::string normalTexture;
    std::string metallicRoughnessTexture;
    std::string emissiveTexture;
    std::string occlusionTexture;
};

struct SubMesh
{
    std::size_t first = 0;
    std::size_t count = 0;
    float baseColor[4] = {1.f, 1.f, 1.f, 1.f};
    std::string texturePath;
    Material material;
};

struct MorphTarget
{
    std::string name;
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
};

enum class FileLightType
{
    Point,
    Spot,
    Directional
};

struct MorphClip
{
    std::string name;
    std::vector<float> times;
    std::vector<float> values;
    bool step      = false;
    float duration = 0.f;
};

struct FileLight
{
    std::string name;
    FileLightType type = FileLightType::Point;
    float color[3]     = {1.f, 1.f, 1.f};
    float intensity    = 1.f;
    float range        = 0.f;
    float innerAngleDegrees = 0.f;
    float outerAngleDegrees = 45.f;
    math::Mat4 world;
};

struct FileCamera
{
    std::string name;
    bool orthographic  = false;
    float fovYDegrees  = 60.f;
    float nearZ        = 0.1f;
    float farZ         = 1000.f;
    math::Mat4 world;
};

struct ZIO_API GpuId
{
    std::uint64_t value = 0;

    GpuId() = default;
    GpuId(const GpuId&) {}
    GpuId& operator=(const GpuId&) { return *this; }
    ~GpuId();
};

class ZIO_API Mesh
{
public:
    std::vector<Vertex3D> vertices;
    std::vector<SubMesh> parts;
    float baseColor[4] = {1.f, 1.f, 1.f, 1.f};
    std::string texturePath;
    Material material;
    std::map<std::string, std::vector<std::uint8_t>> embeddedImages;
    std::vector<MorphTarget> morphTargets;
    std::vector<float> morphWeights;
    std::vector<MorphClip> morphClips;
    std::vector<FileCamera> cameras;
    std::vector<FileLight> lights;
    std::shared_ptr<Skin> skin;
    bool dynamic = false;
    mutable GpuId gpu;

    static std::shared_ptr<Mesh> cube(float size = 1.f);

    static std::shared_ptr<Mesh> loadGltf(const std::string& path, std::string* error = nullptr);

    static std::shared_ptr<Mesh> loadObj(const std::string& path, std::string* error = nullptr);

    static std::shared_ptr<Mesh> fromSource(std::string_view source, std::string* error = nullptr);
};

}
