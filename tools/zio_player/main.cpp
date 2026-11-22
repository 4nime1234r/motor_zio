#include <cstdio>
#include <string>

#include "PlayerApp.h"

int main(int argc, char** argv)
{
    std::optional<zio_player::LaunchArgs> launch = zio_player::detectLaunch(argc, argv);
    if (!launch && argc > 1 && std::string(argv[1]).rfind("--", 0) != 0)
    {
        zio_player::LaunchArgs args;
        args.package = argv[1];
        launch       = args;
    }
    if (!launch)
    {
        std::fprintf(stderr, "Uso: zio_player juego.zgame [--clave archivo.zkey]\n");
        return 2;
    }
    return zio_player::run(*launch);
}
