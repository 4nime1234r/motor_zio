#pragma once

#include <vector>

#include "core/Gizmo.h"
#include "zio/math/Vec3.h"
#include "zio/renderer/Color.h"
#include "zio/scene3d/Node3D.h"
#include "zio/scene3d/Renderer3D.h"

namespace zio_editor
{

struct Segment
{
    zio::math::Vec3 a;
    zio::math::Vec3 b;
    zio::renderer::Color color;
    float widthPixels = 2.f;
};

std::vector<Segment> buildSelectionSegments(const zio::scene3d::Node3D& node, const zio::math::Vec3& cameraPosition,
                                            int hoveredAxis = -1, int activeAxis = -1,
                                            GizmoMode mode = GizmoMode::Move);

void drawSelection(zio::scene3d::Renderer3D& renderer, const zio::scene3d::Node3D& node,
                   const zio::math::Vec3& cameraPosition, int hoveredAxis = -1, int activeAxis = -1,
                   GizmoMode mode = GizmoMode::Move);

std::vector<Segment> buildBoxSegments(const zio::scene3d::Node3D& node);

void drawSelectionBox(zio::scene3d::Renderer3D& renderer, const zio::scene3d::Node3D& node);

bool selectionFocus(const zio::scene3d::Node3D& node, zio::math::Vec3& center, float& radius);

}
