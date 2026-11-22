#include <cmath>
#include <cstdio>

#include "zio/physics/World.h"

using namespace zio::math;
using namespace zio::physics;

namespace
{

int failures = 0;

void check(bool condition, const char* name, float value = 0.f)
{
    std::printf("%s %s (%.4f)\n", condition ? "[ok]  " : "[FALLO]", name, value);
    if (!condition)
        ++failures;
}

void run(World& world, float seconds)
{
    for (int i = 0; i < static_cast<int>(seconds * 60.f); ++i)
        world.step(1.f / 60.f);
}

BodyHandle floor(World& world)
{
    BodyDesc desc;
    desc.type     = BodyType::Static;
    desc.position = {0.f, -1.f};
    desc.shape    = AABB::fromHalfExtents({20.f, 0.5f});
    return world.createBody(desc);
}

}

int main()
{
    {
        World world;
        world.setGravity({0.f, -9.81f});
        floor(world);
        BodyDesc ball;
        ball.position = {0.f, 2.f};
        ball.shape    = Circle{Vec2::zero(), 0.5f};
        ball.layer    = 2u;
        ball.mask     = 4u;
        const BodyHandle ghost = world.createBody(ball);
        run(world, 2.f);
        check(world.position(ghost).y < -3.f, "una capa fuera de la mascara atraviesa el suelo", world.position(ghost).y);
    }
    {
        World world;
        world.setGravity({0.f, -9.81f});
        BodyDesc zone;
        zone.type    = BodyType::Static;
        zone.trigger = true;
        zone.shape   = AABB::fromHalfExtents({2.f, 2.f});
        const BodyHandle area = world.createBody(zone);
        BodyDesc ball;
        ball.position = {0.f, 6.f};
        ball.shape    = Circle{Vec2::zero(), 0.5f};
        const BodyHandle body = world.createBody(ball);
        bool entered = false, exited = false;
        for (int i = 0; i < 240; ++i)
        {
            world.step(1.f / 60.f);
            for (const TriggerEvent& e : world.triggerEvents())
                if (e.trigger == area && e.other == body)
                    (e.entered ? entered : exited) = true;
        }
        check(entered && exited, "un trigger avisa de entrada y salida");
        check(world.position(body).y < -4.f, "el trigger no frena al cuerpo", world.position(body).y);
    }
    {
        World world;
        BodyDesc anchor;
        anchor.type     = BodyType::Static;
        anchor.position = {0.f, 5.f};
        anchor.shape    = Circle{Vec2::zero(), 0.1f};
        const BodyHandle top = world.createBody(anchor);
        BodyDesc bob;
        bob.position = {0.f, 3.f};
        bob.shape    = Circle{Vec2::zero(), 0.2f};
        const BodyHandle weight = world.createBody(bob);
        world.setGravity({0.f, -9.81f});
        JointDesc rod;
        rod.a      = top;
        rod.b      = weight;
        rod.length = 2.f;
        const JointHandle handle = world.createJoint(rod);
        run(world, 3.f);
        const float distance = (world.position(weight) - world.position(top)).length();
        check(std::fabs(distance - 2.f) < 0.1f, "una articulacion de distancia mantiene el largo", distance);
        world.destroyJoint(handle);
        check(!world.hasJoint(handle), "destroyJoint la elimina");
    }
    {
        World world;
        world.setGravity({0.f, -9.81f});
        floor(world);
        BodyDesc a;
        a.position = {0.f, 3.f};
        a.shape    = Circle{Vec2::zero(), 0.4f};
        const BodyHandle first = world.createBody(a);
        a.position = {1.5f, 3.f};
        const BodyHandle second = world.createBody(a);
        JointDesc weld;
        weld.type = JointType::Weld;
        weld.a    = first;
        weld.b    = second;
        world.createJoint(weld);
        run(world, 2.f);
        const Vec2 offset = world.position(second) - world.position(first);
        check(std::fabs(offset.x - 1.5f) < 0.1f && std::fabs(offset.y) < 0.1f, "una soldadura mantiene la posicion relativa", offset.x);
    }

    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
