#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "zio/core/Platform.h"
#include "zio/math/Vec2.h"
#include "zio/render/Types.h"
#include "zio/renderer/Color.h"

namespace zio::renderer
{

class ZIO_API BatchRenderer
{
public:
    BatchRenderer();
    ~BatchRenderer();

    BatchRenderer(const BatchRenderer&)            = delete;
    BatchRenderer& operator=(const BatchRenderer&) = delete;

    static BatchRenderer& instance();

    bool ensureInitialized();

    render::PipelineHandle defaultPipeline() const;

    void begin(std::uint32_t viewportWidth, std::uint32_t viewportHeight);

    void submitQuad(const std::array<math::Vec2, 4>& positionsPx, const std::array<math::Vec2, 4>& uvs, Color color,
                     render::TextureHandle texture, render::PipelineHandle pipeline = render::kInvalidHandle);

    void end();

    struct FrameStats
    {
        std::uint32_t drawCalls = 0;
        std::uint32_t quadCount = 0;
    };

    FrameStats lastFrameStats() const;

private:
    void flush();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
