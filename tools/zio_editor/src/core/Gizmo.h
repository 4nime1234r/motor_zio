#pragma once

#include <optional>

#include "core/Picking.h"
#include "zio/math/Vec3.h"
#include "zio/scene3d/Camera.h"
#include "zio/scene3d/Node3D.h"

namespace zio_editor
{

enum class GizmoMode
{
    Move,
    Rotate,
    Scale
};

constexpr int kRingSegments = 48;

struct GizmoAxes
{
    zio::math::Vec3 origin;
    zio::math::Vec3 direction[3];
    float length = 0.f;
};

GizmoAxes computeGizmoAxes(const zio::scene3d::Node3D& node, const zio::math::Vec3& cameraPosition);

bool projectToScreen(const zio::scene3d::Camera& camera, float widthPx, float heightPx, const zio::math::Vec3& point,
                     float& x, float& y);

int hitTestGizmo(const zio::scene3d::Camera& camera, float widthPx, float heightPx, const GizmoAxes& axes,
                 float mouseX, float mouseY, float thresholdPx = 9.f);

std::optional<float> axisParameterFromRay(const Ray& ray, const zio::math::Vec3& axisOrigin,
                                          const zio::math::Vec3& axisDirection);

zio::math::Vec3 ringPoint(const GizmoAxes& axes, int axis, float angleRadians);

int hitTestRings(const zio::scene3d::Camera& camera, float widthPx, float heightPx, const GizmoAxes& axes,
                 float mouseX, float mouseY, float thresholdPx = 9.f);

zio::math::Vec3 localPositionFor(const zio::scene3d::Node3D& node, const zio::math::Vec3& worldPosition);

class RotateDrag
{
public:
    bool active() const { return active_; }
    int axis() const { return axis_; }

    bool begin(int axis, const zio::math::Vec3& origin, const zio::math::Vec3& normal, const Ray& ray);
    std::optional<float> update(const Ray& ray, float snapRadians);
    void end() { active_ = false; axis_ = -1; }

private:
    bool active_ = false;
    int axis_    = -1;
    zio::math::Vec3 origin_;
    zio::math::Vec3 normal_;
    zio::math::Vec3 startVector_;
    float lastRaw_ = 0.f;
    float total_   = 0.f;
};

class GizmoDrag
{
public:
    bool active() const { return active_; }
    int axis() const { return axis_; }

    bool begin(int axis, const zio::math::Vec3& origin, const zio::math::Vec3& direction, const Ray& ray);
    std::optional<zio::math::Vec3> update(const Ray& ray, float snapStep) const;
    void end() { active_ = false; axis_ = -1; }

private:
    bool active_ = false;
    int axis_    = -1;
    zio::math::Vec3 origin_;
    zio::math::Vec3 direction_;
    float startParameter_ = 0.f;
};

}
