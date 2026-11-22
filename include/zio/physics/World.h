#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "zio/core/Platform.h"
#include "zio/math/Vec2.h"
#include "zio/physics/Shapes.h"

namespace zio::physics
{

using BodyHandle                                = std::uint32_t;
inline constexpr BodyHandle kInvalidBodyHandle = 0;

enum class BodyType : std::uint8_t
{
    Static,
    Dynamic,
};

struct BodyDesc
{
    BodyType type        = BodyType::Dynamic;
    math::Vec2 position   = math::Vec2::zero();
    math::Vec2 velocity   = math::Vec2::zero();
    Shape shape            = Circle{math::Vec2::zero(), 0.5f};
    float mass             = 1.f;
    float restitution      = 0.f;
    std::uint32_t layer    = 1u;
    std::uint32_t mask     = 0xFFFFFFFFu;
    bool trigger           = false;
};

struct TriggerEvent
{
    BodyHandle trigger = kInvalidBodyHandle;
    BodyHandle other   = kInvalidBodyHandle;
    bool entered       = true;
};

using JointHandle                                = std::uint32_t;
inline constexpr JointHandle kInvalidJointHandle = 0;

enum class JointType : std::uint8_t
{
    Distance,
    Weld,
};

struct JointDesc
{
    JointType type     = JointType::Distance;
    BodyHandle a       = kInvalidBodyHandle;
    BodyHandle b       = kInvalidBodyHandle;
    float length       = -1.f;
    float stiffness    = 1.f;
};

struct Contact
{
    BodyHandle a, b;
    math::Vec2 normal;
    float penetration;
};

class ZIO_API World
{
public:
    World();
    ~World();

    World(const World&)            = delete;
    World& operator=(const World&) = delete;

    static World& instance();

    void setGravity(math::Vec2 gravity);
    math::Vec2 gravity() const;

    BodyHandle createBody(const BodyDesc& desc);
    void destroyBody(BodyHandle handle);
    bool hasBody(BodyHandle handle) const;

    void setPosition(BodyHandle handle, math::Vec2 position);
    math::Vec2 position(BodyHandle handle) const;
    void setVelocity(BodyHandle handle, math::Vec2 velocity);
    math::Vec2 velocity(BodyHandle handle) const;
    void applyImpulse(BodyHandle handle, math::Vec2 impulse);

    void setLayer(BodyHandle handle, std::uint32_t layer, std::uint32_t mask);
    void setTrigger(BodyHandle handle, bool trigger);
    bool isTrigger(BodyHandle handle) const;
    const std::vector<TriggerEvent>& triggerEvents() const;
    std::vector<BodyHandle> overlapping(BodyHandle trigger) const;

    JointHandle createJoint(const JointDesc& desc);
    void destroyJoint(JointHandle handle);
    bool hasJoint(JointHandle handle) const;

    Shape worldShape(BodyHandle handle) const;

    void step(float dt);

    const std::vector<Contact>& contacts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
