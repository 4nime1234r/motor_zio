#include "zio/physics3d/World3D.h"

#include <algorithm>
#include <cmath>

#include "Collision3D.h"

namespace zio::physics3d
{

namespace
{

using math::Quaternion;
using math::Vec3;

constexpr float kFixedStep            = 1.f / 120.f;
constexpr int kMaxSubSteps            = 8;
constexpr int kIterations             = 12;
constexpr float kBaumgarte            = 0.2f;
constexpr float kSlop                 = 0.005f;
constexpr float kRestitutionThreshold = 1.f;

struct Body
{
    bool alive = false;
    BodyType type = BodyType::Dynamic;
    Vec3 position;
    Quaternion rotation;
    Vec3 velocity;
    Vec3 angular;
    Vec3 force;
    Vec3 torque;
    Shape3D shape = SphereShape{0.5f};
    float invMass = 0.f;
    Vec3 invInertia;
    float restitution = 0.f;
    float friction    = 0.f;
    float linearDamping  = 0.f;
    float angularDamping = 0.f;
};

struct Constraint
{
    int a = 0;
    int b = 0;
    Vec3 normal;
    Vec3 tangent1;
    Vec3 tangent2;
    Vec3 rA;
    Vec3 rB;
    float normalMass   = 0.f;
    float tangent1Mass = 0.f;
    float tangent2Mass = 0.f;
    float bias         = 0.f;
    float friction     = 0.f;
    float accNormal    = 0.f;
    float accTangent1  = 0.f;
    float accTangent2  = 0.f;
};

Vec3 inverseInertiaWorld(const Body& body, const Vec3& v)
{
    return body.rotation.rotate(body.invInertia * body.rotation.conjugate().rotate(v));
}

Vec3 pointVelocity(const Body& body, const Vec3& r)
{
    return body.velocity + body.angular.cross(r);
}

void applyImpulseAt(Body& a, Body& b, const Vec3& rA, const Vec3& rB, const Vec3& impulse)
{
    a.velocity = a.velocity - impulse * a.invMass;
    a.angular  = a.angular - inverseInertiaWorld(a, rA.cross(impulse));
    b.velocity = b.velocity + impulse * b.invMass;
    b.angular  = b.angular + inverseInertiaWorld(b, rB.cross(impulse));
}

float effectiveMass(const Body& a, const Body& b, const Vec3& rA, const Vec3& rB, const Vec3& direction)
{
    const Vec3 ra = rA.cross(direction);
    const Vec3 rb = rB.cross(direction);
    const float k = a.invMass + b.invMass + inverseInertiaWorld(a, ra).cross(rA).dot(direction) +
                    inverseInertiaWorld(b, rb).cross(rB).dot(direction);
    return k > 1e-9f ? 1.f / k : 0.f;
}

void tangentBasis(const Vec3& n, Vec3& t1, Vec3& t2)
{
    const Vec3 helper = std::fabs(n.x) < 0.9f ? Vec3::unitX() : Vec3::unitY();
    t1                = n.cross(helper).normalized();
    t2                = n.cross(t1);
}

detail::Pose poseOf(const Body& body)
{
    return detail::Pose{body.position, body.rotation};
}

}

struct World3D::Impl
{
    Vec3 gravity = {0.f, -9.81f, 0.f};
    std::vector<Body> bodies;
    std::vector<BodyHandle> freeList;
    std::vector<Contact> contacts;
    std::vector<Constraint> constraints;
    float accumulator = 0.f;

    Body* find(BodyHandle handle)
    {
        if (handle == kInvalidBodyHandle || handle > bodies.size() || !bodies[handle - 1].alive)
            return nullptr;
        return &bodies[handle - 1];
    }

    const Body* find(BodyHandle handle) const
    {
        if (handle == kInvalidBodyHandle || handle > bodies.size() || !bodies[handle - 1].alive)
            return nullptr;
        return &bodies[handle - 1];
    }

    void collect(float dt)
    {
        constraints.clear();
        contacts.clear();

        const std::size_t count = bodies.size();
        std::vector<detail::Aabb> boxes(count);
        for (std::size_t i = 0; i < count; ++i)
            if (bodies[i].alive)
                boxes[i] = detail::computeAabb(bodies[i].shape, poseOf(bodies[i]));

        for (std::size_t i = 0; i < count; ++i)
        {
            if (!bodies[i].alive)
                continue;
            for (std::size_t j = i + 1; j < count; ++j)
            {
                if (!bodies[j].alive)
                    continue;
                Body& a = bodies[i];
                Body& b = bodies[j];
                if (a.invMass == 0.f && b.invMass == 0.f)
                    continue;
                if (!boxes[i].overlaps(boxes[j]))
                    continue;

                detail::Manifold manifold;
                if (!detail::collide(a.shape, poseOf(a), b.shape, poseOf(b), manifold))
                    continue;

                for (int k = 0; k < manifold.count; ++k)
                {
                    const detail::ContactPoint& cp = manifold.points[k];

                    Constraint c;
                    c.a        = static_cast<int>(i);
                    c.b        = static_cast<int>(j);
                    c.normal   = manifold.normal;
                    c.rA       = cp.point - a.position;
                    c.rB       = cp.point - b.position;
                    c.friction = std::sqrt(a.friction * b.friction);
                    tangentBasis(c.normal, c.tangent1, c.tangent2);
                    c.normalMass   = effectiveMass(a, b, c.rA, c.rB, c.normal);
                    c.tangent1Mass = effectiveMass(a, b, c.rA, c.rB, c.tangent1);
                    c.tangent2Mass = effectiveMass(a, b, c.rA, c.rB, c.tangent2);

                    const float closing = (pointVelocity(b, c.rB) - pointVelocity(a, c.rA)).dot(c.normal);
                    float restitutionBias = 0.f;
                    if (closing < -kRestitutionThreshold)
                        restitutionBias = -std::max(a.restitution, b.restitution) * closing;
                    const float separation = std::max(cp.penetration - kSlop, 0.f);
                    c.bias                 = std::max(kBaumgarte / dt * separation, restitutionBias);
                    constraints.push_back(c);

                    Contact contact;
                    contact.a           = static_cast<BodyHandle>(i + 1);
                    contact.b           = static_cast<BodyHandle>(j + 1);
                    contact.point       = cp.point;
                    contact.normal      = manifold.normal;
                    contact.penetration = cp.penetration;
                    contacts.push_back(contact);
                }
            }
        }
    }

    void solve()
    {
        for (int iteration = 0; iteration < kIterations; ++iteration)
        {
            for (Constraint& c : constraints)
            {
                Body& a = bodies[static_cast<std::size_t>(c.a)];
                Body& b = bodies[static_cast<std::size_t>(c.b)];

                Vec3 relative = pointVelocity(b, c.rB) - pointVelocity(a, c.rA);

                const float vt1       = relative.dot(c.tangent1);
                float lambda1         = -vt1 * c.tangent1Mass;
                const float maxFriction = c.friction * c.accNormal;
                const float newT1     = std::clamp(c.accTangent1 + lambda1, -maxFriction, maxFriction);
                lambda1               = newT1 - c.accTangent1;
                c.accTangent1         = newT1;
                applyImpulseAt(a, b, c.rA, c.rB, c.tangent1 * lambda1);

                relative              = pointVelocity(b, c.rB) - pointVelocity(a, c.rA);
                const float vt2       = relative.dot(c.tangent2);
                float lambda2         = -vt2 * c.tangent2Mass;
                const float newT2     = std::clamp(c.accTangent2 + lambda2, -maxFriction, maxFriction);
                lambda2               = newT2 - c.accTangent2;
                c.accTangent2         = newT2;
                applyImpulseAt(a, b, c.rA, c.rB, c.tangent2 * lambda2);

                relative            = pointVelocity(b, c.rB) - pointVelocity(a, c.rA);
                const float vn      = relative.dot(c.normal);
                float lambda        = c.normalMass * (c.bias - vn);
                const float newNormal = std::max(c.accNormal + lambda, 0.f);
                lambda              = newNormal - c.accNormal;
                c.accNormal         = newNormal;
                applyImpulseAt(a, b, c.rA, c.rB, c.normal * lambda);
            }
        }
    }

    void substep(float dt)
    {
        for (Body& body : bodies)
        {
            if (!body.alive || body.type != BodyType::Dynamic)
                continue;
            body.velocity = body.velocity + (gravity + body.force * body.invMass) * dt;
            body.angular  = body.angular + inverseInertiaWorld(body, body.torque) * dt;
            body.velocity = body.velocity * std::max(0.f, 1.f - body.linearDamping * dt);
            body.angular  = body.angular * std::max(0.f, 1.f - body.angularDamping * dt);
        }

        collect(dt);
        solve();

        for (Body& body : bodies)
        {
            if (!body.alive || body.type == BodyType::Static)
                continue;
            body.position = body.position + body.velocity * dt;
            const Quaternion spin{body.angular.x, body.angular.y, body.angular.z, 0.f};
            const Quaternion delta = spin * body.rotation;
            body.rotation          = Quaternion{body.rotation.x + 0.5f * dt * delta.x, body.rotation.y + 0.5f * dt * delta.y,
                                       body.rotation.z + 0.5f * dt * delta.z, body.rotation.w + 0.5f * dt * delta.w}
                                .normalized();
        }
    }
};

World3D::World3D() : impl_(std::make_unique<Impl>()) {}
World3D::~World3D() = default;

World3D& World3D::instance()
{
    static World3D global;
    return global;
}

void World3D::setGravity(math::Vec3 gravity)
{
    impl_->gravity = gravity;
}

math::Vec3 World3D::gravity() const
{
    return impl_->gravity;
}

BodyHandle World3D::createBody(const BodyDesc& desc)
{
    Body body;
    body.alive          = true;
    body.type           = desc.type;
    body.position       = desc.position;
    body.rotation       = desc.rotation.normalized();
    body.velocity       = desc.type == BodyType::Static ? Vec3::zero() : desc.linearVelocity;
    body.angular        = desc.type == BodyType::Static ? Vec3::zero() : desc.angularVelocity;
    body.shape          = desc.shape;
    body.restitution    = std::clamp(desc.restitution, 0.f, 1.f);
    body.friction       = std::max(desc.friction, 0.f);
    body.linearDamping  = std::max(desc.linearDamping, 0.f);
    body.angularDamping = std::max(desc.angularDamping, 0.f);

    if (desc.type == BodyType::Dynamic)
    {
        const float mass = desc.mass > 0.f ? desc.mass : 1.f;
        body.invMass     = 1.f / mass;
        if (const auto* sphere = std::get_if<SphereShape>(&desc.shape))
        {
            const float inertia = 0.4f * mass * sphere->radius * sphere->radius;
            const float inverse = inertia > 0.f ? 1.f / inertia : 0.f;
            body.invInertia     = Vec3{inverse, inverse, inverse};
        }
        else
        {
            const Vec3 h = std::get<BoxShape>(desc.shape).halfExtents;
            const float ix = mass / 3.f * (h.y * h.y + h.z * h.z);
            const float iy = mass / 3.f * (h.x * h.x + h.z * h.z);
            const float iz = mass / 3.f * (h.x * h.x + h.y * h.y);
            body.invInertia = Vec3{ix > 0.f ? 1.f / ix : 0.f, iy > 0.f ? 1.f / iy : 0.f, iz > 0.f ? 1.f / iz : 0.f};
        }
    }

    if (!impl_->freeList.empty())
    {
        const BodyHandle handle = impl_->freeList.back();
        impl_->freeList.pop_back();
        impl_->bodies[handle - 1] = body;
        return handle;
    }
    impl_->bodies.push_back(body);
    return static_cast<BodyHandle>(impl_->bodies.size());
}

void World3D::destroyBody(BodyHandle handle)
{
    if (Body* body = impl_->find(handle))
    {
        *body = Body{};
        impl_->freeList.push_back(handle);
    }
}

bool World3D::hasBody(BodyHandle handle) const
{
    return impl_->find(handle) != nullptr;
}

void World3D::clear()
{
    impl_->bodies.clear();
    impl_->freeList.clear();
    impl_->contacts.clear();
    impl_->constraints.clear();
    impl_->accumulator = 0.f;
}

void World3D::setPosition(BodyHandle handle, math::Vec3 position)
{
    if (Body* body = impl_->find(handle))
        body->position = position;
}

math::Vec3 World3D::position(BodyHandle handle) const
{
    const Body* body = impl_->find(handle);
    return body ? body->position : Vec3::zero();
}

void World3D::setRotation(BodyHandle handle, math::Quaternion rotation)
{
    if (Body* body = impl_->find(handle))
        body->rotation = rotation.normalized();
}

math::Quaternion World3D::rotation(BodyHandle handle) const
{
    const Body* body = impl_->find(handle);
    return body ? body->rotation : Quaternion::identity();
}

void World3D::setLinearVelocity(BodyHandle handle, math::Vec3 velocity)
{
    if (Body* body = impl_->find(handle))
        if (body->type != BodyType::Static)
            body->velocity = velocity;
}

math::Vec3 World3D::linearVelocity(BodyHandle handle) const
{
    const Body* body = impl_->find(handle);
    return body ? body->velocity : Vec3::zero();
}

void World3D::setAngularVelocity(BodyHandle handle, math::Vec3 velocity)
{
    if (Body* body = impl_->find(handle))
        if (body->type != BodyType::Static)
            body->angular = velocity;
}

math::Vec3 World3D::angularVelocity(BodyHandle handle) const
{
    const Body* body = impl_->find(handle);
    return body ? body->angular : Vec3::zero();
}

void World3D::applyImpulse(BodyHandle handle, math::Vec3 impulse)
{
    if (Body* body = impl_->find(handle))
        body->velocity = body->velocity + impulse * body->invMass;
}

void World3D::applyImpulseAtPoint(BodyHandle handle, math::Vec3 impulse, math::Vec3 worldPoint)
{
    if (Body* body = impl_->find(handle))
    {
        body->velocity = body->velocity + impulse * body->invMass;
        body->angular  = body->angular + inverseInertiaWorld(*body, (worldPoint - body->position).cross(impulse));
    }
}

void World3D::applyForce(BodyHandle handle, math::Vec3 force)
{
    if (Body* body = impl_->find(handle))
        body->force = body->force + force;
}

void World3D::applyTorque(BodyHandle handle, math::Vec3 torque)
{
    if (Body* body = impl_->find(handle))
        body->torque = body->torque + torque;
}

void World3D::step(float dt)
{
    if (!(dt > 0.f))
        return;

    impl_->accumulator += dt;
    int steps = 0;
    while (impl_->accumulator >= kFixedStep && steps < kMaxSubSteps)
    {
        impl_->substep(kFixedStep);
        impl_->accumulator -= kFixedStep;
        ++steps;
    }
    if (steps == kMaxSubSteps)
        impl_->accumulator = 0.f;

    for (Body& body : impl_->bodies)
    {
        body.force  = Vec3::zero();
        body.torque = Vec3::zero();
    }
}

const std::vector<Contact>& World3D::contacts() const
{
    return impl_->contacts;
}

std::optional<RayHit> World3D::raycast(math::Vec3 origin, math::Vec3 direction, float maxDistance) const
{
    const float length = direction.length();
    if (!(length > 0.f) || !(maxDistance > 0.f))
        return std::nullopt;
    const Vec3 dir = direction * (1.f / length);

    std::optional<RayHit> best;
    for (std::size_t i = 0; i < impl_->bodies.size(); ++i)
    {
        const Body& body = impl_->bodies[i];
        if (!body.alive)
            continue;
        float distance = 0.f;
        Vec3 normal;
        const float limit = best ? best->distance : maxDistance;
        if (!detail::raycastShape(body.shape, poseOf(body), origin, dir, limit, distance, normal))
            continue;
        RayHit hit;
        hit.body     = static_cast<BodyHandle>(i + 1);
        hit.distance = distance;
        hit.point    = origin + dir * distance;
        hit.normal   = normal;
        best         = hit;
    }
    return best;
}

}
