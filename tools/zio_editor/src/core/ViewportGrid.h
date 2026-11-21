#pragma once

#include <vector>

#include "zio/math/Vec3.h"
#include "zio/renderer/Color.h"
#include "zio/scene3d/Renderer3D.h"

namespace zio_editor
{

struct GridSegment
{
    zio::math::Vec3 a;
    zio::math::Vec3 b;
    zio::renderer::Color colorA;
    zio::renderer::Color colorB;
    float widthPixels = 1.f;
};

float gridSpacing(float cameraDistance);

std::vector<GridSegment> buildGridSegments(const zio::math::Vec3& focus, float cameraDistance);

void drawGrid(zio::scene3d::Renderer3D& renderer, const zio::math::Vec3& focus, float cameraDistance);

}
