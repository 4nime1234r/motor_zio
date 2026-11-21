#include "EditorApp.h"

int main(int argc, char** argv)
{
    zio_editor::EditorApp app;
    if (argc > 1)
        app.openProjectAtStartup(argv[1]);
    else
        app.openRecentProject();
    app.run();
    return 0;
}
