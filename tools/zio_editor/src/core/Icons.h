#pragma once

namespace zio_editor
{

enum class Icon
{
    Move,
    Rotate,
    Scale,
    Focus,
    Play,
    Stop
};

bool iconButton(const char* id, Icon icon, bool active, const char* tooltip, float size = 0.f);

}
