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

    Shape worldShape(BodyHandle handle) const;

    void step(float dt);

    const std::vector<Contact>& contacts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
