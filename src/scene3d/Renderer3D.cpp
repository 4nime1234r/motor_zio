#include "zio/scene3d/Renderer3D.h"

#include <algorithm>
#include <atomic>
#include <tuple>
#include <unordered_map>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

#include "zio/render/Device.h"
#include "zio/renderer/Texture.h"

#include "mesh3d_frag.spv.h"
#include "mesh3d_inst_vert.spv.h"
#include "mesh3d_vert.spv.h"
#include "shadow_frag.spv.h"
#include "shadow_inst_vert.spv.h"
#include "bright_frag.spv.h"
#include "blur_frag.spv.h"
#include "composite_frag.spv.h"
#include "post_vert.spv.h"
#include "sky_frag.spv.h"
#include "sky_vert.spv.h"

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
    float wx, wy, wz;
    float lit, flags;
    float metallic, roughness, normalScale, occlusion;
    float er, eg, eb, pad;
};

struct MaterialKey
{
    render::TextureHandle texture[5] = {};

    bool operator==(const MaterialKey& other) const
    {
        for (int i = 0; i < 5; ++i)
            if (texture[i] != other.texture[i])
                return false;
        return true;
    }
    bool operator!=(const MaterialKey& other) const { return !(*this == other); }
};

constexpr int kMaxLights = 9;
constexpr int kMaxTiles  = 13;

struct SceneBlock
{
    float head[4];
    float camPos[4];
    float tileMat[kMaxTiles][16];
    float tileRect[kMaxTiles][4];
    float tileParam[kMaxTiles][4];
    float lightPos[kMaxLights][4];
    float lightColor[kMaxLights][4];
    float lightDir[kMaxLights][4];
    float lightMisc[kMaxLights][4];
    float viewProj[16];
    float camRight[4];
    float camUp[4];
    float camForward[4];
    float skyZenith[4];
    float skyHorizon[4];
    float skyGround[4];
    float fogColor[4];
    float fogParams[4];
};

struct InstanceData
{
    float row[3][4];
    float normalRow[3][4];
    float color[4];
    float material[4];
    float emissive[4];
    float flagsUv[4];
};

constexpr std::size_t kMaxInstances = 2048;

struct MeshBuffer
{
    render::BufferHandle handle = render::kInvalidHandle;
    std::size_t vertexCount     = 0;
};

std::unordered_map<std::uint64_t, MeshBuffer>& meshBuffers()
{
    static auto* buffers = new std::unordered_map<std::uint64_t, MeshBuffer>();
    return *buffers;
}

float materialFlags(const MeshStyle& style)
{
    float flags = 0.f;
    if (style.normalTexture != render::kInvalidHandle)
        flags += 1.f;
    if (style.metallicRoughnessTexture != render::kInvalidHandle)
        flags += 2.f;
    if (style.emissiveTexture != render::kInvalidHandle)
        flags += 4.f;
    if (style.occlusionTexture != render::kInvalidHandle)
        flags += 8.f;
    if (style.alphaMode == AlphaMode::Mask)
        flags += 16.f;
    if (style.doubleSided)
        flags += 32.f;
    return flags;
}

struct Tile
{
    math::Mat4 viewProj;
    std::uint32_t x = 0, y = 0, size = 0;
    float normalOffset = 0.f;
    float bias         = 0.f;
    float nearZ        = 0.f;
    float farZ         = 0.f;
};

constexpr std::uint32_t kLocalTileSize = 1024;
constexpr int kLocalTileSlots          = 12;

constexpr std::size_t kMaxVertices = 3 * 30000;

constexpr float kOpaqueAlpha = 0.999f;

bool isTranslucent(const MeshStyle& style)
{
    return style.alphaMode == AlphaMode::Blend || style.color.a < kOpaqueAlpha;
}

constexpr float kShadowCasterMargin = 30.f;
constexpr float kShadowNormalOffsetTexels = 1.5f;

struct Command
{
    const Mesh* mesh;
    math::Mat4 world;
    MeshStyle style;
    float distanceSq;
    std::size_t first;
    std::size_t count;
    bool boundsReady = false;
    bool boundsValid = false;
    math::Vec3 center;
    float radius = 0.f;
};

struct Frustum
{
    float planes[6][4] = {};

    void build(const math::Mat4& viewProj)
    {
        const float(&m)[4][4] = viewProj.m;
        for (int c = 0; c < 4; ++c)
        {
            planes[0][c] = m[3][c] + m[0][c];
            planes[1][c] = m[3][c] - m[0][c];
            planes[2][c] = m[3][c] + m[1][c];
            planes[3][c] = m[3][c] - m[1][c];
            planes[4][c] = m[2][c];
            planes[5][c] = m[3][c] - m[2][c];
        }
        for (auto& plane : planes)
        {
            const float length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
            if (length > 1e-8f)
                for (float& v : plane)
                    v /= length;
        }
    }

    bool sees(const math::Vec3& center, float radius) const
    {
        for (const auto& plane : planes)
            if (plane[0] * center.x + plane[1] * center.y + plane[2] * center.z + plane[3] < -radius)
                return false;
        return true;
    }
};

}

struct Renderer3D::Impl
{
    bool initialized = false;

    render::PipelineHandle opaquePipeline      = render::kInvalidHandle;
    render::PipelineHandle translucentPipeline = render::kInvalidHandle;
    render::PipelineHandle opaqueTwoSided      = render::kInvalidHandle;
    render::PipelineHandle translucentTwoSided = render::kInvalidHandle;
    render::PipelineHandle shadowDepthPipeline = render::kInvalidHandle;
    render::PipelineHandle overlayPipeline     = render::kInvalidHandle;
    render::PipelineHandle depthLinePipeline   = render::kInvalidHandle;
    render::BufferHandle vertexBuffer          = render::kInvalidHandle;
    render::TextureHandle whiteTexture         = render::kInvalidHandle;

    math::Mat4 viewProj;
    math::Mat4 lightViewProj;
    bool shadowsActive      = false;
    float shadowNormalOffset = 0.f;
    std::vector<LocalLight> locals;
    Camera camera;
    float aspect = 1.f;
    math::Vec3 cameraPos;
    Light light;
    math::Vec3 toLight = {0.f, 1.f, 0.f};
    std::vector<Command> commands;
    Frustum frustum;
    std::size_t culled    = 0;
    std::size_t instanced = 0;
    render::PipelineHandle instancedPipeline          = render::kInvalidHandle;
    render::PipelineHandle instancedTwoSidedPipeline  = render::kInvalidHandle;
    render::PipelineHandle shadowInstancedPipeline    = render::kInvalidHandle;
    render::PipelineHandle skyPipeline                = render::kInvalidHandle;
    render::PipelineHandle brightPipeline             = render::kInvalidHandle;
    render::PipelineHandle blurPipeline               = render::kInvalidHandle;
    render::PipelineHandle compositePipeline          = render::kInvalidHandle;
    bool postReady                                    = false;
    render::RenderTargetHandle sceneTarget            = render::kInvalidHandle;
    render::RenderTargetHandle bloomA                 = render::kInvalidHandle;
    render::RenderTargetHandle bloomB                 = render::kInvalidHandle;
    std::uint32_t targetWidth                         = 0;
    std::uint32_t targetHeight                        = 0;

    bool ensurePostTargets(std::uint32_t w, std::uint32_t h)
    {
        auto& device = render::Device::instance();
        if (sceneTarget != render::kInvalidHandle && targetWidth == w && targetHeight == h)
            return true;
        for (render::RenderTargetHandle* handle : {&sceneTarget, &bloomA, &bloomB})
        {
            if (*handle != render::kInvalidHandle)
                device.destroyRenderTarget(*handle);
            *handle = render::kInvalidHandle;
        }
        render::RenderTargetDesc scene{w, h, true};
        render::RenderTargetDesc half{std::max(1u, w / 2), std::max(1u, h / 2), false};
        sceneTarget = device.createRenderTarget(scene);
        bloomA      = device.createRenderTarget(half);
        bloomB      = device.createRenderTarget(half);
        targetWidth  = w;
        targetHeight = h;
        if (sceneTarget == render::kInvalidHandle || bloomA == render::kInvalidHandle || bloomB == render::kInvalidHandle)
        {
            targetWidth = 0;
            return false;
        }
        return true;
    }

    void pushPost(render::Device& device, float threshold, float intensity, float bloomOn, float texelX, float texelY)
    {
        const Environment& e = environment;
        const float data[12] = {threshold, intensity, e.exposure, e.contrast,
                                e.saturation, bloomOn, texelX, texelY,
                                e.tint.r, e.tint.g, e.tint.b, 1.f};
        device.setPushConstants(data, sizeof(data));
    }

    void runPost(render::Device& device)
    {
        const Environment& e = environment;
        device.endRenderTarget(false);
        const render::TextureHandle scene = device.renderTargetTexture(sceneTarget);
        if (e.bloom)
        {
            const float halfW = static_cast<float>(std::max(1u, targetWidth / 2));
            const float halfH = static_cast<float>(std::max(1u, targetHeight / 2));
            if (device.beginRenderTarget(bloomA, render::ClearColor{0.f, 0.f, 0.f, 1.f}))
            {
                device.bindPipeline(brightPipeline);
                device.bindTexture(scene);
                pushPost(device, e.bloomThreshold, e.bloomIntensity, 1.f, 0.f, 0.f);
                device.draw(3);
                device.endRenderTarget(false);
            }
            for (int pass = 0; pass < 2; ++pass)
            {
                if (device.beginRenderTarget(bloomB, render::ClearColor{0.f, 0.f, 0.f, 1.f}))
                {
                    device.bindPipeline(blurPipeline);
                    device.bindTexture(device.renderTargetTexture(bloomA));
                    pushPost(device, e.bloomThreshold, e.bloomIntensity, 1.f, 1.f / halfW, 0.f);
                    device.draw(3);
                    device.endRenderTarget(false);
                }
                if (device.beginRenderTarget(bloomA, render::ClearColor{0.f, 0.f, 0.f, 1.f}))
                {
                    device.bindPipeline(blurPipeline);
                    device.bindTexture(device.renderTargetTexture(bloomB));
                    pushPost(device, e.bloomThreshold, e.bloomIntensity, 1.f, 0.f, 1.f / halfH);
                    device.draw(3);
                    device.endRenderTarget(false);
                }
            }
        }
        device.resumeMainPass();
        device.setViewport(viewportX, viewportY, viewportWidth, viewportHeight);
        device.bindPipeline(compositePipeline);
        device.bindTexture(scene);
        const render::TextureHandle bloomTexture[4] = {e.bloom ? device.renderTargetTexture(bloomA) : render::kInvalidHandle,
                                                       render::kInvalidHandle, render::kInvalidHandle,
                                                       render::kInvalidHandle};
        device.bindMaterialTextures(bloomTexture);
        pushPost(device, e.bloomThreshold, e.bloomIntensity, e.bloom ? 1.f : 0.f, 0.f, 0.f);
        device.draw(3);
        currentPipeline = render::kInvalidHandle;
    }
    Environment environment;
    std::shared_ptr<renderer::Texture> panoramaTexture;
    std::string panoramaLoaded;
    render::BufferHandle instanceBuffer               = render::kInvalidHandle;

    struct Line
    {
        math::Vec3 a, b;
        renderer::Color colorA, colorB;
        float widthPixels;
        LineMode mode;
    };
    std::vector<Line> lines;
    std::uint32_t viewportX      = 0;
    std::uint32_t viewportY      = 0;
    std::uint32_t viewportWidth  = 1;
    std::uint32_t viewportHeight = 1;
    float nearW                  = 0.1f;

    std::vector<Vertex> pending;
    render::PipelineHandle currentPipeline = render::kInvalidHandle;
    MaterialKey currentMaterial;

    render::PipelineHandle makePipeline(bool depthWrite, bool cullBack, bool depthTest = true, bool shadowPass = false)
    {
        render::VertexAttribute attrs[] = {
            {"POSITION", 0, render::VertexFormat::Float4, offsetof(Vertex, x)},
            {"COLOR", 0, render::VertexFormat::Float4, offsetof(Vertex, r)},
            {"TEXCOORD", 0, render::VertexFormat::Float2, offsetof(Vertex, u)},
            {"NORMAL", 0, render::VertexFormat::Float3, offsetof(Vertex, nx)},
            {"TEXCOORD", 1, render::VertexFormat::Float3, offsetof(Vertex, wx)},
            {"TEXCOORD", 2, render::VertexFormat::Float2, offsetof(Vertex, lit)},
            {"TEXCOORD", 3, render::VertexFormat::Float4, offsetof(Vertex, metallic)},
            {"TEXCOORD", 4, render::VertexFormat::Float4, offsetof(Vertex, er)},
        };
        render::PipelineDesc desc{};
        desc.vertexShaderSpirv = zio_mesh3d_vert_spirv;
        desc.pixelShaderSpirv  = shadowPass ? std::span<const std::uint32_t>(zio_shadow_frag_spirv)
                                            : std::span<const std::uint32_t>(zio_mesh3d_frag_spirv);
        desc.attributes        = attrs;
        desc.attributeCount    = 8;
        desc.vertexStrideBytes = sizeof(Vertex);
        desc.depthTest         = depthTest;
        desc.depthWrite        = depthWrite;
        desc.cullBack          = cullBack;
        desc.shadowPass        = shadowPass;
        return render::Device::instance().createPipeline(desc);
    }

    render::PipelineHandle makeInstancedPipeline(bool cullBack, bool shadow = false)
    {
        render::VertexAttribute attrs[] = {
            {"POSITION", 0, render::VertexFormat::Float3, offsetof(Vertex3D, position)},
            {"NORMAL", 0, render::VertexFormat::Float3, offsetof(Vertex3D, normal)},
            {"TEXCOORD", 0, render::VertexFormat::Float2, offsetof(Vertex3D, u)},
            {"COLOR", 0, render::VertexFormat::Float4, offsetof(Vertex3D, r)},
        };
        render::VertexAttribute inst[] = {
            {"TEXCOORD", 1, render::VertexFormat::Float4, offsetof(InstanceData, row[0])},
            {"TEXCOORD", 2, render::VertexFormat::Float4, offsetof(InstanceData, row[1])},
            {"TEXCOORD", 3, render::VertexFormat::Float4, offsetof(InstanceData, row[2])},
            {"TEXCOORD", 4, render::VertexFormat::Float4, offsetof(InstanceData, normalRow[0])},
            {"TEXCOORD", 5, render::VertexFormat::Float4, offsetof(InstanceData, normalRow[1])},
            {"TEXCOORD", 6, render::VertexFormat::Float4, offsetof(InstanceData, normalRow[2])},
            {"TEXCOORD", 7, render::VertexFormat::Float4, offsetof(InstanceData, color)},
            {"TEXCOORD", 8, render::VertexFormat::Float4, offsetof(InstanceData, material)},
            {"TEXCOORD", 9, render::VertexFormat::Float4, offsetof(InstanceData, emissive)},
            {"TEXCOORD", 10, render::VertexFormat::Float4, offsetof(InstanceData, flagsUv)},
        };
        render::PipelineDesc desc{};
        desc.vertexShaderSpirv       = shadow ? std::span<const std::uint32_t>(zio_shadow_inst_vert_spirv)
                                              : std::span<const std::uint32_t>(zio_mesh3d_inst_vert_spirv);
        desc.pixelShaderSpirv        = shadow ? std::span<const std::uint32_t>(zio_shadow_frag_spirv)
                                              : std::span<const std::uint32_t>(zio_mesh3d_frag_spirv);
        desc.shadowPass              = shadow;
        desc.attributes              = attrs;
        desc.attributeCount          = 4;
        desc.vertexStrideBytes       = sizeof(Vertex3D);
        desc.instanceAttributes      = inst;
        desc.instanceAttributeCount  = 10;
        desc.instanceStrideBytes     = sizeof(InstanceData);
        desc.depthTest               = true;
        desc.depthWrite              = true;
        desc.cullBack                = cullBack;
        return render::Device::instance().createPipeline(desc);
    }

    render::BufferHandle meshBuffer(const Mesh& mesh)
    {
        static std::atomic<std::uint64_t> counter{0};
        if (mesh.gpu.value == 0)
            mesh.gpu.value = ++counter;
        auto& buffers = meshBuffers();
        auto& entry   = buffers[mesh.gpu.value];
        auto& device  = render::Device::instance();
        if (entry.handle != render::kInvalidHandle && entry.vertexCount == mesh.vertices.size())
            return entry.handle;
        if (entry.handle != render::kInvalidHandle)
            device.destroyBuffer(entry.handle);
        render::BufferDesc desc{};
        desc.type      = render::BufferType::Vertex;
        desc.usage     = render::BufferUsage::Immutable;
        desc.data      = mesh.vertices.data();
        desc.sizeBytes = mesh.vertices.size() * sizeof(Vertex3D);
        entry.handle      = device.createBuffer(desc);
        entry.vertexCount = mesh.vertices.size();
        return entry.handle;
    }

    static bool instanceable(const Command& cmd)
    {
        if (cmd.mesh->dynamic || cmd.count == 0 || cmd.count % 3 != 0 || cmd.style.alphaMode == AlphaMode::Blend)
            return false;
        if (cmd.first + cmd.count > cmd.mesh->vertices.size())
            return false;
        const float(&w)[4][4] = cmd.world.m;
        const float det = w[0][0] * (w[1][1] * w[2][2] - w[1][2] * w[2][1]) +
                          w[0][1] * (w[1][2] * w[2][0] - w[1][0] * w[2][2]) +
                          w[0][2] * (w[1][0] * w[2][1] - w[1][1] * w[2][0]);
        return det > 0.f;
    }

    static std::tuple<const Mesh*, std::size_t, std::size_t, bool, render::TextureHandle, render::TextureHandle,
                      render::TextureHandle, render::TextureHandle, render::TextureHandle>
    groupKey(const Command& cmd)
    {
        return {cmd.mesh,
                cmd.first,
                cmd.count,
                cmd.style.doubleSided,
                cmd.style.texture,
                cmd.style.normalTexture,
                cmd.style.metallicRoughnessTexture,
                cmd.style.emissiveTexture,
                cmd.style.occlusionTexture};
    }

    void fillInstance(const Command& cmd, InstanceData& out) const
    {
        const float(&w)[4][4] = cmd.world.m;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                out.row[r][c] = w[r][c];
        out.normalRow[0][0] = w[1][1] * w[2][2] - w[1][2] * w[2][1];
        out.normalRow[0][1] = w[1][2] * w[2][0] - w[1][0] * w[2][2];
        out.normalRow[0][2] = w[1][0] * w[2][1] - w[1][1] * w[2][0];
        out.normalRow[1][0] = w[0][2] * w[2][1] - w[0][1] * w[2][2];
        out.normalRow[1][1] = w[0][0] * w[2][2] - w[0][2] * w[2][0];
        out.normalRow[1][2] = w[0][1] * w[2][0] - w[0][0] * w[2][1];
        out.normalRow[2][0] = w[0][1] * w[1][2] - w[0][2] * w[1][1];
        out.normalRow[2][1] = w[0][2] * w[1][0] - w[0][0] * w[1][2];
        out.normalRow[2][2] = w[0][0] * w[1][1] - w[0][1] * w[1][0];
        for (int r = 0; r < 3; ++r)
            out.normalRow[r][3] = 0.f;
        const MeshStyle& st = cmd.style;
        out.color[0]        = st.color.r;
        out.color[1]        = st.color.g;
        out.color[2]        = st.color.b;
        out.color[3]        = st.color.a;
        out.material[0]     = st.metallic;
        out.material[1]     = st.roughness;
        out.material[2]     = st.normalScale;
        out.material[3]     = st.occlusionStrength;
        out.emissive[0]     = st.emissive[0];
        out.emissive[1]     = st.emissive[1];
        out.emissive[2]     = st.emissive[2];
        out.emissive[3]     = st.alphaCutoff;
        out.flagsUv[0]      = materialFlags(st);
        out.flagsUv[1]      = st.uvScale.x;
        out.flagsUv[2]      = st.uvScale.y;
        out.flagsUv[3]      = 0.f;
    }

    bool drawInstancedShadow(const std::vector<const Command*>& cmds, const math::Mat4& lightMatrix)
    {
        const Command& head           = *cmds.front();
        const render::BufferHandle vb = meshBuffer(*head.mesh);
        if (vb == render::kInvalidHandle)
            return false;

        flush();
        auto& device = render::Device::instance();
        device.bindPipeline(shadowInstancedPipeline);
        device.bindVertexBuffer(vb, sizeof(Vertex3D));
        float matrix[16];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                matrix[c * 4 + r] = lightMatrix.m[r][c];
        device.setPushConstants(matrix, sizeof(matrix));

        std::vector<InstanceData> data;
        for (std::size_t first = 0; first < cmds.size(); first += kMaxInstances)
        {
            const std::size_t last = std::min(cmds.size(), first + kMaxInstances);
            data.resize(last - first);
            for (std::size_t i = first; i < last; ++i)
                fillInstance(*cmds[i], data[i - first]);
            if (!device.updateBuffer(instanceBuffer, data.data(),
                                     static_cast<std::uint32_t>(data.size() * sizeof(InstanceData))))
                return true;
            device.bindInstanceBuffer(instanceBuffer);
            device.draw(static_cast<std::uint32_t>(head.count), static_cast<std::uint32_t>(head.first),
                        static_cast<std::uint32_t>(data.size()));
        }
        currentPipeline = render::kInvalidHandle;
        return true;
    }

    bool drawInstanced(const std::vector<Command*>& cmds, std::size_t begin, std::size_t end)
    {
        const Command& head = *cmds[begin];
        const render::BufferHandle vb = meshBuffer(*head.mesh);
        if (vb == render::kInvalidHandle)
            return false;

        flush();
        auto& device = render::Device::instance();
        device.bindPipeline(head.style.doubleSided ? instancedTwoSidedPipeline : instancedPipeline);
        device.bindVertexBuffer(vb, sizeof(Vertex3D));
        device.bindTexture(head.style.texture == render::kInvalidHandle ? whiteTexture : head.style.texture, 0);
        const render::TextureHandle material[4] = {head.style.normalTexture, head.style.metallicRoughnessTexture,
                                                   head.style.emissiveTexture, head.style.occlusionTexture};
        device.bindMaterialTextures(material);
        device.bindShadowMap();

        std::vector<InstanceData> data;
        for (std::size_t first = begin; first < end; first += kMaxInstances)
        {
            const std::size_t last = std::min(end, first + kMaxInstances);
            data.resize(last - first);
            for (std::size_t i = first; i < last; ++i)
                fillInstance(*cmds[i], data[i - first]);
            if (!device.updateBuffer(instanceBuffer, data.data(),
                                     static_cast<std::uint32_t>(data.size() * sizeof(InstanceData))))
                return true;
            device.bindInstanceBuffer(instanceBuffer);
            device.draw(static_cast<std::uint32_t>(head.count), static_cast<std::uint32_t>(head.first),
                        static_cast<std::uint32_t>(data.size()));
        }
        instanced += end - begin;
        currentPipeline = render::kInvalidHandle;
        return true;
    }

    bool init()
    {
        auto& device = render::Device::instance();
        if (!device.isInitialized())
            return false;

        opaquePipeline      = makePipeline(true, true);
        translucentPipeline = makePipeline(false, true);
        opaqueTwoSided      = makePipeline(true, false);
        translucentTwoSided = makePipeline(false, false);
        shadowDepthPipeline = makePipeline(true, false, true, true);
        overlayPipeline     = makePipeline(false, false, false);
        depthLinePipeline   = makePipeline(false, false, true);
        if (overlayPipeline == render::kInvalidHandle || depthLinePipeline == render::kInvalidHandle)
            return false;
        if (opaqueTwoSided == render::kInvalidHandle || translucentTwoSided == render::kInvalidHandle)
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

        instancedPipeline         = makeInstancedPipeline(true);
        instancedTwoSidedPipeline = makeInstancedPipeline(false);
        shadowInstancedPipeline   = makeInstancedPipeline(false, true);
        {
            render::PipelineDesc sky{};
            sky.vertexShaderSpirv = zio_sky_vert_spirv;
            sky.pixelShaderSpirv  = zio_sky_frag_spirv;
            sky.depthTest         = false;
            sky.depthWrite        = false;
            sky.cullBack          = false;
            skyPipeline           = device.createPipeline(sky);

            render::PipelineDesc post{};
            post.vertexShaderSpirv = zio_post_vert_spirv;
            post.pixelShaderSpirv  = zio_bright_frag_spirv;
            post.postPass          = true;
            brightPipeline         = device.createPipeline(post);
            post.pixelShaderSpirv  = zio_blur_frag_spirv;
            blurPipeline           = device.createPipeline(post);

            render::PipelineDesc composite{};
            composite.vertexShaderSpirv = zio_post_vert_spirv;
            composite.pixelShaderSpirv  = zio_composite_frag_spirv;
            composite.depthTest         = false;
            composite.depthWrite        = false;
            compositePipeline           = device.createPipeline(composite);
            postReady = brightPipeline != render::kInvalidHandle && blurPipeline != render::kInvalidHandle &&
                        compositePipeline != render::kInvalidHandle;
        }
        render::BufferDesc ib{};
        ib.type      = render::BufferType::Vertex;
        ib.usage     = render::BufferUsage::Dynamic;
        ib.sizeBytes = kMaxInstances * sizeof(InstanceData);
        instanceBuffer = device.createBuffer(ib);
        if (instancedPipeline == render::kInvalidHandle || instancedTwoSidedPipeline == render::kInvalidHandle ||
            shadowInstancedPipeline == render::kInvalidHandle ||
            instanceBuffer == render::kInvalidHandle)
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
        device.bindTexture(currentMaterial.texture[0]);
        device.bindMaterialTextures(currentMaterial.texture + 1);
        if (currentPipeline != shadowDepthPipeline)
            device.bindShadowMap();
        device.draw(static_cast<std::uint32_t>(pending.size()));
        pending.clear();
    }

    void prepareBatch(render::PipelineHandle pipeline, MaterialKey key)
    {
        if (key.texture[0] == render::kInvalidHandle)
            key.texture[0] = whiteTexture;
        if (!pending.empty() && (pipeline != currentPipeline || key != currentMaterial))
            flush();
        else if (pending.size() + 3 > kMaxVertices)
            flush();
        currentPipeline = pipeline;
        currentMaterial = key;
    }

    void prepareBatch(render::PipelineHandle pipeline, render::TextureHandle texture = render::kInvalidHandle)
    {
        MaterialKey key;
        key.texture[0] = texture;
        prepareBatch(pipeline, key);
    }

    void emit(const Command& cmd, render::PipelineHandle pipeline)
    {
        MaterialKey key;
        key.texture[0] = cmd.style.texture;
        key.texture[1] = cmd.style.normalTexture;
        key.texture[2] = cmd.style.metallicRoughnessTexture;
        key.texture[3] = cmd.style.emissiveTexture;
        key.texture[4] = cmd.style.occlusionTexture;
        prepareBatch(pipeline, key);

        const float flags = materialFlags(cmd.style);

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
        const std::size_t end = std::min(verts.size(), cmd.first + cmd.count);
        for (std::size_t i = cmd.first; i + 2 < end; i += 3)
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
                const math::Vec3 wp = cmd.world.transformPoint(v.position);
                pending.push_back(Vertex{
                    m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
                    m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
                    m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3],
                    m[3][0] * x + m[3][1] * y + m[3][2] * z + m[3][3],
                    col.r * v.r, col.g * v.g, col.b * v.b, col.a * v.a,
                    v.u * cmd.style.uvScale.x, v.v * cmd.style.uvScale.y,
                    n.x, n.y, n.z,
                    wp.x, wp.y, wp.z,
                    1.f, flags,
                    cmd.style.metallic, cmd.style.roughness, cmd.style.normalScale, cmd.style.occlusionStrength,
                    cmd.style.emissive[0], cmd.style.emissive[1], cmd.style.emissive[2], cmd.style.alphaCutoff});
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
                                         0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f});
            }
        }
    }

    Tile buildLightMatrix()
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
            const float halfH      = camera.orthographic ? camera.orthoSize : d * tanHalf;
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

        Tile tile;
        tile.viewProj     = proj * view;
        tile.x            = 0;
        tile.y            = 0;
        tile.size         = render::Device::instance().shadowMapSize();
        tile.normalOffset = texel * kShadowNormalOffsetTexels;
        tile.bias         = 0.0004f;
        return tile;
    }

    void ensureBounds(Command& cmd)
    {
        if (cmd.boundsReady)
            return;
        cmd.boundsReady = true;
        const auto& verts = cmd.mesh->vertices;
        const std::size_t end = std::min(verts.size(), cmd.first + cmd.count);
        if (cmd.first >= end)
            return;
        math::Vec3 lo = verts[cmd.first].position, hi = lo;
        for (std::size_t i = cmd.first; i < end; ++i)
        {
            const math::Vec3& p = verts[i].position;
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        const math::Vec3 mid = (lo + hi) * 0.5f;
        const float localR   = (hi - mid).length();
        const float(&w)[4][4] = cmd.world.m;
        float sx = 0.f, sy = 0.f, sz = 0.f;
        for (int c = 0; c < 3; ++c)
        {
            sx += w[0][c] * w[0][c];
            sy += w[1][c] * w[1][c];
            sz += w[2][c] * w[2][c];
        }
        const float scale = std::sqrt(std::max({sx, sy, sz}));
        cmd.center        = cmd.world.transformPoint(mid);
        cmd.radius        = localR * scale;
        cmd.boundsValid   = true;
    }

    bool visible(Command& cmd)
    {
        ensureBounds(cmd);
        const bool seen = !cmd.boundsValid || frustum.sees(cmd.center, cmd.radius);
        if (!seen)
            ++culled;
        return seen;
    }

    void emitDepth(const Command& cmd, const math::Mat4& lightMatrix)
    {
        prepareBatch(shadowDepthPipeline, render::kInvalidHandle);

        const math::Mat4 mvp  = lightMatrix * cmd.world;
        const float(&m)[4][4] = mvp.m;
        const auto& verts     = cmd.mesh->vertices;
        const std::size_t end = std::min(verts.size(), cmd.first + cmd.count);
        for (std::size_t i = cmd.first; i + 2 < end; i += 3)
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
                       std::uint32_t viewportHeight, std::uint32_t viewportX, std::uint32_t viewportY)
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
    impl_->locals.clear();
    impl_->light.ambient       = std::clamp(light.ambient, 0.f, 1.f);
    impl_->light.shadowOpacity = std::clamp(light.shadowOpacity, 0.f, 1.f);
    impl_->toLight   = (-light.direction).normalized();
    impl_->viewportX      = viewportX;
    impl_->viewportY      = viewportY;
    impl_->viewportWidth  = std::max<std::uint32_t>(viewportWidth, 1);
    impl_->viewportHeight = std::max<std::uint32_t>(viewportHeight, 1);
    impl_->nearW          = std::max(camera.nearZ, 1e-4f);
    impl_->commands.clear();
    impl_->lines.clear();
    impl_->pending.clear();
}

void Renderer3D::setEnvironment(const Environment& environment)
{
    impl_->environment = environment;
}

void Renderer3D::addLight(const LocalLight& light)
{
    if (!impl_->initialized || !(light.range > 0.f) || !(light.intensity > 0.f))
        return;
    impl_->locals.push_back(light);
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
    drawMeshPart(mesh, 0, mesh.vertices.size(), world, style);
}

void Renderer3D::drawMeshPart(const Mesh& mesh, std::size_t firstVertex, std::size_t vertexCount, const math::Mat4& world,
                              const MeshStyle& style)
{
    if (!impl_->initialized || mesh.vertices.empty() || vertexCount == 0 || firstVertex >= mesh.vertices.size())
        return;
    const math::Vec3 origin{world.m[0][3], world.m[1][3], world.m[2][3]};
    const math::Vec3 d = origin - impl_->cameraPos;
    impl_->commands.push_back(Command{&mesh, world, style, d.dot(d), firstVertex, vertexCount});
}

std::size_t Renderer3D::instancedLastFrame() const
{
    return impl_->instanced;
}

void Renderer3D::releaseMeshBuffers(std::uint64_t id)
{
    auto& buffers = meshBuffers();
    const auto it = buffers.find(id);
    if (it == buffers.end())
        return;
    if (it->second.handle != render::kInvalidHandle && render::Device::instance().isInitialized())
        render::Device::instance().destroyBuffer(it->second.handle);
    buffers.erase(it);
}

GpuId::~GpuId()
{
    if (value != 0)
        Renderer3D::releaseMeshBuffers(value);
}

std::size_t Renderer3D::culledLastFrame() const
{
    return impl_->culled;
}

void Renderer3D::end()
{
    auto& s = *impl_;
    if (!s.initialized)
        return;

    auto& device = render::Device::instance();

    std::sort(s.locals.begin(), s.locals.end(), [&](const LocalLight& a, const LocalLight& b) {
        const math::Vec3 da = a.position - s.cameraPos;
        const math::Vec3 db = b.position - s.cameraPos;
        return da.dot(da) < db.dot(db);
    });
    if (s.locals.size() > kMaxLocalLights)
        s.locals.resize(kMaxLocalLights);

    Tile tiles[kMaxTiles];
    bool tileUsed[kMaxTiles] = {};
    int firstTile[kMaxLocalLights];
    for (int& f : firstTile)
        f = -1;

    s.shadowsActive = s.light.shadows && s.light.shadowOpacity > 0.f && s.light.direction.length() > 1e-4f;
    if (s.shadowsActive)
    {
        tiles[0]    = s.buildLightMatrix();
        tileUsed[0] = true;
    }

    int nextSlot = 0;
    for (std::size_t i = 0; i < s.locals.size(); ++i)
    {
        const LocalLight& l = s.locals[i];
        if (!l.shadows || !(l.shadowOpacity > 0.f))
            continue;
        const bool spot = l.type == LightType::Spot;
        const int need  = spot ? 1 : 6;
        if (nextSlot + need > kLocalTileSlots)
            continue;
        firstTile[i] = 1 + nextSlot;
        const float nearZ = std::max(0.05f, l.range * 0.01f);
        for (int f = 0; f < need; ++f)
        {
            const int index = firstTile[i] + f;
            const int slot  = nextSlot + f;
            Tile& t         = tiles[index];
            tileUsed[index] = true;
            t.size          = kLocalTileSize;
            if (slot < 4)
            {
                t.x = 2048 + static_cast<std::uint32_t>(slot % 2) * kLocalTileSize;
                t.y = static_cast<std::uint32_t>(slot / 2) * kLocalTileSize;
            }
            else
            {
                t.x = static_cast<std::uint32_t>((slot - 4) % 4) * kLocalTileSize;
                t.y = 2048 + static_cast<std::uint32_t>((slot - 4) / 4) * kLocalTileSize;
            }

            math::Vec3 dir;
            math::Vec3 up = {0.f, 1.f, 0.f};
            float fov     = 90.f;
            if (spot)
            {
                dir = l.direction.length() > 1e-5f ? l.direction.normalized() : math::Vec3{0.f, -1.f, 0.f};
                if (std::fabs(dir.y) > 0.95f)
                    up = {0.f, 0.f, 1.f};
                fov = std::min(2.f * l.outerAngleDegrees + 4.f, 170.f);
            }
            else
            {
                static const math::Vec3 kDirs[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                                                    {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};
                dir = kDirs[f];
                if (f == 2 || f == 3)
                    up = {0.f, 0.f, 1.f};
            }
            const float tanHalf = std::tan(fov * 0.5f * (3.14159265358979323846f / 180.f));
            t.viewProj     = math::Mat4::perspective(fov, 1.f, nearZ, l.range) *
                         math::Mat4::lookAt(l.position, l.position + dir, up);
            t.normalOffset = 1.5f * 2.f * tanHalf / static_cast<float>(kLocalTileSize);
            t.bias         = std::max(0.02f, l.range * 0.002f);
            t.nearZ        = nearZ;
            t.farZ         = l.range;
        }
        nextSlot += need;
    }

    bool anyTile = false;
    for (int i = 0; i < kMaxTiles; ++i)
        anyTile = anyTile || tileUsed[i];

    if (anyTile)
    {
        bool anyLocal = false;
        for (int i = 1; i < kMaxTiles; ++i)
            anyLocal = anyLocal || tileUsed[i];
        if (anyLocal)
            for (Command& c : s.commands)
                if (c.style.castShadow && !isTranslucent(c.style))
                    s.ensureBounds(c);

        std::vector<const Command*> singles;
        std::vector<std::vector<const Command*>> shadowGroups;
        {
            std::vector<Command*> casters;
            for (Command& c : s.commands)
            {
                if (!c.style.castShadow || isTranslucent(c.style))
                    continue;
                if (Impl::instanceable(c))
                    casters.push_back(&c);
                else
                    singles.push_back(&c);
            }
            std::stable_sort(casters.begin(), casters.end(), [](const Command* a, const Command* b) {
                return std::make_tuple(a->mesh, a->first, a->count) < std::make_tuple(b->mesh, b->first, b->count);
            });
            for (std::size_t begin = 0; begin < casters.size();)
            {
                std::size_t end = begin + 1;
                while (end < casters.size() && casters[end]->mesh == casters[begin]->mesh &&
                       casters[end]->first == casters[begin]->first && casters[end]->count == casters[begin]->count)
                    ++end;
                if (end - begin >= 2)
                {
                    shadowGroups.emplace_back(casters.begin() + static_cast<std::ptrdiff_t>(begin),
                                              casters.begin() + static_cast<std::ptrdiff_t>(end));
                }
                else
                {
                    singles.push_back(casters[begin]);
                }
                begin = end;
            }
            for (Command* c : casters)
                s.ensureBounds(*c);
        }

        if (device.beginShadowPass())
        {
            for (int i = 0; i < kMaxTiles; ++i)
            {
                if (!tileUsed[i])
                    continue;
                device.setShadowViewport(tiles[i].x, tiles[i].y, tiles[i].size, tiles[i].size);

                math::Vec3 lightCenter = math::Vec3::zero();
                float lightRange       = 0.f;
                if (i > 0)
                {
                    for (std::size_t k = 0; k < s.locals.size(); ++k)
                    {
                        const int first = firstTile[k];
                        const int count = s.locals[k].type == LightType::Spot ? 1 : 6;
                        if (first >= 0 && i >= first && i < first + count)
                        {
                            lightCenter = s.locals[k].position;
                            lightRange  = s.locals[k].range;
                        }
                    }
                }

                const auto reaches = [&](const Command& c) {
                    if (i == 0)
                        return true;
                    const math::Vec3 d = c.center - lightCenter;
                    const float reach  = lightRange + c.radius;
                    return d.dot(d) <= reach * reach;
                };
                for (const Command* c : singles)
                    if (reaches(*c))
                        s.emitDepth(*c, tiles[i].viewProj);
                s.flush();
                for (const auto& group : shadowGroups)
                {
                    std::vector<const Command*> visibleMembers;
                    for (const Command* c : group)
                        if (reaches(*c))
                            visibleMembers.push_back(c);
                    if (visibleMembers.empty())
                        continue;
                    if (!s.drawInstancedShadow(visibleMembers, tiles[i].viewProj))
                        for (const Command* c : visibleMembers)
                            s.emitDepth(*c, tiles[i].viewProj);
                }
                s.flush();
            }
            device.endShadowPass();
        }
        else
        {
            s.shadowsActive = false;
            for (int& f : firstTile)
                f = -1;
        }
    }

    SceneBlock block{};
    block.head[0] = static_cast<float>(1 + s.locals.size());
    block.head[1] = s.light.ambient;
    block.head[2] = s.shadowsActive ? s.light.shadowOpacity : 0.f;
    block.head[3] = static_cast<float>(device.shadowAtlasSize());
    block.camPos[0] = s.cameraPos.x;
    block.camPos[1] = s.cameraPos.y;
    block.camPos[2] = s.cameraPos.z;

    const float atlas = static_cast<float>(device.shadowAtlasSize());
    for (int i = 0; i < kMaxTiles; ++i)
    {
        const Tile& t = tiles[i];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                block.tileMat[i][c * 4 + r] = t.viewProj.m[r][c];
        block.tileRect[i][0]  = static_cast<float>(t.x) / atlas;
        block.tileRect[i][1]  = static_cast<float>(t.y) / atlas;
        block.tileRect[i][2]  = static_cast<float>(t.size) / atlas;
        block.tileRect[i][3]  = static_cast<float>(t.size) / atlas;
        block.tileParam[i][0] = t.normalOffset;
        block.tileParam[i][1] = t.bias;
        block.tileParam[i][2] = t.nearZ;
        block.tileParam[i][3] = t.farZ;
    }

    const bool dirOn = s.light.direction.length() > 1e-4f;
    block.lightPos[0][0] = s.toLight.x;
    block.lightPos[0][1] = s.toLight.y;
    block.lightPos[0][2] = s.toLight.z;
    block.lightPos[0][3] = dirOn ? 1.f : 0.f;
    block.lightMisc[0][0] = s.shadowsActive ? 0.f : -1.f;
    block.lightMisc[0][2] = s.light.shadowOpacity;

    for (std::size_t i = 0; i < s.locals.size(); ++i)
    {
        const LocalLight& l = s.locals[i];
        const std::size_t k = i + 1;
        const bool spot     = l.type == LightType::Spot;
        block.lightPos[k][0] = l.position.x;
        block.lightPos[k][1] = l.position.y;
        block.lightPos[k][2] = l.position.z;
        block.lightPos[k][3] = spot ? 3.f : 2.f;
        block.lightColor[k][0] = l.color.r * l.intensity;
        block.lightColor[k][1] = l.color.g * l.intensity;
        block.lightColor[k][2] = l.color.b * l.intensity;
        block.lightColor[k][3] = l.range;
        const math::Vec3 dir = l.direction.length() > 1e-5f ? l.direction.normalized() : math::Vec3{0.f, -1.f, 0.f};
        constexpr float kRad = 3.14159265358979323846f / 180.f;
        const float outer    = std::clamp(l.outerAngleDegrees, 1.f, 89.f) * kRad;
        const float inner    = std::clamp(l.innerAngleDegrees * kRad, 0.f, outer - 0.01f);
        block.lightDir[k][0] = dir.x;
        block.lightDir[k][1] = dir.y;
        block.lightDir[k][2] = dir.z;
        block.lightDir[k][3] = std::cos(outer);
        block.lightMisc[k][0] = static_cast<float>(firstTile[i]);
        block.lightMisc[k][1] = std::cos(inner);
        block.lightMisc[k][2] = std::clamp(l.shadowOpacity, 0.f, 1.f);
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            block.viewProj[c * 4 + r] = s.viewProj.m[r][c];
    {
        const math::Vec3 forward = (s.camera.target - s.camera.position).normalized();
        const math::Vec3 right   = forward.cross(s.camera.up).normalized();
        const math::Vec3 up      = right.cross(forward);
        const float tanHalf      = std::tan(s.camera.fovYDegrees * 0.5f * 3.14159265f / 180.f);
        const Environment& env   = s.environment;
        const math::Vec3 rows[3] = {right, up, forward};
        float* targets[3]        = {block.camRight, block.camUp, block.camForward};
        for (int i = 0; i < 3; ++i)
        {
            targets[i][0] = rows[i].x;
            targets[i][1] = rows[i].y;
            targets[i][2] = rows[i].z;
        }
        block.camRight[3] = s.camera.orthographic ? 0.f : tanHalf * s.aspect;
        block.camUp[3]    = s.camera.orthographic ? 0.f : tanHalf;
        const renderer::Color colors[3] = {env.zenith, env.horizon, env.ground};
        float* colorTargets[3]          = {block.skyZenith, block.skyHorizon, block.skyGround};
        for (int i = 0; i < 3; ++i)
        {
            colorTargets[i][0] = colors[i].r;
            colorTargets[i][1] = colors[i].g;
            colorTargets[i][2] = colors[i].b;
            colorTargets[i][3] = 1.f;
        }
        block.fogColor[0] = env.fogColor.r;
        block.fogColor[1] = env.fogColor.g;
        block.fogColor[2] = env.fogColor.b;
        block.fogColor[3] = 1.f;
        block.fogParams[0] = env.fog == FogMode::None ? 0.f : (env.fog == FogMode::Linear ? 1.f : 2.f);
        block.fogParams[1] = env.fog == FogMode::Exponential ? env.fogDensity : env.fogStart;
        block.fogParams[2] = env.fogEnd;
        block.fogParams[3] = env.sky == SkyMode::Panorama && s.panoramaTexture ? 2.f : (env.sky == SkyMode::None ? 0.f : 1.f);
    }
    device.bindUniformBlock(&block, sizeof(block));

    const bool post = s.environment.needsPostProcess() && s.postReady &&
                      s.ensurePostTargets(s.viewportWidth, s.viewportHeight) &&
                      device.beginRenderTarget(s.sceneTarget, render::ClearColor{0.f, 0.f, 0.f, 0.f});
    if (post)
        device.setViewport(0, 0, s.viewportWidth, s.viewportHeight);
    else
        device.setViewport(s.viewportX, s.viewportY, s.viewportWidth, s.viewportHeight);

    if (s.environment.sky != SkyMode::None && s.skyPipeline != render::kInvalidHandle)
    {
        if (s.environment.sky == SkyMode::Panorama && s.panoramaLoaded != s.environment.panorama)
        {
            s.panoramaLoaded  = s.environment.panorama;
            s.panoramaTexture = s.environment.panorama.empty()
                                    ? nullptr
                                    : renderer::TextureCache::instance().loadSync(
                                          s.environment.panorama, render::TextureFilter::Linear, render::TextureWrap::Repeat);
        }
        device.bindPipeline(s.skyPipeline);
        device.bindTexture(s.panoramaTexture ? s.panoramaTexture->handle : s.whiteTexture);
        device.draw(3);
        s.currentPipeline = render::kInvalidHandle;
    }

    s.frustum.build(s.viewProj);
    s.culled    = 0;
    s.instanced = 0;
    std::vector<Command*> candidates;
    for (Command& c : s.commands)
    {
        if (isTranslucent(c.style) || !s.visible(c))
            continue;
        if (Impl::instanceable(c))
            candidates.push_back(&c);
        else
            s.emit(c, c.style.doubleSided ? s.opaqueTwoSided : s.opaquePipeline);
    }
    s.flush();
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Command* a, const Command* b) { return Impl::groupKey(*a) < Impl::groupKey(*b); });
    for (std::size_t begin = 0; begin < candidates.size();)
    {
        std::size_t end = begin + 1;
        while (end < candidates.size() && Impl::groupKey(*candidates[end]) == Impl::groupKey(*candidates[begin]))
            ++end;
        if (end - begin < 2 || !s.drawInstanced(candidates, begin, end))
            for (std::size_t i = begin; i < end; ++i)
                s.emit(*candidates[i], candidates[i]->style.doubleSided ? s.opaqueTwoSided : s.opaquePipeline);
        begin = end;
    }
    s.flush();

    s.emitLines(LineMode::DepthTested);
    s.flush();

    std::vector<const Command*> translucent;
    for (Command& c : s.commands)
        if (isTranslucent(c.style) && s.visible(c))
            translucent.push_back(&c);
    std::stable_sort(translucent.begin(), translucent.end(),
                     [](const Command* a, const Command* b) { return a->distanceSq > b->distanceSq; });
    for (const Command* c : translucent)
        s.emit(*c, c->style.doubleSided ? s.translucentTwoSided : s.translucentPipeline);
    s.flush();

    s.emitLines(LineMode::Overlay);
    s.flush();

    if (post)
        s.runPost(device);
    device.resetViewport();

    s.commands.clear();
    s.lines.clear();
    s.locals.clear();
}

}
