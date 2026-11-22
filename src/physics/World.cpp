#include "zio/physics/World.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <utility>

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
    std::uint32_t layer      = 1u;
    std::uint32_t mask       = 0xFFFFFFFFu;
    bool trigger             = false;
};

struct JointData
{
    JointType type = JointType::Distance;
    BodyHandle a   = kInvalidBodyHandle;
    BodyHandle b   = kInvalidBodyHandle;
    math::Vec2 offset = math::Vec2::zero();
    float length      = 0.f;
    float stiffness   = 1.f;
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
    std::unordered_map<JointHandle, JointData> joints;
    JointHandle nextJoint = 1;
    std::set<std::pair<BodyHandle, BodyHandle>> stepOverlaps;
    std::set<std::pair<BodyHandle, BodyHandle>> previousOverlaps;
    std::vector<TriggerEvent> events;

    void solveJoints()
    {
        for (auto& [handle, joint] : joints)
        {
            auto ia = bodies.find(joint.a);
            auto ib = bodies.find(joint.b);
            if (ia == bodies.end() || ib == bodies.end())
                continue;
            BodyData& a = ia->second;
            BodyData& b = ib->second;
            const float invSum = a.invMass + b.invMass;
            if (invSum <= 0.f)
                continue;

            math::Vec2 target;
            math::Vec2 delta = b.position - a.position;
            if (joint.type == JointType::Weld)
            {
                target = joint.offset;
            }
            else
            {
                const float current = delta.length();
                if (current < 1e-6f)
                    continue;
                target = delta * (joint.length / current);
            }
            const math::Vec2 correction = (target - delta) * joint.stiffness;
            a.position = a.position - correction * (a.invMass / invSum);
            b.position = b.position + correction * (b.invMass / invSum);

            const math::Vec2 relative = b.velocity - a.velocity;
            math::Vec2 axis = b.position - a.position;
            const float axisLength = axis.length();
            if (joint.type == JointType::Weld)
            {
                a.velocity = a.velocity + relative * (a.invMass / invSum);
                b.velocity = b.velocity - relative * (b.invMass / invSum);
            }
            else if (axisLength > 1e-6f)
            {
                axis = axis * (1.f / axisLength);
                const float along = relative.dot(axis);
                a.velocity = a.velocity + axis * (along * a.invMass / invSum);
                b.velocity = b.velocity - axis * (along * b.invMass / invSum);
            }
        }
    }

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
    data.layer        = desc.layer;
    data.mask         = desc.mask;
    data.trigger      = desc.trigger;

    const BodyHandle handle = impl_->nextHandle++;
    impl_->bodies.emplace(handle, std::move(data));
    return handle;
}

void World::destroyBody(BodyHandle handle)
{
    for (auto it = impl_->joints.begin(); it != impl_->joints.end();)
        it = (it->second.a == handle || it->second.b == handle) ? impl_->joints.erase(it) : std::next(it);
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
            if ((a.layer & b.mask) == 0 || (b.layer & a.mask) == 0)
                continue;
            if (!a.trigger && !b.trigger && a.type != BodyType::Dynamic && b.type != BodyType::Dynamic)
                continue;

            const Manifold manifold = testShapes(impl_->worldShapeOf(a), impl_->worldShapeOf(b));
            if (!manifold.colliding)
                continue;
            if (a.trigger || b.trigger)
            {
                if (a.trigger)
                    impl_->stepOverlaps.insert({handles[i], handles[j]});
                else
                    impl_->stepOverlaps.insert({handles[j], handles[i]});
                continue;
            }

            resolveCollision(a, b, manifold);
            impl_->contacts.push_back(Contact{handles[i], handles[j], manifold.normal, manifold.penetration});
        }
    }

    for (int iteration = 0; iteration < 4; ++iteration)
        impl_->solveJoints();

    impl_->events.clear();
    for (const auto& pair : impl_->stepOverlaps)
        if (!impl_->previousOverlaps.contains(pair))
            impl_->events.push_back({pair.first, pair.second, true});
    for (const auto& pair : impl_->previousOverlaps)
        if (!impl_->stepOverlaps.contains(pair))
            impl_->events.push_back({pair.first, pair.second, false});
    impl_->previousOverlaps = impl_->stepOverlaps;
    impl_->stepOverlaps.clear();
}

void World::setLayer(BodyHandle handle, std::uint32_t layer, std::uint32_t mask)
{
    auto it = impl_->bodies.find(handle);
    if (it != impl_->bodies.end())
    {
        it->second.layer = layer;
        it->second.mask  = mask;
    }
}

void World::setTrigger(BodyHandle handle, bool trigger)
{
    auto it = impl_->bodies.find(handle);
    if (it != impl_->bodies.end())
        it->second.trigger = trigger;
}

bool World::isTrigger(BodyHandle handle) const
{
    auto it = impl_->bodies.find(handle);
    return it != impl_->bodies.end() && it->second.trigger;
}

const std::vector<TriggerEvent>& World::triggerEvents() const
{
    return impl_->events;
}

std::vector<BodyHandle> World::overlapping(BodyHandle trigger) const
{
    std::vector<BodyHandle> out;
    for (const auto& pair : impl_->previousOverlaps)
        if (pair.first == trigger)
            out.push_back(pair.second);
    return out;
}

JointHandle World::createJoint(const JointDesc& desc)
{
    auto ia = impl_->bodies.find(desc.a);
    auto ib = impl_->bodies.find(desc.b);
    if (ia == impl_->bodies.end() || ib == impl_->bodies.end() || desc.a == desc.b)
        return kInvalidJointHandle;
    JointData joint;
    joint.type      = desc.type;
    joint.a         = desc.a;
    joint.b         = desc.b;
    joint.offset    = ib->second.position - ia->second.position;
    joint.length    = desc.length >= 0.f ? desc.length : joint.offset.length();
    joint.stiffness = std::clamp(desc.stiffness, 0.f, 1.f);
    const JointHandle handle = impl_->nextJoint++;
    impl_->joints.emplace(handle, joint);
    return handle;
}

void World::destroyJoint(JointHandle handle)
{
    impl_->joints.erase(handle);
}

bool World::hasJoint(JointHandle handle) const
{
    return impl_->joints.contains(handle);
}

const std::vector<Contact>& World::contacts() const
{
    return impl_->contacts;
}

}
