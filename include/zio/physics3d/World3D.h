#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/math/Quaternion.h"
#include "zio/math/Vec3.h"
#include "zio/physics3d/Shapes3D.h"

namespace zio::physics3d
{

using BodyHandle                                = std::uint32_t;
inline constexpr BodyHandle kInvalidBodyHandle = 0;

enum class BodyType : std::uint8_t
{
    Static,
    Dynamic,
    Kinematic,
};

struct BodyDesc
{
    BodyType type                = BodyType::Dynamic;
    math::Vec3 position          = math::Vec3::zero();
    math::Quaternion rotation    = math::Quaternion::identity();
    math::Vec3 linearVelocity    = math::Vec3::zero();
    math::Vec3 angularVelocity   = math::Vec3::zero();
    Shape3D shape                = SphereShape{0.5f};
    float mass                   = 1.f;
    float restitution            = 0.2f;
    float friction               = 0.5f;
    float linearDamping          = 0.01f;
    float angularDamping         = 0.05f;
};

struct Contact
{
    BodyHandle a            = kInvalidBodyHandle;
    BodyHandle b            = kInvalidBodyHandle;
    math::Vec3 point        = math::Vec3::zero();
    math::Vec3 normal       = math::Vec3::zero();
    float penetration       = 0.f;
};

struct RayHit
{
    BodyHandle body     = kInvalidBodyHandle;
    float distance      = 0.f;
    math::Vec3 point    = math::Vec3::zero();
    math::Vec3 normal   = math::Vec3::zero();
};

class ZIO_API World3D
{
public:
    World3D();
    ~World3D();

    World3D(const World3D&)            = delete;
    World3D& operator=(const World3D&) = delete;

    static World3D& instance();

    void setGravity(math::Vec3 gravity);
    math::Vec3 gravity() const;

    BodyHandle createBody(const BodyDesc& desc);
    void destroyBody(BodyHandle handle);
    bool hasBody(BodyHandle handle) const;
    void clear();

    void setPosition(BodyHandle handle, math::Vec3 position);
    math::Vec3 position(BodyHandle handle) const;
    void setRotation(BodyHandle handle, math::Quaternion rotation);
    math::Quaternion rotation(BodyHandle handle) const;
    void setLinearVelocity(BodyHandle handle, math::Vec3 velocity);
    math::Vec3 linearVelocity(BodyHandle handle) const;
    void setAngularVelocity(BodyHandle handle, math::Vec3 velocity);
    math::Vec3 angularVelocity(BodyHandle handle) const;

    void applyImpulse(BodyHandle handle, math::Vec3 impulse);
    void applyImpulseAtPoint(BodyHandle handle, math::Vec3 impulse, math::Vec3 worldPoint);
    void applyForce(BodyHandle handle, math::Vec3 force);
    void applyTorque(BodyHandle handle, math::Vec3 torque);

    void step(float dt);

    const std::vector<Contact>& contacts() const;

    std::optional<RayHit> raycast(math::Vec3 origin, math::Vec3 direction, float maxDistance) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
