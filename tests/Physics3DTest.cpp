#include <cmath>
#include <cstdio>

#include "zio/physics3d/World3D.h"

using namespace zio::math;
using namespace zio::physics3d;

namespace
{

int failures = 0;

void check(bool condition, const char* name, float value = 0.f)
{
    std::printf("%s %s (%.4f)\n", condition ? "[ok]  " : "[FALLO]", name, value);
    if (!condition)
        ++failures;
}

BodyHandle ground(World3D& world)
{
    BodyDesc desc;
    desc.type      = BodyType::Static;
    desc.position  = {0.f, -0.5f, 0.f};
    desc.shape     = BoxShape{{20.f, 0.5f, 20.f}};
    desc.friction  = 0.8f;
    return world.createBody(desc);
}

void run(World3D& world, float seconds)
{
    const int steps = static_cast<int>(seconds * 60.f);
    for (int i = 0; i < steps; ++i)
        world.step(1.f / 60.f);
}

void sphereRests()
{
    World3D world;
    ground(world);
    BodyDesc desc;
    desc.position    = {0.f, 5.f, 0.f};
    desc.shape       = SphereShape{0.5f};
    desc.restitution = 0.f;
    const BodyHandle sphere = world.createBody(desc);
    run(world, 3.f);
    check(std::fabs(world.position(sphere).y - 0.5f) < 0.03f, "una esfera cae y queda apoyada en el suelo", world.position(sphere).y);
    check(world.linearVelocity(sphere).length() < 0.1f, "la esfera queda en reposo", world.linearVelocity(sphere).length());
}

void boxRests()
{
    World3D world;
    ground(world);
    BodyDesc desc;
    desc.position = {0.f, 3.f, 0.f};
    desc.shape    = BoxShape{{0.5f, 0.5f, 0.5f}};
    const BodyHandle box = world.createBody(desc);
    run(world, 3.f);
    check(std::fabs(world.position(box).y - 0.5f) < 0.03f, "una caja cae y queda apoyada en el suelo", world.position(box).y);
    check(world.linearVelocity(box).length() < 0.1f, "la caja queda en reposo", world.linearVelocity(box).length());
}

void tiltedBoxSettles()
{
    World3D world;
    ground(world);
    BodyDesc desc;
    desc.position = {0.f, 2.f, 0.f};
    desc.rotation = Quaternion::fromEulerDegrees({25.f, 0.f, 30.f});
    desc.shape    = BoxShape{{0.5f, 0.5f, 0.5f}};
    const BodyHandle box = world.createBody(desc);
    run(world, 5.f);
    const Vec3 up = world.rotation(box).rotate(Vec3::unitY());
    check(std::fabs(world.position(box).y - 0.5f) < 0.06f, "una caja inclinada acaba plana sobre el suelo", world.position(box).y);
    check(up.y > 0.98f, "la caja inclinada queda con la cara hacia arriba", up.y);
}

void stackStays()
{
    World3D world;
    ground(world);
    BodyHandle boxes[3];
    for (int i = 0; i < 3; ++i)
    {
        BodyDesc desc;
        desc.position = {0.f, 0.5f + static_cast<float>(i) * 1.02f, 0.f};
        desc.shape    = BoxShape{{0.5f, 0.5f, 0.5f}};
        boxes[i]      = world.createBody(desc);
    }
    run(world, 4.f);
    check(std::fabs(world.position(boxes[0]).y - 0.5f) < 0.06f, "pila de cajas: la de abajo", world.position(boxes[0]).y);
    check(std::fabs(world.position(boxes[1]).y - 1.5f) < 0.1f, "pila de cajas: la del medio", world.position(boxes[1]).y);
    check(std::fabs(world.position(boxes[2]).y - 2.5f) < 0.15f, "pila de cajas: la de arriba", world.position(boxes[2]).y);
    check(std::fabs(world.position(boxes[2]).x) < 0.2f && std::fabs(world.position(boxes[2]).z) < 0.2f,
          "la pila no se desploma de lado", world.position(boxes[2]).x);
}

void bounces()
{
    World3D world;
    ground(world);
    BodyDesc desc;
    desc.position    = {0.f, 3.f, 0.f};
    desc.shape       = SphereShape{0.5f};
    desc.restitution = 0.8f;
    const BodyHandle ball = world.createBody(desc);
    float maxUp = 0.f;
    for (int i = 0; i < 240; ++i)
    {
        world.step(1.f / 60.f);
        maxUp = std::fmax(maxUp, world.linearVelocity(ball).y);
    }
    check(maxUp > 3.f, "una esfera con rebote sale hacia arriba tras el golpe", maxUp);
}

void frictionStops()
{
    World3D world;
    ground(world);
    BodyDesc desc;
    desc.position       = {0.f, 0.5f, 0.f};
    desc.linearVelocity = {6.f, 0.f, 0.f};
    desc.shape          = BoxShape{{0.5f, 0.5f, 0.5f}};
    desc.friction       = 0.8f;
    const BodyHandle box = world.createBody(desc);
    run(world, 3.f);
    check(std::fabs(world.linearVelocity(box).x) < 0.1f, "la fricción detiene una caja deslizando", world.linearVelocity(box).x);
    check(world.position(box).x > 1.f && world.position(box).x < 6.f, "la caja recorre una distancia razonable", world.position(box).x);
}

void sphereOnBox()
{
    World3D world;
    ground(world);
    BodyDesc boxDesc;
    boxDesc.type     = BodyType::Static;
    boxDesc.position = {0.f, 0.5f, 0.f};
    boxDesc.shape    = BoxShape{{1.f, 0.5f, 1.f}};
    world.createBody(boxDesc);

    BodyDesc desc;
    desc.position = {0.2f, 4.f, 0.f};
    desc.shape    = SphereShape{0.5f};
    const BodyHandle sphere = world.createBody(desc);
    run(world, 3.f);
    check(std::fabs(world.position(sphere).y - 1.5f) < 0.05f, "una esfera se apoya sobre una caja fija", world.position(sphere).y);
}

void kinematicPushes()
{
    World3D world;
    BodyDesc platform;
    platform.type           = BodyType::Kinematic;
    platform.position       = {0.f, 0.f, 0.f};
    platform.linearVelocity = {0.f, 1.f, 0.f};
    platform.shape          = BoxShape{{2.f, 0.25f, 2.f}};
    const BodyHandle plat   = world.createBody(platform);

    BodyDesc desc;
    desc.position = {0.f, 0.8f, 0.f};
    desc.shape    = BoxShape{{0.25f, 0.25f, 0.25f}};
    const BodyHandle box = world.createBody(desc);
    run(world, 2.f);
    check(world.position(plat).y > 1.9f, "una plataforma cinemática sube con su velocidad", world.position(plat).y);
    check(world.position(box).y > 2.0f, "la caja sube llevada por la plataforma", world.position(box).y);
}

void raycasts()
{
    World3D world;
    ground(world);
    BodyDesc desc;
    desc.position = {0.f, 2.f, 0.f};
    desc.shape    = SphereShape{1.f};
    const BodyHandle sphere = world.createBody(desc);

    const auto hitSphere = world.raycast({0.f, 10.f, 0.f}, {0.f, -1.f, 0.f}, 100.f);
    check(hitSphere && hitSphere->body == sphere && std::fabs(hitSphere->distance - 7.f) < 0.01f,
          "el rayo golpea la esfera a 7 m", hitSphere ? hitSphere->distance : -1.f);

    const auto hitGround = world.raycast({10.f, 5.f, 0.f}, {0.f, -1.f, 0.f}, 100.f);
    check(hitGround && std::fabs(hitGround->distance - 5.f) < 0.01f && hitGround->normal.y > 0.99f,
          "el rayo golpea el suelo con normal hacia arriba", hitGround ? hitGround->distance : -1.f);

    const auto miss = world.raycast({0.f, 10.f, 0.f}, {0.f, -1.f, 0.f}, 3.f);
    check(!miss, "el rayo no llega si el alcance es corto");
}

void impulses()
{
    World3D world;
    world.setGravity({0.f, 0.f, 0.f});
    BodyDesc desc;
    desc.shape = BoxShape{{0.5f, 0.5f, 0.5f}};
    desc.mass  = 2.f;
    const BodyHandle box = world.createBody(desc);
    world.applyImpulse(box, {4.f, 0.f, 0.f});
    check(std::fabs(world.linearVelocity(box).x - 2.f) < 1e-4f, "un impulso cambia la velocidad según la masa", world.linearVelocity(box).x);

    world.applyImpulseAtPoint(box, {0.f, 0.f, 3.f}, {0.5f, 0.f, 0.f});
    check(world.angularVelocity(box).length() > 0.1f, "un impulso descentrado genera giro", world.angularVelocity(box).length());
}

void handles()
{
    World3D world;
    const BodyHandle a = world.createBody({});
    check(world.hasBody(a), "un cuerpo creado existe");
    world.destroyBody(a);
    check(!world.hasBody(a), "un cuerpo destruido deja de existir");
    const BodyHandle b = world.createBody({});
    check(b == a, "el identificador libre se reutiliza", static_cast<float>(b));
}

}

int main()
{
    sphereRests();
    boxRests();
    tiltedBoxSettles();
    stackStays();
    bounces();
    frictionStops();
    sphereOnBox();
    kinematicPushes();
    raycasts();
    impulses();
    handles();

    std::printf("\n%s: %d fallos\n", failures == 0 ? "TODO BIEN" : "HAY ERRORES", failures);
    return failures == 0 ? 0 : 1;
}
