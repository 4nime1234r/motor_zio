#pragma once

#include <algorithm>
#include <cmath>

#include "zio/math/Vec3.h"
#include "zio/scene3d/Camera.h"

namespace zio_editor
{

class EditorCamera
{
public:
    static constexpr float kMinDistance = 0.05f;
    static constexpr float kMaxDistance = 5000.f;
    static constexpr float kMaxPitch    = 1.553343f;

    void resetFrom(const zio::scene3d::Camera& camera)
    {
        target_                      = camera.target;
        const zio::math::Vec3 offset = camera.position - camera.target;
        const float length           = offset.length();
        if (!(length > 1e-6f) || !std::isfinite(length))
        {
            distance_ = 5.f;
            yaw_      = 0.f;
            pitch_    = 0.3f;
            return;
        }
        distance_ = std::clamp(length, kMinDistance, kMaxDistance);
        yaw_      = std::atan2(offset.x, offset.z);
        pitch_    = std::clamp(std::asin(std::clamp(offset.y / length, -1.f, 1.f)), -kMaxPitch, kMaxPitch);
    }

    void orbit(float dxPixels, float dyPixels)
    {
        constexpr float kSensitivity = 0.005f;
        yaw_ -= dxPixels * kSensitivity;
        pitch_ = std::clamp(pitch_ + dyPixels * kSensitivity, -kMaxPitch, kMaxPitch);
    }

    void pan(float dxPixels, float dyPixels)
    {
        const zio::math::Vec3 forward = (target_ - position()).normalized();
        const zio::math::Vec3 right   = forward.cross(zio::math::Vec3::unitY()).normalized();
        const zio::math::Vec3 up      = right.cross(forward);
        const float scale             = distance_ * 0.0015f;
        target_ += (right * -dxPixels + up * dyPixels) * scale;
    }

    void zoom(float wheelSteps)
    {
        distance_ = std::clamp(distance_ * std::pow(0.9f, wheelSteps), kMinDistance, kMaxDistance);
    }

    void translate(const zio::math::Vec3& delta) { target_ += delta; }

    zio::math::Vec3 forward() const { return (target_ - position()).normalized(); }
    zio::math::Vec3 right() const { return forward().cross(zio::math::Vec3::unitY()).normalized(); }

    void fly(float forwardAmount, float rightAmount, float upAmount)
    {
        translate(forward() * forwardAmount + right() * rightAmount + zio::math::Vec3::unitY() * upAmount);
    }

    void focus(const zio::math::Vec3& point, float distance)
    {
        target_   = point;
        distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
    }

    zio::math::Vec3 position() const
    {
        const float cp = std::cos(pitch_);
        return target_ + zio::math::Vec3{cp * std::sin(yaw_), std::sin(pitch_), cp * std::cos(yaw_)} * distance_;
    }

    const zio::math::Vec3& target() const { return target_; }
    float distance() const { return distance_; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }

    zio::scene3d::Camera toCamera(const zio::scene3d::Camera& base) const
    {
        zio::scene3d::Camera camera = base;
        camera.position             = position();
        camera.target               = target_;
        camera.up                   = zio::math::Vec3::unitY();
        return camera;
    }

private:
    zio::math::Vec3 target_ = zio::math::Vec3::zero();
    float distance_         = 5.f;
    float yaw_              = 0.f;
    float pitch_            = 0.3f;
};

}
