#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "zio/scene3d/Node3D.h"
#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"
#include "zio/scripting/Script.h"

using namespace zio;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

std::vector<std::string> events;

class Girar : public scripting::Script
{
public:
    void onStart() override { events.push_back("start"); }
    void onUpdate(float dt) override
    {
        events.push_back("update");
        node().setPosition({node().position().x + dt, 0.f, 0.f});
    }
    void onDestroy() override { events.push_back("destroy"); }
};
ZIO_SCRIPT(Girar)

class Otro : public scripting::Script
{
public:
    void onUpdate(float) override { events.push_back("otro"); }
};
ZIO_SCRIPT(Otro)

int count(const std::string& name)
{
    int n = 0;
    for (const auto& e : events)
        if (e == name)
            ++n;
    return n;
}

}

int main()
{
    auto& registry = scripting::Registry::instance();
    check(registry.has("Girar") && registry.has("Otro"), "los scripts se registran con ZIO_SCRIPT");
    check(!registry.has("NoExiste"), "un script desconocido no esta registrado");
    check(registry.create("Girar") != nullptr && registry.create("NoExiste") == nullptr, "el registro crea instancias");
    check(!registry.add("", [] { return std::unique_ptr<scripting::Script>(); }), "no se acepta un nombre vacio");

    scene3d::Scene3D scene;
    auto* node = scene.root().addChild<scene3d::Node3D>();
    node->setName("heroe");

    check(node->addScript("Girar"), "se adjunta un script registrado");
    check(!node->addScript("Girar"), "no se adjunta dos veces el mismo");
    check(!node->addScript("Fantasma"), "un script desconocido se guarda pero no se crea");
    check(node->scriptNames().size() == 2 && node->hasScript("Fantasma"), "el nombre del desconocido se conserva");

    events.clear();
    scene.update(0.5f);
    check(count("start") == 1 && count("update") == 1, "onStart una vez y onUpdate en el primer cuadro");
    scene.update(0.5f);
    check(count("start") == 1 && count("update") == 2, "onStart no se repite");
    check(node->position().x > 0.99f && node->position().x < 1.01f, "el script mueve su nodo");

    node->addScript("Otro");
    events.clear();
    scene.update(0.1f);
    check(count("update") == 1 && count("otro") == 1, "varios scripts en un nodo se ejecutan");

    const std::string text = scene3d::serializeScene3D(scene);
    check(text.find("\"scripts\"") != std::string::npos && text.find("Fantasma") != std::string::npos,
          "los scripts se guardan en la escena");

    scene3d::Scene3D loaded;
    std::string error;
    check(scene3d::deserializeScene3D(loaded, text, &error), "la escena con scripts se vuelve a leer");
    check(loaded.root().children().size() == 1, "un hijo tras leer");
    if (loaded.root().children().size() == 1)
    {
        const auto* back = loaded.root().children()[0].get();
        const auto names = back->scriptNames();
        check(names.size() == 3 && names[0] == "Girar" && names[1] == "Fantasma" && names[2] == "Otro",
              "se conservan los nombres y el orden");
        check(back->scripts()[0].instance != nullptr && back->scripts()[1].instance == nullptr,
              "se crean las instancias de los scripts conocidos");
    }

    events.clear();
    check(node->removeScript("Girar"), "se quita un script");
    check(count("destroy") == 1, "al quitar un script ya iniciado se llama a onDestroy");
    check(!node->removeScript("Girar"), "quitar uno que no esta devuelve falso");

    {
        scene3d::Scene3D temp;
        auto* n = temp.root().addChild<scene3d::Node3D>();
        n->addScript("Girar");
        temp.update(0.1f);
        events.clear();
    }
    check(count("destroy") == 1, "al destruir la escena se llama a onDestroy");

    scene3d::Scene3D bad;
    check(!scene3d::deserializeScene3D(bad, R"({"format":"zio.scene3d","root":{"type":"Node3D","scripts":[5]}})", &error),
          "scripts mal formados dan error");

    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
