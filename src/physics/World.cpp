#include "zio/physics/World.h"

#include <algorithm>
#include <unordered_map>

#include "zio/physics/Collision.h"

namespace zio::physics
{

namespace
{

struct BodyData
{
    BodyType type          = BodyType::Dynamic;
    math::Vec2 position     = math::Vec2::zero();
    math::Vec2 velocity     = math::Vec2::zero();
    Shape localShape        = Circle{};
    float invMass            = 0.f;
    float restitution        = 0.f;
};

void resolveCollision(BodyData& a, BodyData& b, const Manifold& manifold)
{
    const float invMassSum = a.invMass + b.invMass;
    if (invMassSum <= 0.f)
        return;

    const math::Vec2 relativeVelocity = b.velocity - a.velocity;
    const float velocityAlongNormal    = relativeVelocity.dot(manifold.normal);

    if (velocityAlongNormal <= 0.f)
    {
        const float restitution = std::min(a.restitution, b.restitution);
        const float j              = -(1.f + restitution) * velocityAlongNormal / invMassSum;
        const math::Vec2 impulse   = manifold.normal * j;
        a.velocity = a.velocity - impulse * a.invMass;
        b.velocity += impulse * b.invMass;
    }

    constexpr float kSlop    = 0.01f;
    constexpr float kPercent = 0.2f;
    const math::Vec2 correction =
        manifold.normal * (kPercent * std::max(manifold.penetration - kSlop, 0.f) / invMassSum);
    a.position = a.position - correction * a.invMass;
    b.position += correction * b.invMass;
}

}

struct World::Impl
{
    std::unordered_map<BodyHandle, BodyData> bodies;
    BodyHandle nextHandle = 1;
    math::Vec2 gravity      = math::Vec2::zero();
    std::vector<Contact> contacts;

    Shape worldShapeOf(const BodyData& body) const { return physics::translated(body.localShape, body.position); }
};

World::World() : impl_(std::make_unique<Impl>()) {}
World::~World() = default;

World& World::instance()
{
    static World global;
    return global;
}

void World::setGravity(math::Vec2 gravity)
{
    impl_->gravity = gravity;
}

math::Vec2 World::gravity() const
{
    return impl_->gravity;
}

BodyHandle World::createBody(const BodyDesc& desc)
{
    const bool movable = desc.type == BodyType::Dynamic && desc.mass > 0.f;
    if (desc.type == BodyType::Dynamic && !movable)
        return kInvalidBodyHandle;

    BodyData data;
    data.type        = desc.type;
    data.position     = desc.position;
    data.velocity     = desc.velocity;
    data.localShape   = desc.shape;
    data.invMass      = movable ? 1.f / desc.mass : 0.f;
    data.restitution  = std::clamp(desc.restitution, 0.f, 1.f);

    const BodyHandle handle = impl_->nextHandle++;
    impl_->bodies.emplace(handle, std::move(data));
    return handle;
}

void World::destroyBody(BodyHandle handle)
{
    impl_->bodies.erase(handle);
}

bool World::hasBody(BodyHandle handle) const
{
    return impl_->bodies.contains(handle);
}

void World::setPosition(BodyHandle handle, math::Vec2 position)
{
    auto it = impl_->bodies.find(handle);
    if (it != impl_->bodies.end())
        it->second.position = position;
}

math::Vec2 World::position(BodyHandle handle) const
{
    auto it = impl_->bodies.find(handle);
    return it != impl_->bodies.end() ? it->second.position : math::Vec2::zero();
}

void World::setVelocity(BodyHandle handle, math::Vec2 velocity)
{
    auto it = impl_->bodies.find(handle);
    if (it != impl_->bodies.end())
        it->second.velocity = velocity;
}

math::Vec2 World::velocity(BodyHandle handle) const
{
    auto it = impl_->bodies.find(handle);
    return it != impl_->bodies.end() ? it->second.velocity : math::Vec2::zero();
}

void World::applyImpulse(BodyHandle handle, math::Vec2 impulse)
{
    auto it = impl_->bodies.find(handle);
    if (it != impl_->bodies.end())
        it->second.velocity += impulse * it->second.invMass;
}

Shape World::worldShape(BodyHandle handle) const
{
    auto it = impl_->bodies.find(handle);
    return it != impl_->bodies.end() ? impl_->worldShapeOf(it->second) : Shape{Circle{}};
}

void World::step(float dt)
{
    impl_->contacts.clear();
    if (dt <= 0.f)
        return;

    for (auto& [handle, body] : impl_->bodies)
    {
        if (body.type != BodyType::Dynamic)
            continue;
        body.velocity += impl_->gravity * dt;
        body.position += body.velocity * dt;
    }

    std::vector<BodyHandle> handles;
    handles.reserve(impl_->bodies.size());
    for (auto& [handle, body] : impl_->bodies)
        handles.push_back(handle);

    for (std::size_t i = 0; i < handles.size(); ++i)
    {
        BodyData& a = impl_->bodies.at(handles[i]);
        for (std::size_t j = i + 1; j < handles.size(); ++j)
        {
            BodyData& b = impl_->bodies.at(handles[j]);
            if (a.type != BodyType::Dynamic && b.type != BodyType::Dynamic)
                continue;

            const Manifold manifold = testShapes(impl_->worldShapeOf(a), impl_->worldShapeOf(b));
            if (!manifold.colliding)
                continue;

            resolveCollision(a, b, manifold);
            impl_->contacts.push_back(Contact{handles[i], handles[j], manifold.normal, manifold.penetration});
        }
    }
}

const std::vector<Contact>& World::contacts() const
{
    return impl_->contacts;
}

}
