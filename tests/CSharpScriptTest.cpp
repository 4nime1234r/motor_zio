#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "zio/scene3d/Node3D.h"
#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"
#include "zio/scripting/DotNet.h"

namespace fs = std::filesystem;
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

bool near(float a, float b)
{
    return a > b - 0.01f && a < b + 0.01f;
}

bool contains(const std::vector<std::string>& lines, const std::string& text)
{
    for (const auto& line : lines)
        if (line.find(text) != std::string::npos)
            return true;
    return false;
}

}

int main()
{
    auto& runtime = scripting::DotNetRuntime::instance();
    if (!scripting::DotNetRuntime::embeddedBridgeAvailable() || !scripting::DotNetRuntime::dotnetSdkAvailable())
    {
        std::printf("[omitida] no hay SDK de .NET o puente de C# en esta compilacion\n");
        return 0;
    }

    const fs::path base = fs::temp_directory_path() / "zio_csharp_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "assets" / "scripts");

    std::string error;
    check(runtime.start(base / ".zio", &error), "se inicia el runtime de .NET");
    if (!runtime.running())
    {
        std::printf("error: %s\n", error.c_str());
        return 1;
    }
    check(runtime.start(base / ".zio", &error), "iniciarlo otra vez es inocuo");

    {
        std::ofstream bad(base / "assets" / "scripts" / "Malo.cs");
        bad << "public class Malo : Zio.Script { void Falta( }\n";
    }
    std::vector<std::string> lines;
    check(!scripting::DotNetRuntime::buildScripts(base, [&](const std::string& l) { lines.push_back(l); }, &error),
          "un script con errores de sintaxis no compila");
    check(contains(lines, "error"), "se informa del error de compilacion");
    fs::remove(base / "assets" / "scripts" / "Malo.cs");

    {
        std::ofstream good(base / "assets" / "scripts" / "Mover.cs");
        good << "using System.Numerics;\nusing Zio;\n\n"
                "public class Mover : Script\n{\n"
                "    public override void OnStart() { Node.Position = new Vector3(1f, 2f, 3f); }\n"
                "    public override void OnUpdate(float dt) { var p = Node.Position; Node.Position = new Vector3(p.X + dt, p.Y, p.Z); }\n"
                "    public override void OnDestroy() { Log.Info(\"adios \" + Node.Name); }\n"
                "}\n\n"
                "public class Roto : Script\n{\n"
                "    public override void OnUpdate(float dt) { throw new System.InvalidOperationException(\"boom\"); }\n"
                "}\n";
    }
    lines.clear();
    check(scripting::DotNetRuntime::buildScripts(base, [&](const std::string& l) { lines.push_back(l); }, &error),
          "un script correcto compila");
    if (!error.empty())
    {
        std::printf("error: %s\n", error.c_str());
        for (const auto& l : lines)
            std::printf("  %s\n", l.c_str());
    }
    check(runtime.loadScripts(scripting::DotNetRuntime::scriptsDllPath(base), &error), "se cargan los scripts compilados");

    const auto names = runtime.scriptNames();
    check(names.size() == 2, "se descubren las dos clases");
    check(scripting::Registry::instance().has("Mover") && scripting::Registry::instance().has("Roto"),
          "los scripts de C# aparecen en el registro");

    scene3d::Scene3D scene;
    auto* node = scene.root().addChild<scene3d::Node3D>();
    node->setName("heroe");
    check(node->addScript("Mover"), "se adjunta un script de C#");
    check(node->addScript("Roto"), "se adjunta otro");

    scene.update(0.5f);
    check(near(node->position().x, 1.5f) && near(node->position().y, 2.f) && near(node->position().z, 3.f),
          "OnStart y OnUpdate mueven el nodo");
    scene.update(0.5f);
    check(near(node->position().x, 2.f), "OnUpdate se repite");
    check(contains(scripting::DotNetRuntime::takeLog(), "boom"), "una excepcion en un script se registra");
    scene.update(0.5f);
    check(near(node->position().x, 2.5f), "una excepcion en un script no detiene a los demas");

    const std::string text = scene3d::serializeScene3D(scene);
    scene3d::Scene3D loaded;
    check(scene3d::deserializeScene3D(loaded, text, &error), "la escena con scripts de C# se vuelve a leer");
    check(loaded.root().children().size() == 1 && loaded.root().children()[0]->scripts().size() == 2 &&
              loaded.root().children()[0]->scripts()[0].instance != nullptr,
          "los scripts de C# se vuelven a crear al leer la escena");

    scripting::DotNetRuntime::takeLog();
    node->removeScript("Mover");
    check(contains(scripting::DotNetRuntime::takeLog(), "adios heroe"), "OnDestroy se ejecuta al quitar el script");

    scene3d::Scene3D early;
    auto* lateNode = early.root().addChild<scene3d::Node3D>();
    lateNode->addScript("Fantasma");
    check(lateNode->scripts()[0].instance == nullptr, "un script desconocido espera sin instancia");

    check(runtime.loadScripts(base / "no_existe.dll", &error) == false, "cargar una biblioteca que no existe da error");
    check(!scripting::Registry::instance().has("Mover"), "tras una carga fallida ya no hay scripts de C#");

    fs::remove_all(base, ec);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
