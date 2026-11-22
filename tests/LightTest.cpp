#include <cmath>
#include <cstdio>
#include <string>

#include "zio/scene3d/Node3D.h"
#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"

using namespace zio::scene3d;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-3f;
}

}

int main()
{
    Scene3D scene;
    auto* point = scene.root().addChild<LightNode>();
    point->setName("puntual");
    point->setPosition({1.f, 2.f, 3.f});
    point->setColor({1.f, 0.5f, 0.25f, 1.f});
    point->setIntensity(7.f);
    point->setRange(12.f);
    point->setCastShadow(true);
    point->setShadowOpacity(0.6f);

    auto* spot = scene.root().addChild<LightNode>();
    spot->setName("focal");
    spot->setLightType(LightType::Spot);
    spot->setInnerAngle(10.f);
    spot->setOuterAngle(40.f);

    scene.environment().sky      = SkyMode::Panorama;
    scene.environment().panorama = "assets/cielo.png";
    scene.environment().fog      = FogMode::Exponential;
    scene.environment().fogDensity = 0.07f;
    scene.environment().bloom    = true;
    scene.environment().exposure = 1.4f;
    scene.environment().tint     = {1.f, 0.9f, 0.8f, 1.f};
    scene.camera().orthographic = true;
    scene.camera().orthoSize    = 7.5f;
    const std::string text = serializeScene3D(scene);

    Scene3D loaded;
    std::string error;
    check(deserializeScene3D(loaded, text, &error), "la escena con luces se carga");
    check(loaded.environment().sky == SkyMode::Panorama && loaded.environment().panorama == "assets/cielo.png",
          "el cielo de la escena se conserva");
    check(loaded.environment().fog == FogMode::Exponential && near(loaded.environment().fogDensity, 0.07f),
          "la niebla de la escena se conserva");
    check(loaded.environment().bloom && near(loaded.environment().exposure, 1.4f) && near(loaded.environment().tint.g, 0.9f),
          "el postproceso de la escena se conserva");
    check(loaded.camera().orthographic && near(loaded.camera().orthoSize, 7.5f), "la camara ortografica se conserva");
    {
        Camera ortho;
        ortho.orthographic = true;
        ortho.orthoSize    = 4.f;
        ortho.position     = {0.f, 0.f, 10.f};
        ortho.target       = {0.f, 0.f, 0.f};
        const zio::math::Mat4 vp = ortho.projectionMatrix(2.f) * ortho.viewMatrix();
        const zio::math::Vec3 edgeX = vp.transformPoint({8.f, 0.f, 0.f});
        const zio::math::Vec3 edgeY = vp.transformPoint({0.f, 4.f, 0.f});
        const zio::math::Vec3 nearP = vp.transformPoint({0.f, 0.f, 9.9f});
        const zio::math::Vec3 farP  = vp.transformPoint({0.f, 0.f, -990.f});
        check(near(edgeX.x, 1.f) && near(edgeY.y, 1.f), "la camara ortografica mapea su ancho y alto a la pantalla");
        check(nearP.z < farP.z && nearP.z >= 0.f && farP.z <= 1.001f, "la profundidad ortografica crece con la distancia");
    }
    check(loaded.root().children().size() == 2, "hay dos luces");
    if (loaded.root().children().size() == 2)
    {
        const auto* p = dynamic_cast<const LightNode*>(loaded.root().children()[0].get());
        const auto* s = dynamic_cast<const LightNode*>(loaded.root().children()[1].get());
        check(p && p->lightType() == LightType::Point, "la primera es puntual");
        check(p && near(p->intensity(), 7.f) && near(p->range(), 12.f), "intensidad y alcance se conservan");
        check(p && near(p->color().g, 0.5f) && near(p->color().b, 0.25f), "el color se conserva");
        check(p && p->castShadow() && near(p->shadowOpacity(), 0.6f), "la sombra se conserva");
        check(s && s->lightType() == LightType::Spot, "la segunda es focal");
        check(s && near(s->innerAngle(), 10.f) && near(s->outerAngle(), 40.f), "los ángulos se conservan");
    }
    check(serializeScene3D(loaded) == text, "guardar de nuevo da el mismo texto");

    Scene3D bad;
    const std::string broken = "{\"format\":\"zio.scene3d\",\"version\":1,\"root\":{\"type\":\"Node3D\",\"children\":["
                               "{\"type\":\"Light\",\"light\":\"cono\"}]}}";
    check(!deserializeScene3D(bad, broken, &error), "un tipo de luz desconocido se rechaza");

    if (failures)
    {
        std::printf("%d fallos\n", failures);
        return 1;
    }
    std::printf("todo bien\n");
    return 0;
}
