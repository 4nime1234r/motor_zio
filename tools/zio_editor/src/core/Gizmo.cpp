#include "core/Gizmo.h"

#include <algorithm>
#include <cmath>

namespace zio_editor
{

namespace
{

constexpr float kAxisLengthPerDistance = 0.12f;
constexpr float kMinAxisLength         = 0.05f;
constexpr float kParallelEpsilon       = 1e-5f;

zio::math::Vec3 axisDirection(const zio::math::Mat4& world, const zio::math::Vec3& unit)
{
    const zio::math::Vec3 direction = world.transformVector(unit);
    return direction.length() > 1e-6f ? direction.normalized() : unit;
}

float distanceToSegment(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx    = bx - ax;
    const float dy    = by - ay;
    const float lenSq = dx * dx + dy * dy;
    float t           = 0.f;
    if (lenSq > 1e-9f)
        t = std::clamp(((px - ax) * dx + (py - ay) * dy) / lenSq, 0.f, 1.f);
    const float cx = ax + dx * t;
    const float cy = ay + dy * t;
    return std::hypot(px - cx, py - cy);
}


std::optional<zio::math::Vec3> planeHit(const Ray& ray, const zio::math::Vec3& origin, const zio::math::Vec3& normal)
{
    const float denominator = normal.dot(ray.direction);
    if (std::fabs(denominator) < 1e-4f)
        return std::nullopt;
    const float t = normal.dot(origin - ray.origin) / denominator;
    if (!(t >= 0.f) || !std::isfinite(t))
        return std::nullopt;
    return ray.origin + ray.direction * t;
}

}

GizmoAxes computeGizmoAxes(const zio::scene3d::Node3D& node, const zio::math::Vec3& cameraPosition)
{
    GizmoAxes axes;
    axes.origin       = node.worldMatrix().transformPoint(zio::math::Vec3::zero());
    axes.direction[0] = axisDirection(node.worldMatrix(), zio::math::Vec3::unitX());
    axes.direction[1] = axisDirection(node.worldMatrix(), zio::math::Vec3::unitY());
    axes.direction[2] = axisDirection(node.worldMatrix(), zio::math::Vec3::unitZ());
    axes.length       = std::max(kMinAxisLength, (axes.origin - cameraPosition).length() * kAxisLengthPerDistance);
    return axes;
}

bool projectToScreen(const zio::scene3d::Camera& camera, float widthPx, float heightPx, const zio::math::Vec3& point,
                     float& x, float& y)
{
    if (!(widthPx > 0.f) || !(heightPx > 0.f))
        return false;

    const zio::math::Mat4 viewProj = camera.projectionMatrix(widthPx / heightPx) * camera.viewMatrix();
    const float(&m)[4][4]          = viewProj.m;
    const float cx = m[0][0] * point.x + m[0][1] * point.y + m[0][2] * point.z + m[0][3];
    const float cy = m[1][0] * point.x + m[1][1] * point.y + m[1][2] * point.z + m[1][3];
    const float cw = m[3][0] * point.x + m[3][1] * point.y + m[3][2] * point.z + m[3][3];
    if (!(cw >= camera.nearZ) || !std::isfinite(cw))
        return false;

    x = (cx / cw * 0.5f + 0.5f) * widthPx;
    y = (0.5f - cy / cw * 0.5f) * heightPx;
    return true;
}

int hitTestGizmo(const zio::scene3d::Camera& camera, float widthPx, float heightPx, const GizmoAxes& axes,
                 float mouseX, float mouseY, float thresholdPx)
{
    float ox = 0.f, oy = 0.f;
    if (!projectToScreen(camera, widthPx, heightPx, axes.origin, ox, oy))
        return -1;

    int best          = -1;
    float bestDistance = thresholdPx;
    for (int i = 0; i < 3; ++i)
    {
        float tx = 0.f, ty = 0.f;
        if (!projectToScreen(camera, widthPx, heightPx, axes.origin + axes.direction[i] * axes.length, tx, ty))
            continue;
        const float distance = distanceToSegment(mouseX, mouseY, ox, oy, tx, ty);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best         = i;
        }
    }
    return best;
}

zio::math::Vec3 ringPoint(const GizmoAxes& axes, int axis, float angleRadians)
{
    const zio::math::Vec3 u = axes.direction[(axis + 1) % 3];
    const zio::math::Vec3 v = axes.direction[axis].cross(u).normalized();
    return axes.origin + (u * std::cos(angleRadians) + v * std::sin(angleRadians)) * axes.length;
}

int hitTestRings(const zio::scene3d::Camera& camera, float widthPx, float heightPx, const GizmoAxes& axes,
                 float mouseX, float mouseY, float thresholdPx)
{
    constexpr float kTwoPi = 6.28318530718f;
    int best               = -1;
    float bestDistance     = thresholdPx;
    for (int axis = 0; axis < 3; ++axis)
    {
        float px = 0.f, py = 0.f;
        bool havePrevious = projectToScreen(camera, widthPx, heightPx, ringPoint(axes, axis, 0.f), px, py);
        for (int s = 1; s <= kRingSegments; ++s)
        {
            float x = 0.f, y = 0.f;
            const bool ok = projectToScreen(camera, widthPx, heightPx,
                                            ringPoint(axes, axis, kTwoPi * static_cast<float>(s) / kRingSegments), x, y);
            if (ok && havePrevious)
            {
                const float distance = distanceToSegment(mouseX, mouseY, px, py, x, y);
                if (distance <= bestDistance)
                {
                    bestDistance = distance;
                    best         = axis;
                }
            }
            px           = x;
            py           = y;
            havePrevious = ok;
        }
    }
    return best;
}

std::optional<float> axisParameterFromRay(const Ray& ray, const zio::math::Vec3& axisOrigin,
                                          const zio::math::Vec3& axisDirection)
{
    const zio::math::Vec3 w0 = axisOrigin - ray.origin;
    const float b            = axisDirection.dot(ray.direction);
    const float d            = axisDirection.dot(w0);
    const float e            = ray.direction.dot(w0);
    const float denominator  = 1.f - b * b;
    if (denominator < kParallelEpsilon)
        return std::nullopt;

    const float parameter = (b * e - d) / denominator;
    if (!std::isfinite(parameter))
        return std::nullopt;
    return parameter;
}

zio::math::Vec3 localPositionFor(const zio::scene3d::Node3D& node, const zio::math::Vec3& worldPosition)
{
    if (const zio::scene3d::Node3D* parent = node.parent())
        return parent->worldMatrix().inverseAffine().transformPoint(worldPosition);
    return worldPosition;
}

bool RotateDrag::begin(int axis, const zio::math::Vec3& origin, const zio::math::Vec3& normal, const Ray& ray)
{
    if (axis < 0 || axis > 2)
        return false;
    const auto hit = planeHit(ray, origin, normal);
    if (!hit)
        return false;
    const zio::math::Vec3 start = *hit - origin;
    if (start.length() < 1e-5f)
        return false;

    active_      = true;
    axis_        = axis;
    origin_      = origin;
    normal_      = normal;
    startVector_ = start;
    lastRaw_     = 0.f;
    total_       = 0.f;
    return true;
}

std::optional<float> RotateDrag::update(const Ray& ray, float snapRadians)
{
    if (!active_)
        return std::nullopt;
    const auto hit = planeHit(ray, origin_, normal_);
    if (!hit)
        return std::nullopt;
    const zio::math::Vec3 current = *hit - origin_;
    if (current.length() < 1e-5f)
        return std::nullopt;

    constexpr float kPi = 3.14159265359f;
    const float raw     = std::atan2(normal_.dot(startVector_.cross(current)), startVector_.dot(current));
    float step          = raw - lastRaw_;
    while (step > kPi)
        step -= 2.f * kPi;
    while (step < -kPi)
        step += 2.f * kPi;
    total_ += step;
    lastRaw_ = raw;

    if (snapRadians > 0.f)
        return std::round(total_ / snapRadians) * snapRadians;
    return total_;
}

bool GizmoDrag::begin(int axis, const zio::math::Vec3& origin, const zio::math::Vec3& direction, const Ray& ray)
{
    const auto parameter = axisParameterFromRay(ray, origin, direction);
    if (axis < 0 || axis > 2 || !parameter)
        return false;

    active_         = true;
    axis_           = axis;
    origin_         = origin;
    direction_      = direction;
    startParameter_ = *parameter;
    return true;
}

std::optional<zio::math::Vec3> GizmoDrag::update(const Ray& ray, float snapStep) const
{
    if (!active_)
        return std::nullopt;

    const auto parameter = axisParameterFromRay(ray, origin_, direction_);
    if (!parameter)
        return std::nullopt;

    float delta = *parameter - startParameter_;
    if (snapStep > 0.f)
        delta = std::round(delta / snapStep) * snapStep;
    return origin_ + direction_ * delta;
}

}
