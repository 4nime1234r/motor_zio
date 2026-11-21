#pragma once

#include "zio/math/Vec3.h"
#include "zio/scene3d/Camera.h"
#include "zio/scene3d/Node3D.h"

namespace zio_editor
{

struct Ray
{
    zio::math::Vec3 origin;
    zio::math::Vec3 direction;
};

Ray makeRay(const zio::scene3d::Camera& camera, float aspect, float ndcX, float ndcY);

bool intersectTriangle(const Ray& ray, zio::math::Vec3 a, zio::math::Vec3 b, zio::math::Vec3 c, float& distance);

zio::scene3d::Node3D* pickNode(zio::scene3d::Node3D& root, const Ray& ray, float* distance = nullptr);

}
