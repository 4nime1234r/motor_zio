#include "core/SelectionGizmo.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/Gizmo.h"

namespace zio_editor
{

namespace
{

constexpr zio::renderer::Color kBoxColor    = {1.f, 0.65f, 0.1f, 1.f};
constexpr zio::renderer::Color kAxisColor[3] = {{1.f, 0.25f, 0.25f, 1.f}, {0.3f, 1.f, 0.3f, 1.f}, {0.35f, 0.55f, 1.f, 1.f}};
constexpr zio::renderer::Color kActiveColor = {1.f, 0.92f, 0.25f, 1.f};
constexpr float kBoxWidth                   = 2.f;
constexpr float kAxisWidth                  = 3.f;
constexpr float kHighlightWidth             = 4.5f;
constexpr float kHeadWidth                  = 2.5f;
constexpr float kHeadLengthRatio            = 0.2f;
constexpr float kHeadRadiusRatio            = 0.4f;
constexpr float kScaleHandleRatio           = 0.07f;

struct Bounds
{
    zio::math::Vec3 min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max()};
    zio::math::Vec3 max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                           std::numeric_limits<float>::lowest()};
    bool valid          = false;

    void add(const zio::math::Vec3& p)
    {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
        valid = true;
    }
};

void collectWorldBounds(const zio::scene3d::Node3D& node, Bounds& bounds)
{
    if (const auto* meshNode = dynamic_cast<const zio::scene3d::MeshNode*>(&node))
        if (const auto& mesh = meshNode->mesh())
            for (const auto& vertex : mesh->vertices)
                bounds.add(node.worldMatrix().transformPoint(vertex.position));

    for (const auto& child : node.children())
        collectWorldBounds(*child, bounds);
}

void addBox(std::vector<Segment>& out, const zio::math::Vec3 corners[8])
{
    for (int i = 0; i < 8; ++i)
        for (int bit = 0; bit < 3; ++bit)
            if (((i >> bit) & 1) == 0)
                out.push_back({corners[i], corners[i | (1 << bit)], kBoxColor, kBoxWidth});
}

bool meshLocalBounds(const zio::scene3d::MeshNode& node, Bounds& local)
{
    if (!node.mesh() || node.mesh()->vertices.empty())
        return false;
    for (const auto& vertex : node.mesh()->vertices)
        local.add(vertex.position);
    return true;
}

zio::renderer::Color axisColor(int axis, int hoveredAxis, int activeAxis)
{
    if (axis == activeAxis)
        return kActiveColor;
    zio::renderer::Color color = kAxisColor[axis];
    if (axis == hoveredAxis)
    {
        color.r += (1.f - color.r) * 0.55f;
        color.g += (1.f - color.g) * 0.55f;
        color.b += (1.f - color.b) * 0.55f;
    }
    return color;
}

}

std::vector<Segment> buildBoxSegments(const zio::scene3d::Node3D& node)
{
    std::vector<Segment> segments;

    Bounds local;
    const auto* meshNode = dynamic_cast<const zio::scene3d::MeshNode*>(&node);
    if (meshNode && meshLocalBounds(*meshNode, local))
    {
        zio::math::Vec3 corners[8];
        for (int i = 0; i < 8; ++i)
        {
            const zio::math::Vec3 p = {(i & 1) ? local.max.x : local.min.x, (i & 2) ? local.max.y : local.min.y,
                                       (i & 4) ? local.max.z : local.min.z};
            corners[i]              = node.worldMatrix().transformPoint(p);
        }
        addBox(segments, corners);
    }
    else
    {
        Bounds world;
        collectWorldBounds(node, world);
        if (world.valid)
        {
            zio::math::Vec3 corners[8];
            for (int i = 0; i < 8; ++i)
                corners[i] = {(i & 1) ? world.max.x : world.min.x, (i & 2) ? world.max.y : world.min.y,
                              (i & 4) ? world.max.z : world.min.z};
            addBox(segments, corners);
        }
    }
    return segments;
}

std::vector<Segment> buildSelectionSegments(const zio::scene3d::Node3D& node, const zio::math::Vec3& cameraPosition,
                                            int hoveredAxis, int activeAxis, GizmoMode mode)
{
    std::vector<Segment> segments = buildBoxSegments(node);

    const GizmoAxes axes = computeGizmoAxes(node, cameraPosition);

    if (mode == GizmoMode::Rotate)
    {
        constexpr float kTwoPi = 6.28318530718f;
        for (int i = 0; i < 3; ++i)
        {
            const bool highlighted = i == hoveredAxis || i == activeAxis;
            const float width      = highlighted ? kHighlightWidth : kAxisWidth;
            const auto color       = axisColor(i, hoveredAxis, activeAxis);
            zio::math::Vec3 previous = ringPoint(axes, i, 0.f);
            for (int s = 1; s <= kRingSegments; ++s)
            {
                const zio::math::Vec3 point = ringPoint(axes, i, kTwoPi * static_cast<float>(s) / kRingSegments);
                segments.push_back({previous, point, color, width});
                previous = point;
            }
        }
        return segments;
    }

    for (int i = 0; i < 3; ++i)
    {
        const bool highlighted = i == hoveredAxis || i == activeAxis;
        segments.push_back({axes.origin, axes.origin + axes.direction[i] * axes.length,
                            axisColor(i, hoveredAxis, activeAxis), highlighted ? kHighlightWidth : kAxisWidth});
    }

    if (mode == GizmoMode::Scale)
    {
        const float half = axes.length * kScaleHandleRatio;
        for (int i = 0; i < 3; ++i)
        {
            const zio::math::Vec3 tip        = axes.origin + axes.direction[i] * axes.length;
            const zio::renderer::Color color = axisColor(i, hoveredAxis, activeAxis);
            zio::math::Vec3 corners[8];
            for (int c = 0; c < 8; ++c)
                corners[c] = tip + axes.direction[0] * ((c & 1) ? half : -half) +
                             axes.direction[1] * ((c & 2) ? half : -half) + axes.direction[2] * ((c & 4) ? half : -half);
            for (int c = 0; c < 8; ++c)
                for (int bit = 0; bit < 3; ++bit)
                    if (((c >> bit) & 1) == 0)
                        segments.push_back({corners[c], corners[c | (1 << bit)], color, kHeadWidth});
        }
        return segments;
    }

    for (int i = 0; i < 3; ++i)
    {
        const zio::math::Vec3 direction = axes.direction[i];
        const zio::math::Vec3 tip       = axes.origin + direction * axes.length;
        const zio::math::Vec3 helper    = std::fabs(direction.y) < 0.9f ? zio::math::Vec3::unitY() : zio::math::Vec3::unitX();
        const zio::math::Vec3 u         = direction.cross(helper).normalized();
        const zio::math::Vec3 v         = direction.cross(u);
        const float headLength          = axes.length * kHeadLengthRatio;
        const zio::math::Vec3 base      = tip - direction * headLength;
        const float radius              = headLength * kHeadRadiusRatio;
        const zio::renderer::Color color = axisColor(i, hoveredAxis, activeAxis);

        for (const zio::math::Vec3& offset : {u * radius, u * -radius, v * radius, v * -radius})
            segments.push_back({tip, base + offset, color, kHeadWidth});
    }
    return segments;
}

void drawSelection(zio::scene3d::Renderer3D& renderer, const zio::scene3d::Node3D& node,
                   const zio::math::Vec3& cameraPosition, int hoveredAxis, int activeAxis, GizmoMode mode)
{
    for (const Segment& segment : buildSelectionSegments(node, cameraPosition, hoveredAxis, activeAxis, mode))
        renderer.drawLine(segment.a, segment.b, segment.color, segment.widthPixels);
}

void drawSelectionBox(zio::scene3d::Renderer3D& renderer, const zio::scene3d::Node3D& node)
{
    for (const Segment& segment : buildBoxSegments(node))
        renderer.drawLine(segment.a, segment.b, segment.color, segment.widthPixels);
}

bool selectionFocus(const zio::scene3d::Node3D& node, zio::math::Vec3& center, float& radius)
{
    Bounds bounds;
    const auto* meshNode = dynamic_cast<const zio::scene3d::MeshNode*>(&node);
    Bounds local;
    if (meshNode && meshLocalBounds(*meshNode, local))
    {
        for (int i = 0; i < 8; ++i)
            bounds.add(node.worldMatrix().transformPoint({(i & 1) ? local.max.x : local.min.x,
                                                          (i & 2) ? local.max.y : local.min.y,
                                                          (i & 4) ? local.max.z : local.min.z}));
    }
    else
    {
        collectWorldBounds(node, bounds);
    }

    if (!bounds.valid)
    {
        center = node.worldMatrix().transformPoint(zio::math::Vec3::zero());
        radius = 1.f;
        return false;
    }

    center = (bounds.min + bounds.max) * 0.5f;
    radius = std::max(0.05f, (bounds.max - bounds.min).length() * 0.5f);
    return true;
}

}
