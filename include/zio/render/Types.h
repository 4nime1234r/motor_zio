#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace zio::render
{

using BufferHandle   = std::uint32_t;
using PipelineHandle = std::uint32_t;
using TextureHandle  = std::uint32_t;
inline constexpr std::uint32_t kInvalidHandle = 0;

enum class BufferType
{
    Vertex,
    Index,
};

enum class BufferUsage
{
    Immutable,
    Dynamic,
};

struct BufferDesc
{
    BufferType type    = BufferType::Vertex;
    BufferUsage usage  = BufferUsage::Immutable;
    const void* data   = nullptr;
    std::size_t sizeBytes = 0;
};

enum class VertexFormat
{
    Float2,
    Float3,
    Float4,
};

struct VertexAttribute
{
    const char* semanticName;
    std::uint32_t semanticIndex = 0;
    VertexFormat format         = VertexFormat::Float3;
    std::uint32_t offsetBytes   = 0;
};

struct PipelineDesc
{
    std::span<const std::uint32_t> vertexShaderSpirv;
    std::span<const std::uint32_t> pixelShaderSpirv;
    const VertexAttribute* attributes = nullptr;
    std::size_t attributeCount        = 0;
    std::uint32_t vertexStrideBytes   = 0;
    bool depthTest = false;
    bool depthWrite = true;
    bool cullBack = false;
    bool stencilOnce = false;
    bool shadowPass = false;
};

struct ClearColor
{
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
};

enum class TextureFilter
{
    Point,
    Linear,
};

enum class TextureWrap
{
    Clamp,
    Repeat,
};

struct TextureDesc
{
    std::uint32_t width      = 0;
    std::uint32_t height     = 0;
    const void* pixelsRgba8  = nullptr;
    TextureFilter filter     = TextureFilter::Point;
    TextureWrap wrap         = TextureWrap::Clamp;
};

}
