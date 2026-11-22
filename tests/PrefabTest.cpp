#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "zio/scene3d/Node3D.h"
#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"

using namespace zio::scene3d;
namespace fs = std::filesystem;

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

std::string escaped(const fs::path& path)
{
    std::string text;
    for (const char c : path.generic_string())
    {
        if (c == '"' || c == '\\')
            text += '\\';
        text += c;
    }
    return text;
}

}

int main()
{
    const fs::path dir = fs::temp_directory_path() / "zio_prefab_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    {
        Scene3D source;
        auto* body = source.root().addChild<Node3D>();
        body->setName("cuerpo");
        body->setPosition({1.f, 0.f, 0.f});
        auto* light = body->addChild<LightNode>();
        light->setName("foco");
        light->setIntensity(3.f);
        std::string error;
        check(saveScene3DFile(source, (dir / "enemigo.zscene3d").generic_string(), &error), "se guarda la escena que servira de prefab");
    }

    const std::string prefabPath = (dir / "enemigo.zscene3d").generic_string();

    Scene3D level;
    auto* first  = level.root().addChild<PrefabNode>();
    auto* second = level.root().addChild<PrefabNode>();
    std::string error;
    check(first->setSource(prefabPath, &error), "un prefab se instancia desde un archivo de escena");
    check(second->setSource(prefabPath, &error), "el mismo prefab se puede instanciar varias veces");
    first->setPosition({10.f, 0.f, 0.f});
    second->setPosition({-10.f, 0.f, 0.f});
    check(first->children().size() == 1 && second->children().size() == 1, "cada instancia trae los nodos de la escena");
    check(first->children()[0]->name() == "cuerpo" && first->children()[0]->children().size() == 1,
          "la jerarquia del prefab se conserva");
    check(first->children()[0].get() != second->children()[0].get(), "las instancias no comparten nodos");

    const std::string text = serializeScene3D(level);
    check(text.find("\"Prefab\"") != std::string::npos && text.find("enemigo.zscene3d") != std::string::npos,
          "la escena guarda la referencia al prefab");
    check(text.find("cuerpo") == std::string::npos, "los nodos del prefab no se copian en la escena");

    Scene3D loaded;
    check(deserializeScene3D(loaded, text, &error), "la escena con prefabs se vuelve a leer");
    check(loaded.root().children().size() == 2, "se leen las dos instancias");
    if (loaded.root().children().size() == 2)
    {
        const auto* a = dynamic_cast<const PrefabNode*>(loaded.root().children()[0].get());
        check(a && a->source() == prefabPath && a->children().size() == 1, "al leer, el prefab se vuelve a expandir");
        check(a && near(a->position().x, 10.f), "la posicion de la instancia se conserva");
    }

    {
        std::ofstream out(dir / "ciclo.zscene3d");
        out << "{\"format\":\"zio.scene3d\",\"version\":1,\"root\":{\"type\":\"Node3D\",\"children\":[{\"type\":\"Prefab\",\"name\":\"yo\",\"source\":\""
            << escaped(dir / "ciclo.zscene3d") << "\"}]}}";
    }
    PrefabNode loop;
    check(!loop.setSource((dir / "ciclo.zscene3d").generic_string(), &error) && !error.empty(),
          "un prefab que se contiene a si mismo da error y no se cuelga");
    check(!first->setSource((dir / "no_existe.zscene3d").generic_string(), &error), "un prefab que no existe da error");

    fs::remove_all(dir, ec);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
