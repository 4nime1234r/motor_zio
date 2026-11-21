#include "zio/renderer/BatchRenderer.h"

#include <cstddef>
#include <vector>

#include "zio/render/Device.h"

#include "quad_frag.spv.h"
#include "quad_vert.spv.h"

namespace zio::renderer
{

namespace
{

struct Vertex2D
{
    float x, y;
    float u, v;
    float r, g, b, a;
};

constexpr std::size_t kMaxQuadsPerBatch = 4096;
constexpr std::size_t kVerticesPerQuad  = 6;
constexpr std::size_t kMaxVertices      = kMaxQuadsPerBatch * kVerticesPerQuad;

}

struct BatchRenderer::Impl
{
    bool initialized                   = false;
    render::PipelineHandle pipeline    = render::kInvalidHandle;
    render::BufferHandle vertexBuffer  = render::kInvalidHandle;

    std::vector<Vertex2D> pending;
    render::TextureHandle currentTexture   = render::kInvalidHandle;
    render::PipelineHandle currentPipeline = render::kInvalidHandle;

    float ndcScaleX = 0.f, ndcScaleY = 0.f;

    BatchRenderer::FrameStats frameStats;

    bool init()
    {
        auto& device = render::Device::instance();

        render::VertexAttribute attrs[] = {
            {"POSITION", 0, render::VertexFormat::Float2, offsetof(Vertex2D, x)},
            {"TEXCOORD", 0, render::VertexFormat::Float2, offsetof(Vertex2D, u)},
            {"COLOR", 0, render::VertexFormat::Float4, offsetof(Vertex2D, r)},
        };
        render::PipelineDesc pipelineDesc{};
        pipelineDesc.vertexShaderSpirv = zio_quad_vert_spirv;
        pipelineDesc.pixelShaderSpirv  = zio_quad_frag_spirv;
        pipelineDesc.attributes         = attrs;
        pipelineDesc.attributeCount     = 3;
        pipelineDesc.vertexStrideBytes  = sizeof(Vertex2D);
        pipeline                        = device.createPipeline(pipelineDesc);
        if (pipeline == render::kInvalidHandle)
            return false;

        render::BufferDesc vbDesc{};
        vbDesc.type      = render::BufferType::Vertex;
        vbDesc.usage     = render::BufferUsage::Dynamic;
        vbDesc.data      = nullptr;
        vbDesc.sizeBytes = kMaxVertices * sizeof(Vertex2D);
        vertexBuffer     = device.createBuffer(vbDesc);
        if (vertexBuffer == render::kInvalidHandle)
            return false;

        pending.reserve(kMaxVertices);
        initialized = true;
        return true;
    }

    math::Vec2 toNdc(math::Vec2 px) const { return {px.x * ndcScaleX - 1.f, 1.f - px.y * ndcScaleY}; }
};

BatchRenderer::BatchRenderer() : impl_(std::make_unique<Impl>()) {}
BatchRenderer::~BatchRenderer() = default;

BatchRenderer& BatchRenderer::instance()
{
    static BatchRenderer global;
    return global;
}

bool BatchRenderer::ensureInitialized()
{
    if (impl_->initialized)
        return true;
    return impl_->init();
}

render::PipelineHandle BatchRenderer::defaultPipeline() const
{
    return impl_->pipeline;
}

void BatchRenderer::begin(std::uint32_t viewportWidth, std::uint32_t viewportHeight)
{
    ensureInitialized();
    impl_->ndcScaleX      = viewportWidth > 0 ? 2.f / static_cast<float>(viewportWidth) : 0.f;
    impl_->ndcScaleY      = viewportHeight > 0 ? 2.f / static_cast<float>(viewportHeight) : 0.f;
    impl_->currentTexture  = render::kInvalidHandle;
    impl_->currentPipeline = render::kInvalidHandle;
    impl_->pending.clear();
    impl_->frameStats = {};
}

void BatchRenderer::submitQuad(const std::array<math::Vec2, 4>& positionsPx, const std::array<math::Vec2, 4>& uvs,
                                Color color, render::TextureHandle texture, render::PipelineHandle pipeline)
{
    if (!impl_->initialized)
        return;

    const render::PipelineHandle effectivePipeline = pipeline == render::kInvalidHandle ? impl_->pipeline : pipeline;

    const bool batchBreak = texture != impl_->currentTexture || effectivePipeline != impl_->currentPipeline;
    const bool wouldOverflow = impl_->pending.size() + kVerticesPerQuad > kMaxVertices;
    if (batchBreak || wouldOverflow)
    {
        flush();
        impl_->currentTexture  = texture;
        impl_->currentPipeline = effectivePipeline;
    }

    constexpr int order[kVerticesPerQuad] = {0, 1, 2, 2, 1, 3};
    for (int idx : order)
    {
        const math::Vec2 ndc = impl_->toNdc(positionsPx[idx]);
        impl_->pending.push_back(
            Vertex2D{ndc.x, ndc.y, uvs[idx].x, uvs[idx].y, color.r, color.g, color.b, color.a});
    }
}

void BatchRenderer::flush()
{
    if (impl_->pending.empty())
        return;

    auto& device = render::Device::instance();
    device.updateBuffer(impl_->vertexBuffer, impl_->pending.data(),
                         static_cast<std::uint32_t>(impl_->pending.size() * sizeof(Vertex2D)));
    device.bindPipeline(impl_->currentPipeline);
    device.bindVertexBuffer(impl_->vertexBuffer, sizeof(Vertex2D));
    device.bindTexture(impl_->currentTexture);
    device.draw(static_cast<std::uint32_t>(impl_->pending.size()));

    ++impl_->frameStats.drawCalls;
    impl_->frameStats.quadCount += static_cast<std::uint32_t>(impl_->pending.size() / kVerticesPerQuad);

    impl_->pending.clear();
}

void BatchRenderer::end()
{
    flush();
}

BatchRenderer::FrameStats BatchRenderer::lastFrameStats() const
{
    return impl_->frameStats;
}

}
