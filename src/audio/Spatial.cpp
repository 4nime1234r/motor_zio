#include "zio/audio/Types.h"

#include <algorithm>
#include <cmath>

namespace zio::audio
{

SpatialGains computeSpatialGains(const Listener& listener, const math::Vec3& emitter, float minDistance,
                                 float maxDistance)
{
    minDistance = std::max(minDistance, 0.01f);
    maxDistance = std::max(maxDistance, minDistance + 0.01f);

    const math::Vec3 offset = emitter - listener.position;
    const float distance    = offset.length();

    float attenuation = 1.f;
    if (distance > minDistance)
    {
        attenuation = minDistance / distance;
        const float fade = 1.f - std::clamp((distance - minDistance) / (maxDistance - minDistance), 0.f, 1.f);
        attenuation *= fade;
    }

    float pan = 0.f;
    if (distance > 1e-4f)
    {
        const math::Vec3 forward = listener.forward.length() > 1e-6f ? listener.forward.normalized() : math::Vec3{0.f, 0.f, -1.f};
        const math::Vec3 right   = forward.cross(listener.up).normalized();
        pan                      = std::clamp((offset * (1.f / distance)).dot(right), -1.f, 1.f);
    }

    const float angle = (pan + 1.f) * 0.78539816f;
    return {attenuation * std::cos(angle), attenuation * std::sin(angle)};
}

}
