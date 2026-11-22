#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "project/ProjectManager.h"

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

}

int main()
{
    const fs::path base = fs::temp_directory_path() / "zio_project_info_test";
    fs::remove_all(base);
    fs::create_directories(base);

    ProjectManager project;
    std::string error;
    check(project.createProject(base, "juego", &error), "se crea un proyecto");
    check(project.gameInfo().title.empty() && project.gameInfo().version == "1.0.0" && !project.gameInfo().allowImport,
          "un proyecto nuevo no tiene datos del juego");

    GameInfo info;
    info.title       = "Mi \"gran\" juego";
    info.author      = "4nime1234r";
    info.version     = "3.4.5";
    info.description = "Línea con ñ, 日本語 y \\ barra";
    info.copyright   = "(c) 2026";
    info.website     = "https://example.com/juego?x=1&y=2";
    info.license     = "Todos los derechos reservados";
    info.id          = "123e4567-e89b-42d3-a456-426614174000";
    info.entryScene  = "assets/scenes/nivel.zscene3d";
    info.allowImport = true;
    check(project.saveGameInfo(info, &error), "se guardan los datos del juego");

    ProjectManager again;
    check(again.loadProject(project.rootDir(), &error), "el proyecto se vuelve a abrir");
    const GameInfo& read = again.gameInfo();
    check(read.title == info.title && read.author == info.author && read.version == info.version &&
              read.description == info.description && read.copyright == info.copyright && read.website == info.website &&
              read.license == info.license && read.id == info.id && read.entryScene == info.entryScene && read.allowImport,
          "los datos del juego se conservan tal cual (incluidos los caracteres especiales)");
    check(again.name() == "juego", "el nombre del proyecto no cambia");
    check(!fs::exists(project.rootDir() / "project.zio.tmp"), "no queda el archivo temporal");

    ProjectManager closed;
    check(!closed.saveGameInfo(info, &error), "no se guarda sin proyecto abierto");

    fs::remove_all(base);
    std::printf("%s\n", failures == 0 ? "TODO OK" : "HAY FALLOS");
    return failures == 0 ? 0 : 1;
}
