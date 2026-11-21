#pragma once

#include <cstdint>
#include <memory>

#include "zio/core/Platform.h"
#include "zio/math/Mat4.h"
#include "zio/math/Vec2.h"
#include "zio/math/Vec3.h"
#include "zio/render/Types.h"
#include "zio/renderer/Color.h"
#include "zio/scene3d/Camera.h"
#include "zio/scene3d/Mesh.h"

namespace zio::scene3d
{

enum class LineMode
{
    Overlay,
    DepthTested
};

struct Light
{
    math::Vec3 direction = {-0.4f, -1.f, -0.6f};
    float ambient = 0.25f;

    bool shadows = false;
    float shadowOpacity = 0.45f;
    float shadowDistance = 60.f;
};

struct MeshStyle
{
    renderer::Color color = renderer::Color::white();
    render::TextureHandle texture = render::kInvalidHandle;
    math::Vec2 uvScale            = math::Vec2::one();
    bool castShadow               = true;
};

class ZIO_API Renderer3D
{
public:
    Renderer3D();
    ~Renderer3D();

    Renderer3D(const Renderer3D&)            = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    static Renderer3D& instance();

    bool ensureInitialized();

    void begin(const Camera& camera, const Light& light, std::uint32_t viewportWidth, std::uint32_t viewportHeight,
               std::uint32_t viewportX = 0, std::uint32_t viewportY = 0);
    void drawMesh(const Mesh& mesh, const math::Mat4& world, const MeshStyle& style);
    void drawMesh(const Mesh& mesh, const math::Mat4& world, renderer::Color color)
    {
        MeshStyle style;
        style.color = color;
        drawMesh(mesh, world, style);
    }
    void drawLine(math::Vec3 a, math::Vec3 b, renderer::Color color, float widthPixels = 2.f,
                  LineMode mode = LineMode::Overlay);
    void drawLine(math::Vec3 a, math::Vec3 b, renderer::Color colorA, renderer::Color colorB, float widthPixels,
                  LineMode mode = LineMode::Overlay);
    void end();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
