#include "EditorApp.h"

int main(int argc, char** argv)
{
    zio_editor::EditorApp app;
    if (argc > 1)
        app.setStartupProject(argv[1]);

    zio::core::AppConfig config;
    config.title        = "Editor de Zio";
    config.windowWidth  = 1600;
    config.windowHeight = 900;
    config.targetFps    = 144;
    config.enableAudio   = false;
    config.enablePhysics = false;

    app.run(config);
    return 0;
}
