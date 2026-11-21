#include "core/ViewportGrid.h"

#include <algorithm>
#include <cmath>

namespace zio_editor
{

namespace
{

constexpr int kHalfCells    = 50;
constexpr int kMajorEvery   = 10;
constexpr int kPieces       = 10;
constexpr float kMaxSpacing = 1e6f;
constexpr float kCellsAcrossView = 40.f;

constexpr zio::renderer::Color kMinor  = {0.62f, 0.66f, 0.74f, 0.30f};
constexpr zio::renderer::Color kMajor  = {0.75f, 0.80f, 0.90f, 0.55f};
constexpr zio::renderer::Color kAxisX  = {0.95f, 0.27f, 0.27f, 0.95f};
constexpr zio::renderer::Color kAxisZ  = {0.33f, 0.55f, 1.00f, 0.95f};

float fadeAt(float distance, float radius)
{
    const float t = std::clamp(distance / radius, 0.f, 1.f);
    return (1.f - t) * (1.f - t);
}

zio::renderer::Color faded(zio::renderer::Color color, float fade)
{
    color.a *= fade;
    return color;
}

}

float gridSpacing(float cameraDistance)
{
    float spacing = 1.f;
    while (cameraDistance > kCellsAcrossView * spacing && spacing < kMaxSpacing)
        spacing *= 10.f;
    return spacing;
}

std::vector<GridSegment> buildGridSegments(const zio::math::Vec3& focus, float cameraDistance)
{
    std::vector<GridSegment> segments;
    if (!std::isfinite(focus.x) || !std::isfinite(focus.z) || !std::isfinite(cameraDistance))
        return segments;

    const float spacing = gridSpacing(cameraDistance);
    const float radius  = kHalfCells * spacing;
    const float snap    = spacing * kMajorEvery;
    const float cx      = std::round(focus.x / snap) * snap;
    const float cz      = std::round(focus.z / snap) * snap;

    segments.reserve(2 * (2 * kHalfCells + 1) * kPieces);

    auto addLine = [&](bool alongX, int index) {
        const float fixedCoord = (alongX ? cz : cx) + static_cast<float>(index) * spacing;
        const long long cell   = std::llround(fixedCoord / spacing);

        zio::renderer::Color base = kMinor;
        float width               = 1.f;
        if (cell == 0)
        {
            base  = alongX ? kAxisX : kAxisZ;
            width = 2.f;
        }
        else if (cell % kMajorEvery == 0)
        {
            base  = kMajor;
            width = 1.5f;
        }

        const float start = (alongX ? cx : cz) - radius;
        const float step  = 2.f * radius / static_cast<float>(kPieces);
        for (int piece = 0; piece < kPieces; ++piece)
        {
            const float t0 = start + step * static_cast<float>(piece);
            const float t1 = t0 + step;

            const zio::math::Vec3 a = alongX ? zio::math::Vec3{t0, 0.f, fixedCoord} : zio::math::Vec3{fixedCoord, 0.f, t0};
            const zio::math::Vec3 b = alongX ? zio::math::Vec3{t1, 0.f, fixedCoord} : zio::math::Vec3{fixedCoord, 0.f, t1};

            const float fadeA = fadeAt(std::hypot(a.x - focus.x, a.z - focus.z), radius);
            const float fadeB = fadeAt(std::hypot(b.x - focus.x, b.z - focus.z), radius);
            if (fadeA < 0.01f && fadeB < 0.01f)
                continue;

            segments.push_back({a, b, faded(base, fadeA), faded(base, fadeB), width});
        }
    };

    for (int i = -kHalfCells; i <= kHalfCells; ++i)
    {
        addLine(true, i);
        addLine(false, i);
    }
    return segments;
}

void drawGrid(zio::scene3d::Renderer3D& renderer, const zio::math::Vec3& focus, float cameraDistance)
{
    for (const GridSegment& segment : buildGridSegments(focus, cameraDistance))
        renderer.drawLine(segment.a, segment.b, segment.colorA, segment.colorB, segment.widthPixels,
                          zio::scene3d::LineMode::DepthTested);
}

}
