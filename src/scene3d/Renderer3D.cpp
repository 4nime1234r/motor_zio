#include "zio/scene3d/Renderer3D.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

#include "zio/render/Device.h"

#include "mesh3d_frag.spv.h"
#include "mesh3d_vert.spv.h"
#include "shadow_frag.spv.h"

namespace zio::scene3d
{

namespace
{

struct Vertex
{
    float x, y, z, w;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
    float lx, ly, lz, ambient;
    float sx, sy, sz, sw;
    float shadowStrength, shadowPad;
};

constexpr std::size_t kMaxVertices = 3 * 30000;

constexpr float kOpaqueAlpha = 0.999f;

constexpr float kShadowCasterMargin = 30.f;
constexpr float kShadowNormalOffsetTexels = 1.5f;

struct Command
{
    const Mesh* mesh;
    math::Mat4 world;
    MeshStyle style;
    float distanceSq;
};

}

struct Renderer3D::Impl
{
    bool initialized = false;

    render::PipelineHandle opaquePipeline      = render::kInvalidHandle;
    render::PipelineHandle translucentPipeline = render::kInvalidHandle;
    render::PipelineHandle shadowDepthPipeline = render::kInvalidHandle;
    render::PipelineHandle overlayPipeline     = render::kInvalidHandle;
    render::PipelineHandle depthLinePipeline   = render::kInvalidHandle;
    render::BufferHandle vertexBuffer          = render::kInvalidHandle;
    render::TextureHandle whiteTexture         = render::kInvalidHandle;

    math::Mat4 viewProj;
    math::Mat4 lightViewProj;
    bool shadowsActive      = false;
    float shadowNormalOffset = 0.f;
    Camera camera;
    float aspect = 1.f;
    math::Vec3 cameraPos;
    Light light;
    math::Vec3 toLight = {0.f, 1.f, 0.f};
    std::vector<Command> commands;

    struct Line
    {
        math::Vec3 a, b;
        renderer::Color colorA, colorB;
        float widthPixels;
        LineMode mode;
    };
    std::vector<Line> lines;
    std::uint32_t viewportWidth  = 1;
    std::uint32_t viewportHeight = 1;
    float nearW                  = 0.1f;

    std::vector<Vertex> pending;
    render::PipelineHandle currentPipeline = render::kInvalidHandle;
    render::TextureHandle currentTexture   = render::kInvalidHandle;

    render::PipelineHandle makePipeline(bool depthWrite, bool cullBack, bool depthTest = true, bool shadowPass = false)
    {
        render::VertexAttribute attrs[] = {
            {"POSITION", 0, render::VertexFormat::Float4, offsetof(Vertex, x)},
            {"COLOR", 0, render::VertexFormat::Float4, offsetof(Vertex, r)},
            {"TEXCOORD", 0, render::VertexFormat::Float2, offsetof(Vertex, u)},
            {"NORMAL", 0, render::VertexFormat::Float3, offsetof(Vertex, nx)},
            {"TEXCOORD", 1, render::VertexFormat::Float4, offsetof(Vertex, lx)},
            {"TEXCOORD", 2, render::VertexFormat::Float4, offsetof(Vertex, sx)},
            {"TEXCOORD", 3, render::VertexFormat::Float2, offsetof(Vertex, shadowStrength)},
        };
        render::PipelineDesc desc{};
        desc.vertexShaderSpirv = zio_mesh3d_vert_spirv;
        desc.pixelShaderSpirv  = shadowPass ? std::span<const std::uint32_t>(zio_shadow_frag_spirv)
                                            : std::span<const std::uint32_t>(zio_mesh3d_frag_spirv);
        desc.attributes        = attrs;
        desc.attributeCount    = 7;
        desc.vertexStrideBytes = sizeof(Vertex);
        desc.depthTest         = depthTest;
        desc.depthWrite        = depthWrite;
        desc.cullBack          = cullBack;
        desc.shadowPass        = shadowPass;
        return render::Device::instance().createPipeline(desc);
    }

    bool init()
    {
        auto& device = render::Device::instance();
        if (!device.isInitialized())
            return false;

        opaquePipeline      = makePipeline(true, true);
        translucentPipeline = makePipeline(false, true);
        shadowDepthPipeline = makePipeline(true, false, true, true);
        overlayPipeline     = makePipeline(false, false, false);
        depthLinePipeline   = makePipeline(false, false, true);
        if (overlayPipeline == render::kInvalidHandle || depthLinePipeline == render::kInvalidHandle)
            return false;
        if (opaquePipeline == render::kInvalidHandle || translucentPipeline == render::kInvalidHandle ||
            shadowDepthPipeline == render::kInvalidHandle)
            return false;

        render::BufferDesc vb{};
        vb.type      = render::BufferType::Vertex;
        vb.usage     = render::BufferUsage::Dynamic;
        vb.sizeBytes = kMaxVertices * sizeof(Vertex);
        vertexBuffer = device.createBuffer(vb);
        if (vertexBuffer == render::kInvalidHandle)
            return false;

        const std::uint8_t white[4] = {255, 255, 255, 255};
        render::TextureDesc td{};
        td.width       = 1;
        td.height      = 1;
        td.pixelsRgba8 = white;
        whiteTexture   = device.createTexture(td);
        if (whiteTexture == render::kInvalidHandle)
            return false;

        pending.reserve(kMaxVertices);
        initialized = true;
        return true;
    }

    void flush()
    {
        if (pending.empty())
            return;
        auto& device = render::Device::instance();
        device.updateBuffer(vertexBuffer, pending.data(), static_cast<std::uint32_t>(pending.size() * sizeof(Vertex)));
        device.bindPipeline(currentPipeline);
        device.bindVertexBuffer(vertexBuffer, sizeof(Vertex));
        device.bindTexture(currentTexture);
        if (currentPipeline != shadowDepthPipeline)
            device.bindShadowMap();
        device.draw(static_cast<std::uint32_t>(pending.size()));
        pending.clear();
    }

    void prepareBatch(render::PipelineHandle pipeline, render::TextureHandle texture)
    {
        if (texture == render::kInvalidHandle)
            texture = whiteTexture;
        if (!pending.empty() && (pipeline != currentPipeline || texture != currentTexture))
            flush();
        else if (pending.size() + 3 > kMaxVertices)
            flush();
        currentPipeline = pipeline;
        currentTexture  = texture;
    }

    void emit(const Command& cmd, render::PipelineHandle pipeline)
    {
        prepareBatch(pipeline, cmd.style.texture);

        const math::Mat4 mvp  = viewProj * cmd.world;
        const float(&m)[4][4] = mvp.m;
        const float(&w)[4][4] = cmd.world.m;

        const float c00 = w[1][1] * w[2][2] - w[1][2] * w[2][1];
        const float c01 = w[1][2] * w[2][0] - w[1][0] * w[2][2];
        const float c02 = w[1][0] * w[2][1] - w[1][1] * w[2][0];
        const float c10 = w[0][2] * w[2][1] - w[0][1] * w[2][2];
        const float c11 = w[0][0] * w[2][2] - w[0][2] * w[2][0];
        const float c12 = w[0][1] * w[2][0] - w[0][0] * w[2][1];
        const float c20 = w[0][1] * w[1][2] - w[0][2] * w[1][1];
        const float c21 = w[0][2] * w[1][0] - w[0][0] * w[1][2];
        const float c22 = w[0][0] * w[1][1] - w[0][1] * w[1][0];
        const float det = w[0][0] * c00 + w[0][1] * c01 + w[0][2] * c02;
        const float sgn = det < 0.f ? -1.f : 1.f;

        const auto& verts = cmd.mesh->vertices;
        const auto& col   = cmd.style.color;
        const float(&lm)[4][4] = lightViewProj.m;
        for (std::size_t i = 0; i + 2 < verts.size(); i += 3)
        {
            if (pending.size() + 3 > kMaxVertices)
                flush();
            static constexpr std::size_t order[2][3] = {{0, 1, 2}, {0, 2, 1}};
            for (const std::size_t k : order[det < 0.f ? 1 : 0])
            {
                const Vertex3D& v = verts[i + k];
                const float x = v.position.x, y = v.position.y, z = v.position.z;
                math::Vec3 n{(c00 * v.normal.x + c01 * v.normal.y + c02 * v.normal.z) * sgn,
                             (c10 * v.normal.x + c11 * v.normal.y + c12 * v.normal.z) * sgn,
                             (c20 * v.normal.x + c21 * v.normal.y + c22 * v.normal.z) * sgn};
                n = n.normalized();
                float sx = 0.f, sy = 0.f, sz = 0.f, sw = 0.f;
                if (shadowsActive)
                {
                    const math::Vec3 wp = cmd.world.transformPoint(v.position) + n * shadowNormalOffset;
                    sx = lm[0][0] * wp.x + lm[0][1] * wp.y + lm[0][2] * wp.z + lm[0][3];
                    sy = lm[1][0] * wp.x + lm[1][1] * wp.y + lm[1][2] * wp.z + lm[1][3];
                    sz = lm[2][0] * wp.x + lm[2][1] * wp.y + lm[2][2] * wp.z + lm[2][3];
                    sw = lm[3][0] * wp.x + lm[3][1] * wp.y + lm[3][2] * wp.z + lm[3][3];
                }
                pending.push_back(Vertex{
                    m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
                    m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
                    m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3],
                    m[3][0] * x + m[3][1] * y + m[3][2] * z + m[3][3],
                    col.r, col.g, col.b, col.a,
                    v.u * cmd.style.uvScale.x, v.v * cmd.style.uvScale.y,
                    n.x, n.y, n.z,
                    toLight.x, toLight.y, toLight.z, light.ambient,
                    sx, sy, sz, sw,
                    shadowsActive ? light.shadowOpacity : 0.f, 0.f});
            }
        }
    }

    void emitLines(LineMode mode)
    {
        const render::PipelineHandle pipeline = mode == LineMode::DepthTested ? depthLinePipeline : overlayPipeline;
        constexpr float kDepthBias            = 1e-4f;

        bool prepared         = false;
        const float(&m)[4][4] = viewProj.m;
        const float width     = static_cast<float>(viewportWidth);
        const float height    = static_cast<float>(viewportHeight);
        for (const Line& line : lines)
        {
            if (line.mode != mode)
                continue;

            float p[2][4];
            renderer::Color c[2]     = {line.colorA, line.colorB};
            const math::Vec3 ends[2] = {line.a, line.b};
            for (int k = 0; k < 2; ++k)
            {
                const float x = ends[k].x, y = ends[k].y, z = ends[k].z;
                for (int r = 0; r < 4; ++r)
                    p[k][r] = m[r][0] * x + m[r][1] * y + m[r][2] * z + m[r][3];
            }

            const bool in0 = p[0][3] >= nearW;
            const bool in1 = p[1][3] >= nearW;
            if (!in0 && !in1)
                continue;
            if (in0 != in1)
            {
                const int inside  = in0 ? 0 : 1;
                const int outside = in0 ? 1 : 0;
                const float t     = (nearW - p[inside][3]) / (p[outside][3] - p[inside][3]);
                for (int r = 0; r < 4; ++r)
                    p[outside][r] = p[inside][r] + (p[outside][r] - p[inside][r]) * t;
                c[outside].r = c[inside].r + (c[outside].r - c[inside].r) * t;
                c[outside].g = c[inside].g + (c[outside].g - c[inside].g) * t;
                c[outside].b = c[inside].b + (c[outside].b - c[inside].b) * t;
                c[outside].a = c[inside].a + (c[outside].a - c[inside].a) * t;
            }

            const float ax = p[0][0] / p[0][3], ay = p[0][1] / p[0][3];
            const float bx = p[1][0] / p[1][3], by = p[1][1] / p[1][3];
            const float dx  = (bx - ax) * 0.5f * width;
            const float dy  = (by - ay) * 0.5f * height;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (!(len > 1e-3f) || !std::isfinite(len))
                continue;

            float za = 0.f, zb = 0.f;
            if (mode == LineMode::DepthTested)
            {
                za = std::clamp(p[0][2] / p[0][3] - kDepthBias, 0.f, 1.f);
                zb = std::clamp(p[1][2] / p[1][3] - kDepthBias, 0.f, 1.f);
            }

            const float ox    = (-dy / len) * line.widthPixels * 0.5f * (2.f / width);
            const float oy    = (dx / len) * line.widthPixels * 0.5f * (2.f / height);
            const float qx[4] = {ax - ox, ax + ox, bx + ox, bx - ox};
            const float qy[4] = {ay - oy, ay + oy, by + oy, by - oy};
            const float qz[4] = {za, za, zb, zb};
            const int qc[4]   = {0, 0, 1, 1};
            static constexpr int kQuad[6] = {0, 1, 2, 0, 2, 3};

            if (!prepared)
            {
                prepareBatch(pipeline, render::kInvalidHandle);
                prepared = true;
            }
            if (pending.size() + 6 > kMaxVertices)
                flush();
            for (const int idx : kQuad)
            {
                const renderer::Color& col = c[qc[idx]];
                pending.push_back(Vertex{qx[idx], qy[idx], qz[idx], 1.f, col.r, col.g, col.b, col.a, 0.f, 0.f, 0.f,
                                         1.f, 0.f, 0.f, 1.f, 0.f, 1.f});
            }
        }
    }

    void buildLightMatrix()
    {
        const math::Vec3 dir = light.direction.normalized();
        const math::Vec3 up  = std::fabs(dir.y) > 0.95f ? math::Vec3{0.f, 0.f, 1.f} : math::Vec3{0.f, 1.f, 0.f};

        const math::Vec3 forward = (camera.target - camera.position).normalized();
        const math::Vec3 right   = forward.cross(camera.up).normalized();
        const math::Vec3 camUp   = right.cross(forward);
        const float farSlice     = std::clamp(light.shadowDistance, camera.nearZ + 0.1f, camera.farZ);
        const float tanHalf      = std::tan(camera.fovYDegrees * 0.5f * (3.14159265358979323846f / 180.f));

        math::Vec3 corners[8];
        int cornerIndex = 0;
        for (const float d : {camera.nearZ, farSlice})
        {
            const float halfH      = d * tanHalf;
            const float halfW      = halfH * aspect;
            const math::Vec3 mid   = camera.position + forward * d;
            for (const float sxSign : {-1.f, 1.f})
                for (const float sySign : {-1.f, 1.f})
                    corners[cornerIndex++] = mid + right * (halfW * sxSign) + camUp * (halfH * sySign);
        }

        math::Vec3 center = math::Vec3::zero();
        for (const math::Vec3& p : corners)
            center = center + p;
        center = center * (1.f / 8.f);
        float radius = 0.f;
        for (const math::Vec3& p : corners)
            radius = std::max(radius, (p - center).length());
        const float half = std::max(std::ceil(radius), 1.f);

        const float size  = static_cast<float>(render::Device::instance().shadowMapSize());
        const float texel = 2.f * half / size;

        const math::Mat4 rotation = math::Mat4::lookAt(math::Vec3::zero(), dir, up);
        math::Vec3 snapped        = rotation.transformPoint(center);
        snapped.x                 = std::floor(snapped.x / texel) * texel;
        snapped.y                 = std::floor(snapped.y / texel) * texel;
        math::Vec3 shift          = math::Vec3::zero();
        shift.x                   = -snapped.x;
        shift.y                   = -snapped.y;
        const math::Mat4 view     = math::Mat4::translation(shift) * rotation;

        const float nearD = -(half + kShadowCasterMargin);
        const float farD  = half;
        math::Mat4 proj;
        proj.m[0][0] = 1.f / half;
        proj.m[1][1] = 1.f / half;
        proj.m[2][2] = -1.f / (farD - nearD);
        proj.m[2][3] = -nearD / (farD - nearD);

        lightViewProj       = proj * view;
        shadowNormalOffset  = texel * kShadowNormalOffsetTexels;
    }

    void emitDepth(const Command& cmd)
    {
        prepareBatch(shadowDepthPipeline, render::kInvalidHandle);

        const math::Mat4 mvp  = lightViewProj * cmd.world;
        const float(&m)[4][4] = mvp.m;
        const auto& verts     = cmd.mesh->vertices;
        for (std::size_t i = 0; i + 2 < verts.size(); i += 3)
        {
            if (pending.size() + 3 > kMaxVertices)
                flush();
            for (std::size_t k = 0; k < 3; ++k)
            {
                const math::Vec3& p = verts[i + k].position;
                pending.push_back(Vertex{
                    m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                    m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                    m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3],
                    m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3]});
            }
        }
    }
};

Renderer3D::Renderer3D() : impl_(std::make_unique<Impl>()) {}
Renderer3D::~Renderer3D() = default;

Renderer3D& Renderer3D::instance()
{
    static Renderer3D global;
    return global;
}

bool Renderer3D::ensureInitialized()
{
    return impl_->initialized || impl_->init();
}

void Renderer3D::begin(const Camera& camera, const Light& light, std::uint32_t viewportWidth,
                       std::uint32_t viewportHeight)
{
    if (!ensureInitialized())
        return;
    const float aspect =
        viewportHeight > 0 ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight) : 1.f;
    impl_->camera    = camera;
    impl_->aspect    = aspect;
    impl_->viewProj  = camera.projectionMatrix(aspect) * camera.viewMatrix();
    impl_->cameraPos = camera.position;
    impl_->light     = light;
    impl_->light.ambient       = std::clamp(light.ambient, 0.f, 1.f);
    impl_->light.shadowOpacity = std::clamp(light.shadowOpacity, 0.f, 1.f);
    impl_->toLight   = (-light.direction).normalized();
    impl_->viewportWidth  = std::max<std::uint32_t>(viewportWidth, 1);
    impl_->viewportHeight = std::max<std::uint32_t>(viewportHeight, 1);
    impl_->nearW          = std::max(camera.nearZ, 1e-4f);
    impl_->commands.clear();
    impl_->lines.clear();
    impl_->pending.clear();
}

void Renderer3D::drawLine(math::Vec3 a, math::Vec3 b, renderer::Color color, float widthPixels, LineMode mode)
{
    drawLine(a, b, color, color, widthPixels, mode);
}

void Renderer3D::drawLine(math::Vec3 a, math::Vec3 b, renderer::Color colorA, renderer::Color colorB,
                          float widthPixels, LineMode mode)
{
    if (!impl_->initialized || !(widthPixels > 0.f))
        return;
    impl_->lines.push_back(Impl::Line{a, b, colorA, colorB, widthPixels, mode});
}

void Renderer3D::drawMesh(const Mesh& mesh, const math::Mat4& world, const MeshStyle& style)
{
    if (!impl_->initialized || mesh.vertices.empty())
        return;
    const math::Vec3 origin{world.m[0][3], world.m[1][3], world.m[2][3]};
    const math::Vec3 d = origin - impl_->cameraPos;
    impl_->commands.push_back(Command{&mesh, world, style, d.dot(d)});
}

void Renderer3D::end()
{
    auto& s = *impl_;
    if (!s.initialized)
        return;

    s.shadowsActive = false;
    if (s.light.shadows && s.light.shadowOpacity > 0.f && s.light.direction.length() > 1e-4f)
    {
        s.buildLightMatrix();
        if (render::Device::instance().beginShadowPass())
        {
            for (const Command& c : s.commands)
                if (c.style.castShadow && c.style.color.a >= kOpaqueAlpha)
                    s.emitDepth(c);
            s.flush();
            render::Device::instance().endShadowPass();
            s.shadowsActive = true;
        }
    }

    for (const Command& c : s.commands)
        if (c.style.color.a >= kOpaqueAlpha)
            s.emit(c, s.opaquePipeline);
    s.flush();

    s.emitLines(LineMode::DepthTested);
    s.flush();

    std::vector<const Command*> translucent;
    for (const Command& c : s.commands)
        if (c.style.color.a < kOpaqueAlpha)
            translucent.push_back(&c);
    std::stable_sort(translucent.begin(), translucent.end(),
                     [](const Command* a, const Command* b) { return a->distanceSq > b->distanceSq; });
    for (const Command* c : translucent)
        s.emit(*c, s.translucentPipeline);
    s.flush();

    s.emitLines(LineMode::Overlay);
    s.flush();

    s.commands.clear();
    s.lines.clear();
}

}
