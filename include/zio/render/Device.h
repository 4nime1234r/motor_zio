#pragma once

#include <cstdint>
#include <memory>

#include "zio/core/Platform.h"
#include "zio/render/Types.h"

namespace zio::render
{

class ZIO_API Device
{
public:
    Device();
    ~Device();

    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;

    static Device& instance();

    bool init(void* nativeWindow, std::uint32_t width, std::uint32_t height);
    void shutdown();
    bool isInitialized() const;

    bool resize(std::uint32_t width, std::uint32_t height);

    BufferHandle createBuffer(const BufferDesc& desc);
    void destroyBuffer(BufferHandle handle);
    bool updateBuffer(BufferHandle handle, const void* data, std::uint32_t sizeBytes);

    PipelineHandle createPipeline(const PipelineDesc& desc);
    void destroyPipeline(PipelineHandle handle);

    TextureHandle createTexture(const TextureDesc& desc);
    void destroyTexture(TextureHandle handle);

    void beginFrame(const ClearColor& clear);
    void bindPipeline(PipelineHandle pipeline);
    void bindVertexBuffer(BufferHandle buffer, std::uint32_t strideBytes);
    void bindTexture(TextureHandle texture, std::uint32_t slot = 0);
    void draw(std::uint32_t vertexCount, std::uint32_t startVertex = 0);

    bool beginShadowPass();
    void endShadowPass();
    void bindShadowMap();
    std::uint32_t shadowMapSize() const;

    void endFrame();

    bool readBackPixel(std::uint32_t x, std::uint32_t y, std::uint8_t outRgba[4]);

    void* nativeDevice() const;
    void* nativeContext() const;

    void* nativeInstance() const;
    void* nativePhysicalDevice() const;
    void* nativeQueue() const;
    std::uint32_t nativeQueueFamily() const;
    void* nativeRenderPass() const;
    std::uint32_t swapchainImageCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
