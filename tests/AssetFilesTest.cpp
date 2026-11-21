#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/AssetFiles.h"
#include "project/ProjectManager.h"
#include "zio/scene3d/Scene3D.h"
#include "zio/scene3d/SceneIO3D.h"

namespace fs = std::filesystem;
using namespace zio_editor;

namespace
{

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[ok]  " : "[FALLO]", name);
    if (!condition)
        ++failures;
}

bool has(const std::vector<AssetEntry>& entries, const std::string& relative)
{
    for (const AssetEntry& e : entries)
        if (e.relative == relative)
            return true;
    return false;
}

}

int main()
{
    const fs::path base = fs::temp_directory_path() / "zio_assetfiles_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);

    ProjectManager project;
    std::string error;
    check(project.createProject(base, "demo", &error), "crea un proyecto");

    check(AssetFiles::folderFor("a.zscene3d") == "assets/scenes", "las escenas van a scenes");
    check(AssetFiles::folderFor("a.GLB") == "assets/models", "los modelos van a models");
    check(AssetFiles::folderFor("a.png") == "assets/textures", "las imagenes van a textures");
    check(AssetFiles::folderFor("a.wav") == "assets/audio", "el sonido va a audio");
    check(AssetFiles::folderFor("a.cs") == "assets/scripts", "los scripts van a scripts");
    check(AssetFiles::folderFor("a.txt") == "assets", "lo demas va a assets");

    std::string reason;
    check(!AssetFiles::isValidName("", &reason), "un nombre vacio no vale");
    check(!AssetFiles::isValidName("sin_extension", &reason), "sin extension no vale");
    check(!AssetFiles::isValidName("../fuera.txt", &reason), "no se permiten rutas");
    check(!AssetFiles::isValidName("a:b.txt", &reason), "no se permiten caracteres raros");
    check(!AssetFiles::isValidName(".oculto.txt", &reason), "no se permite empezar con punto");
    check(AssetFiles::isValidName("jugador.cs", &reason), "un nombre normal vale");

    std::string relative;
    check(AssetFiles::create(project, "jugador.cs", &relative, &error) && relative == "assets/scripts/jugador.cs",
          "crea un script en su carpeta");
    check(fs::is_regular_file(project.rootDir() / "assets" / "scripts" / "jugador.cs"), "el script existe en disco");
    check(!AssetFiles::create(project, "jugador.cs", &relative, &error) && !error.empty(), "no sobrescribe un archivo existente");
    check(AssetFiles::create(project, "nivel.zscene3d", &relative, &error), "crea una escena");

    zio::scene3d::Scene3D loaded;
    check(zio::scene3d::loadScene3DFile(loaded, (project.rootDir() / "assets" / "scenes" / "nivel.zscene3d").string(), &error),
          "la escena creada se puede abrir");
    check(AssetFiles::create(project, "notas.txt", &relative, &error) && relative == "assets/notas.txt", "crea un archivo suelto");

    const auto entries = AssetFiles::scan(project.rootDir());
    check(has(entries, "assets/scripts/jugador.cs"), "el listado incluye el script");
    check(has(entries, "assets/scenes/nivel.zscene3d"), "el listado incluye la escena");
    check(has(entries, "assets/notas.txt"), "el listado incluye el archivo suelto");

    {
        std::ofstream hidden(project.rootDir() / "assets" / ".oculto");
        hidden << "x";
    }
    check(!has(AssetFiles::scan(project.rootDir()), "assets/.oculto"), "los archivos ocultos no se listan");

    check(!AssetFiles::remove(project, "project.zio", &error), "no se elimina nada fuera de assets");
    check(!AssetFiles::remove(project, "assets/../project.zio", &error), "no se escapa de assets con ..");
    check(fs::is_regular_file(project.rootDir() / "project.zio"), "project.zio sigue existiendo");
    check(AssetFiles::remove(project, "assets/scripts/jugador.cs", &error), "elimina un archivo");
    check(!fs::exists(project.rootDir() / "assets" / "scripts" / "jugador.cs"), "el archivo ya no existe");
    check(!AssetFiles::remove(project, "assets/scripts/jugador.cs", &error), "eliminar dos veces da error");
    check(!has(AssetFiles::scan(project.rootDir()), "assets/scripts/jugador.cs"), "el listado ya no lo incluye");

    fs::remove_all(base, ec);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
